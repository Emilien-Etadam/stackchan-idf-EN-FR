// SPDX-FileCopyrightText: 2026 Emilien Etadam
// SPDX-License-Identifier: BSL-1.0
//
// [EN-FR fork] The robot's own voice outside Japanese.
//
// jtts / sanoTTS only read kana, so in French or English the phrases the robot
// says by itself (timer alerts, reminders, babble, test button) are fetched
// from the voix-fr service (POST <voice>/tts/stream {"text": ...}, answer =
// raw PCM s16le mono 24 kHz streamed as it is synthesised) and played through
// the same ChunkPlayer path as jtts. No URL configured or server unreachable:
// nothing is played (the balloon text still shows).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace stackchan::app::remote_voice {

inline constexpr std::uint32_t kSampleRate = 24'000;

// True when phrases must go to voix-fr instead of jtts (language is not
// Japanese). The URL may still be empty: then synthesize() fails and the
// robot stays silent.
bool active();

// Stream `text` (UTF-8) from voix-fr. `on_chunk` receives PCM blocks of a few
// hundred ms at kSampleRate, in order; returning false stops the transfer.
// Blocking; call from a synthesis task. Returns true when at least one chunk
// was delivered.
bool synthesize(const std::string& text,
                const std::function<bool(std::vector<std::int16_t>&& pcm, std::uint32_t rate)>& on_chunk);

// UTF-32 → UTF-8 (the jtts call paths carry the text as std::u32string).
std::string encode_utf8(std::u32string_view s);

} // namespace stackchan::app::remote_voice
