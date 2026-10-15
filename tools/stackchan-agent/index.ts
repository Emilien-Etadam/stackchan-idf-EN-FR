// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
//
// stackchan-agent — the host side of the firmware's `ask_agent` tool.
//
// The Stack-chan conversation model (OpenAI Realtime / Gemini Live) can't
// browse or compute; when the user asks for something it can't answer it
// calls `ask_agent(question)`. The firmware POSTs the question here, we run
// a headless Claude Code (`claude -p`) in a scratch directory, and the
// firmware polls for the result and voices it.
//
//   POST /ask         {"question": "..."}   → 202 {"id": "..."}
//   GET  /jobs/:id                          → {"status":"running"} |
//        {"status":"done","answer":"...","reading":"..."} | {"status":"error","error":"..."}
//   GET  /health                            → {"ok":true,"running":N}
//
// Every request needs `Authorization: Bearer <STACKCHAN_AGENT_TOKEN>`.
// Plain HTTP by design: bind it to a LAN / Tailscale address, never the
// public internet (the firmware doesn't do TLS for this hop).

const TOKEN = process.env.STACKCHAN_AGENT_TOKEN ?? '';
const PORT = Number(process.env.PORT ?? 8787);
const HOST = process.env.HOST ?? '0.0.0.0';
const CLAUDE_BIN = process.env.CLAUDE_BIN ?? 'claude';
const WORKDIR = process.env.WORKDIR ?? `${process.env.HOME}/.stackchan-agent`;
const MODEL = process.env.MODEL ?? '';
const TIMEOUT_MS = Number(process.env.TIMEOUT_S ?? 150) * 1000;
// Read-only + web tools only: the device-side user can't review what the
// agent does, so it must not be able to change anything on this host.
const ALLOWED_TOOLS = process.env.ALLOWED_TOOLS ?? 'WebSearch,WebFetch,Read,Glob,Grep';
const MAX_JOBS = 64;
const JOB_TTL_MS = 15 * 60 * 1000;

if (!TOKEN) {
  console.error('stackchan-agent: STACKCHAN_AGENT_TOKEN is required');
  process.exit(2);
}

const SYSTEM_PROMPT = [
  'あなたは卓上ロボット「スタックチャン」の後ろで働くアシスタントです。',
  'ユーザーはロボットに話しかけ、ロボットがあなたの回答を音声で読み上げます。',
  '必要なら Web 検索やファイル参照で調べ、日本語で簡潔に (2〜4 文、合計 200 文字以内) 答えてください。',
  'Markdown・箸条書き・URL・記号は使わず、声に出して読める文にしてください。',
  '出力は次の JSON オブジェクトだけにしてください (前後に説明を付けない):',
  '{"answer": "<回答本文>", "reading": "<回答本文をひらがな (と長音・句読点) だけで書いた読み>"}',
].join('\n');

type Job = {
  id: string;
  question: string;
  status: 'running' | 'done' | 'error';
  answer?: string;
  reading?: string;
  error?: string;
  startedAt: number;
  finishedAt?: number;
};

const jobs = new Map<string, Job>();

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8' },
  });
}

function authorized(req: Request): boolean {
  const h = req.headers.get('authorization') ?? '';
  return h === `Bearer ${TOKEN}`;
}

function purge(): void {
  const now = Date.now();
  for (const [id, j] of jobs) {
    if (j.finishedAt && now - j.finishedAt > JOB_TTL_MS) jobs.delete(id);
  }
  while (jobs.size > MAX_JOBS) {
    const oldest = [...jobs.values()].sort((a, b) => a.startedAt - b.startedAt)[0];
    if (!oldest) break;
    jobs.delete(oldest.id);
  }
}

// `claude -p --output-format json` prints {"type":"result","result":"<text>",...}.
// The text itself should be our JSON envelope, but models sometimes wrap it
// in a code fence or add a sentence — dig the object out defensively.
function parseAnswer(resultText: string): { answer: string; reading: string } {
  const trimmed = resultText.trim().replace(/^```(?:json)?\s*/i, '').replace(/```\s*$/, '');
  const start = trimmed.indexOf('{');
  const end = trimmed.lastIndexOf('}');
  if (start >= 0 && end > start) {
    try {
      const obj = JSON.parse(trimmed.slice(start, end + 1));
      if (typeof obj.answer === 'string' && obj.answer) {
        return { answer: obj.answer, reading: typeof obj.reading === 'string' ? obj.reading : '' };
      }
    } catch {
      /* fall through */
    }
  }
  return { answer: trimmed, reading: '' };
}

