// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#pragma once

// "Agent" tools for the realtime conversation: tools that return information
// to the LLM (time, device status, memory) or schedule something on the device
// (a reminder), as opposed to the body-only tools in conversation_task.cpp.
// Everything here completes on the conversation task without blocking on the
// network; nothing talks to the outside world.

#include <optional>
#include <string>
#include <vector>

#include <conversation/conversation_service.hpp>

#include "shared_state.hpp"

namespace stackchan::app::agent_tools {

// Tool declarations to append to ConversationConfig::tools.
std::vector<conversation::ToolDefinition> definitions();

// Usage guidance appended to the system prompt (built-in or user-set) so the
// model knows when to reach for these tools and how to phrase the results.
const char* guidance();

// Run one tool. std::nullopt → not one of ours (caller tries its own tools).
// Returns the JSON result string for submit_tool_result.
std::optional<std::string> dispatch(const std::string& name, const std::string& arguments_json,
                                    SharedState& state);

} // namespace stackchan::app::agent_tools
