// SPDX-FileCopyrightText: 2026 Emilien Etadam
// SPDX-License-Identifier: BSL-1.0
//
// [EN-FR fork] Language selection for on-device texts, the LLM prompt / tool
// descriptions and the robot's own voice.
//
// The language, time zone and remote voice URL live in one setting
// (DeviceConfig::locale_json, registry id "locale"), parsed once at boot by
// configure() before any task that draws or speaks starts. After that the
// values are read-only, so the getters need no locking.
#pragma once

#include <cstdint>
#include <string>

namespace stackchan::i18n {

enum class Lang : std::uint8_t { Fr = 0, En = 1, Ja = 2 };

// Default POSIX TZ (Europe/Paris) used when the setting has none.
inline constexpr const char* kDefaultTz = "CET-1CEST,M3.5.0,M10.5.0/3";

// Parse DeviceConfig::locale_json. Empty / malformed → French, kDefaultTz,
// no remote voice. Call once at boot.
void configure(const std::string& locale_json);

Lang lang();
// "fr" / "en" / "ja"
const char* lang_code();
// POSIX TZ string for setenv("TZ").
const char* timezone();
// Base URL of the voix-fr service ("http://host:port", no trailing slash),
// empty when unset. Non-Japanese speech goes there; empty = silent.
const std::string& voice_url();

// BLE settings service: true = always started (upstream behaviour); false
// ("ble":"auto", default) = only when no Wi-Fi is configured or Wi-Fi does not
// come up within 30 s. Frees ~40 KB of internal RAM for the camera.
bool ble_always();

// Pick the string for the current language. Arguments are always in the
// order French, English, Japanese so call sites read the same everywhere.
inline const char* tr(const char* fr, const char* en, const char* ja)
{
    switch (lang()) {
    case Lang::Fr: return fr;
    case Lang::En: return en;
    case Lang::Ja: break;
    }
    return ja;
}

inline bool is_ja() { return lang() == Lang::Ja; }

} // namespace stackchan::i18n