async function runJob(job: Job): Promise<void> {
  const args = ['-p', job.question, '--output-format', 'json', '--append-system-prompt', SYSTEM_PROMPT];
  if (ALLOWED_TOOLS) args.push('--allowedTools', ALLOWED_TOOLS);
  if (MODEL) args.push('--model', MODEL);
  console.error(`stackchan-agent: [${job.id}] start: ${job.question}`);
  const proc = Bun.spawn([CLAUDE_BIN, ...args], {
    cwd: WORKDIR,
    stdin: 'ignore',
    stdout: 'pipe',
    stderr: 'pipe',
    env: { ...process.env },
  });
  const timer = setTimeout(() => {
    console.error(`stackchan-agent: [${job.id}] timeout, killing`);
    proc.kill();
  }, TIMEOUT_MS);
  const [stdout, stderr, code] = await Promise.all([
    new Response(proc.stdout).text(),
    new Response(proc.stderr).text(),
    proc.exited,
  ]);
  clearTimeout(timer);
  job.finishedAt = Date.now();
  if (code !== 0) {
    job.status = 'error';
    job.error = `claude が終了コード ${code} で失敗しました`;
    console.error(`stackchan-agent: [${job.id}] exit ${code}: ${stderr.slice(0, 500)}`);
    return;
  }
  let resultText = stdout;
  try {
    const envelope = JSON.parse(stdout);
    if (envelope.is_error) {
      job.status = 'error';
      job.error = String(envelope.result ?? 'claude error');
      return;
    }
    if (typeof envelope.result === 'string') resultText = envelope.result;
  } catch {
    /* not JSON — treat stdout as the answer text */
  }
  const parsed = parseAnswer(resultText);
  job.status = 'done';
  job.answer = parsed.answer;
  job.reading = parsed.reading;
  console.error(
    `stackchan-agent: [${job.id}] done in ${((job.finishedAt - job.startedAt) / 1000).toFixed(1)} s: ${parsed.answer}`,
  );
}

await Bun.$`mkdir -p ${WORKDIR}`.quiet();

Bun.serve({
  hostname: HOST,
  port: PORT,
  async fetch(req) {
    const url = new URL(req.url);
    if (!authorized(req)) return json({ error: 'unauthorized' }, 401);

    if (req.method === 'GET' && url.pathname === '/health') {
      const running = [...jobs.values()].filter((j) => j.status === 'running').length;
      return json({ ok: true, running });
    }

    if (req.method === 'POST' && url.pathname === '/ask') {
      let question = '';
      try {
        const body = (await req.json()) as { question?: unknown };
        if (typeof body.question === 'string') question = body.question.trim();
      } catch {
        return json({ error: 'bad json' }, 400);
      }
      if (!question) return json({ error: 'question required' }, 400);
      if (question.length > 2000) question = question.slice(0, 2000);
      purge();
      const job: Job = { id: crypto.randomUUID(), question, status: 'running', startedAt: Date.now() };
      jobs.set(job.id, job);
      void runJob(job).catch((e) => {
        job.status = 'error';
        job.error = (e as Error).message;
        job.finishedAt = Date.now();
      });
      return json({ id: job.id }, 202);
    }

    const m = url.pathname.match(/^\/jobs\/([0-9a-f-]+)$/);
    if (req.method === 'GET' && m) {
      const job = jobs.get(m[1]!);
      if (!job) return json({ error: 'not found' }, 404);
      if (job.status === 'running') return json({ status: 'running' });
      if (job.status === 'error') return json({ status: 'error', error: job.error ?? 'error' });
      return json({ status: 'done', answer: job.answer ?? '', reading: job.reading ?? '' });
    }

    return json({ error: 'not found' }, 404);
  },
});

console.error(`stackchan-agent: listening on http://${HOST}:${PORT} (claude=${CLAUDE_BIN}, tools=${ALLOWED_TOOLS})`);
