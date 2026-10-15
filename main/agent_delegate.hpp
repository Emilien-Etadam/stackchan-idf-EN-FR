// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "conversation/conversation_service.hpp"

namespace stackchan::app::agent_delegate {

// Delegation of a question to an agent running on a host machine (the
// tools/stackchan-agent server: `claude -p` behind a tiny HTTP job API).
//
//   POST {url}/ask   {"question": "..."}      → 202 {"id": "..."}
//   GET  {url}/jobs/{id}                      → {"status":"running"} |
//        {"status":"done","answer":"...","reading":"..."} | {"status":"error","error":"..."}
//
// The conversation model calls the `ask_agent` tool, which only *starts* the
// job (an agent run takes 10–90 s; realtime tool calls can't wait that
// long). The conversation task then polls while idle and, once the answer
// arrives, feeds it back into the live session as a text turn so the model
// voices it in its own words — or, with no session, hands it to demo_loop
// via the reminder slot (balloon + jtts reading).
//
// Plain HTTP on purpose: the host is on the same LAN (or reachable through
// Tailscale, which encrypts the hop), and a TLS session would cost ~40 KiB
// of the internal RAM the conversation already fights for. The bearer token
// keeps strangers on the LAN from running the agent.
struct Result {
    bool ok = false;
    std::string answer;   // display / text turn
    std::string reading;  // hiragana, for jtts when no session is live
    std::string error;    // when !ok
};

class Delegate {
public:
    Delegate(std::string base_url, std::string token);

    bool configured() const noexcept { return !base_url_.empty(); }
    bool busy() const noexcept { return !job_id_.empty(); }
    const std::string& question() const noexcept { return question_; }

    // Kick off a job. Returns the tool-result JSON for the model.
    std::string start(const std::string& arguments_json);

    // Poll the outstanding job (rate-limited internally; cheap to call every
    // loop iteration). Returns a Result once the job finished or failed.
    std::optional<Result> poll(std::uint32_t now_ms);

    static conversation::ToolDefinition tool_definition();

private:
    std::optional<std::string> http(const char* method, const std::string& path, const std::string& body,
                                    int& status_out);

    std::string base_url_;
    std::string token_;
    std::string job_id_;
    std::string question_;
    std::uint32_t started_ms_{0};
    std::uint32_t next_poll_ms_{0};
};

} // namespace stackchan::app::agent_delegate
