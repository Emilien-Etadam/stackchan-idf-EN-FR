// SPDX-FileCopyrightText: 2026 Emilien Etadam
// SPDX-License-Identifier: BSL-1.0
#include <config_service/i18n.hpp>

#include <cstring>

#include <cJSON.h>
#include <esp_log.h>

namespace stackchan::i18n {

namespace {
constexpr const char* kTag = "i18n";
Lang g_lang = Lang::Fr;
std::string g_tz = kDefaultTz;
std::string g_voice_url;
} // namespace

void configure(const std::string& locale_json)
{
    g_lang = Lang::Fr;
    g_tz = kDefaultTz;
    g_voice_url.clear();
    if (!locale_json.empty()) {
        cJSON* root = cJSON_Parse(locale_json.c_str());
        if (root == nullptr || !cJSON_IsObject(root)) {
            ESP_LOGW(kTag, "locale: JSON parse failed, using defaults");
        } else {
            const cJSON* l = cJSON_GetObjectItemCaseSensitive(root, "lang");
            if (cJSON_IsString(l) && l->valuestring != nullptr) {
                if (std::strcmp(l->valuestring, "en") == 0) g_lang = Lang::En;
                else if (std::strcmp(l->valuestring, "ja") == 0) g_lang = Lang::Ja;
            }
            const cJSON* tz = cJSON_GetObjectItemCaseSensitive(root, "tz");
            if (cJSON_IsString(tz) && tz->valuestring != nullptr && tz->valuestring[0] != '\0') {
                g_tz = tz->valuestring;
            }
            const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, "voice");
            if (cJSON_IsString(v) && v->valuestring != nullptr) {
                g_voice_url = v->valuestring;
                while (!g_voice_url.empty() && g_voice_url.back() == '/') g_voice_url.pop_back();
            }
        }
        cJSON_Delete(root);
    }
    ESP_LOGI(kTag, "lang=%s tz=%s voice=%s", lang_code(), g_tz.c_str(),
             g_voice_url.empty() ? "(none)" : g_voice_url.c_str());
}

Lang lang() { return g_lang; }

const char* lang_code()
{
    switch (g_lang) {
    case Lang::Fr: return "fr";
    case Lang::En: return "en";
    case Lang::Ja: break;
    }
    return "ja";
}

const char* timezone() { return g_tz.c_str(); }

const std::string& voice_url() { return g_voice_url; }

} // namespace stackchan::i18n
