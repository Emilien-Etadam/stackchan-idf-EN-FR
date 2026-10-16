<!--
SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
SPDX-License-Identifier: BSL-1.0
-->

# stackchan-agent — `ask_agent` ツールの委譲先サーバー

スタックチャンの会話 (OpenAI Realtime / Gemini Live) は検索も計算もできません。
会話中に「調べて」「〇〇に聞いておいて」と頼まれると、ファームウェアは `ask_agent`
ツールでこのサーバーに依頼を投げ、サーバーはヘッドレスの Claude Code (`claude -p`)
を走らせて答えを作ります。数十秒後に答えが機体に届くと、機体は会話セッションに
その文章を流し込み、スタックチャンの口調で読み上げます。

```
[ユーザー] ─声─▶ [スタックチャン] ─ask_agent─▶ 会話 API (Gemini / OpenAI)
                      │ POST /ask {"question"}            ▲ 回答をテキスト ターンとして注入
                      ▼                                    │
               [stackchan-agent (このサーバー)] ── GET /jobs/<id> (2 秒ごと) ──┘
                      │ claude -p ... --output-format json
                      ▼
               [Claude Code CLI] (WebSearch / WebFetch / Read / Glob / Grep のみ)
```

## 必要なもの

- 機体と同じ LAN、または機体から Tailscale 経由で届くマシン (常時起動が望ましい)
- [Bun](https://bun.sh) 1.x
- [Claude Code CLI](https://claude.com/claude-code) がインストール・ログイン済み
  (`claude -p "hello"` が動くこと)
- 共有トークン (ランダムな英数字 32 文字程度)。機体側の設定「エージェント サーバー
  トークン」と同じ値

TLS は使いません (機体側の内部 RAM 節約のため)。LAN / Tailscale の中だけで公開し、
インターネットに直接出さないでください。

## 起動

```sh
cd tools/stackchan-agent
STACKCHAN_AGENT_TOKEN='<トークン>' bun run index.ts
# → stackchan-agent: listening on http://0.0.0.0:8787 ...
```

環境変数:

| 変数 | 既定 | 意味 |
|---|---|---|
| `STACKCHAN_AGENT_TOKEN` | (必須) | Bearer トークン |
| `PORT` / `HOST` | `8787` / `0.0.0.0` | 待ち受け |
| `CLAUDE_BIN` | `claude` | Claude Code CLI のパス |
| `WORKDIR` | `~/.stackchan-agent` | `claude -p` の作業ディレクトリ (空の場所にしておく) |
| `ALLOWED_TOOLS` | `WebSearch,WebFetch,Read,Glob,Grep` | エージェントに許すツール (読み取り系のみを推奨) |
| `MODEL` | (CLI 既定) | `--model` に渡す値 |
| `TIMEOUT_S` | `150` | 1 件の上限時間。機体側は 180 秒で諦める |

systemd (ユーザー ユニット) の例:

```ini
# ~/.config/systemd/user/stackchan-agent.service
[Unit]
Description=stackchan-agent (ask_agent delegate)
After=network-online.target

[Service]
# STACKCHAN_AGENT_TOKEN=<トークン> を 1 行書いたファイル (chmod 600)
EnvironmentFile=%h/.stackchan-agent/env
WorkingDirectory=%h/repos/stackchan-idf/tools/stackchan-agent
ExecStart=%h/.bun/bin/bun run index.ts
Restart=on-failure

[Install]
WantedBy=default.target
```

## ローカル LLM で動かす

`claude -p` の代わりに、Anthropic API 互換のエンドポイントを持つローカル サーバー
(llama-server / Strata など) に Claude Code を向けるラッパーを `CLAUDE_BIN` に指定すれば、
回答をローカル モデルで作れます。ラッパーは `ANTHROPIC_BASE_URL` と
`ANTHROPIC_AUTH_TOKEN`、`ANTHROPIC_MODEL` を環境変数で設定してから
`exec claude "$@"` するだけのスクリプトで構いません (引数はそのまま通す)。

```ini
# [Service] に追加
Environment=CLAUDE_BIN=%h/.local/bin/claude-local
# WebSearch は Anthropic 側で実行されるツールなのでローカル モデルでは使えない
Environment=ALLOWED_TOOLS=WebFetch,Read,Glob,Grep
# ローカルは初回の prefill に数十秒かかる。機体 / XiaoZhi 側の上限は 180 秒
Environment=TIMEOUT_S=170
```

ローカル モデルでは質問 1 件に 1 分前後かかります (システム プロンプトの prefill が
大半。2 回目以降はプレフィックス キャッシュが効けば短くなる)。Web 検索はできないので、
URL が分かっているページの参照 (WebFetch) と知識ベースの回答になります。

## 機体側の設定

Wi-Fi 設定ページ (`http://<機体>/`) の「会話」→「エージェント サーバー URL」に
`http://<このマシンの IP>:8787`、「トークン」に同じ値を入れて保存&再起動。
URL が空なら `ask_agent` ツールは会話モデルに提示されません。

## API

| | |
|---|---|
| `POST /ask` `{"question":"..."}` | `202 {"id":"..."}` |
| `GET /jobs/<id>` | `{"status":"running"}` / `{"status":"done","answer":"...","reading":"..."}` / `{"status":"error","error":"..."}` |
| `GET /health` | `{"ok":true,"running":N}` |

`reading` は回答のひらがな読みで、会話セッションが無いとき (会話モード以外や接続断)
に機体が jtts で読み上げるために使います。

## 動作確認

```sh
T='<トークン>'
curl -s -X POST -H "Authorization: Bearer $T" -H 'Content-Type: application/json' \
  --data '{"question":"今日の東京の天気を一言で"}' http://localhost:8787/ask
# {"id":"..."} → 数十秒後
curl -s -H "Authorization: Bearer $T" http://localhost:8787/jobs/<id>
```
