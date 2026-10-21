// SPDX-FileCopyrightText: 2026 Emilien Etadam
// SPDX-License-Identifier: BSL-1.0
#include "remote_voice.hpp"

#include <cstring>

#include <cJSON.h>
#include <config_service/i18n.hpp>
#include <esp_http_client.h>
#include <esp_log.h>

namespace stackchan::app::remote_voice {

namespace {
constexpr const char* kTag = "remote_voice";
// First block small so sound starts quickly, then larger blocks (fewer
// speaker slot hand-offs).
constexpr std::size_t kFirstChunkSamples = kSampleRate / 5;    // 200 ms
constexpr std::size_t kChunkSamples = kSampleRate * 2 / 5;     // 400 ms
} // namespace

bool active()
{
    return !i18n::is_ja();
}

bool synthesize(const std::string& text,
                const std::function<bool(std::vector<std::int16_t>&& pcm, std::uint32_t rate)>& on_chunk)
{
    const std::string& base = i18n::voice_url();
    if (base.empty() || text.empty()) {
        if (base.empty()) ESP_LOGW(kTag, "no voice URL configured (locale.voice) — staying silent");
        return false;
    }
    const std::string url = base + "/tts/stream";

    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "text", text.c_str());
    char* body_c = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (body_c == nullptr) return false;
    const std::string body = body_c;
    cJSON_free(body_c);

    esp_http_client_config_t cfg{};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 15000; // first audio can take ~1 s on a busy CPU
    cfg.keep_alive_enable = false;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 1024;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) return false;
    esp_http_client_set_header(client, "Content-Type", "application/json");

    bool any = false;
    if (esp_http_client_open(client, static_cast<int>(body.size())) != ESP_OK) {
        ESP_LOGW(kTag, "cannot reach %s", url.c_str());
    } else if (esp_http_client_write(client, body.data(), static_cast<int>(body.size())) !=
               static_cast<int>(body.size())) {
        ESP_LOGW(kTag, "request write failed");
    } else if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGW(kTag, "no response headers");
    } else if (const int status = esp_http_client_get_status_code(client); status != 200) {
        ESP_LOGW(kTag, "HTTP %d from %s", status, url.c_str());
    } else {
        std::vector<std::int16_t> pcm;
        std::size_t want = kFirstChunkSamples;
        pcm.reserve(want);
        char buf[2048];
        bool odd = false;   // a sample split across two reads
        char odd_byte = 0;
        int stalls = 0;
        for (;;) {
            const int n = esp_http_client_read(client, buf, sizeof(buf));
            if (n == -ESP_ERR_HTTP_EAGAIN && ++stalls < 3) continue; // server still synthesising
            if (n < 0) {
                ESP_LOGW(kTag, "read error %d", n);
                break;
            }
            if (n == 0) break; // end of stream
            stalls = 0;
            int i = 0;
            if (odd) {
                const auto lo = static_cast<std::uint8_t>(odd_byte);
                const auto hi = static_cast<std::uint8_t>(buf[0]);
                pcm.push_back(static_cast<std::int16_t>(lo | (hi << 8)));
                odd = false;
                i = 1;
            }
            for (; i + 1 < n; i += 2) {
                const auto lo = static_cast<std::uint8_t>(buf[i]);
                const auto hi = static_cast<std::uint8_t>(buf[i + 1]);
                pcm.push_back(static_cast<std::int16_t>(lo | (hi << 8)));
            }
            if (i < n) {
                odd = true;
                odd_byte = buf[i];
            }
            if (pcm.size() >= want) {
                if (!on_chunk(std::move(pcm), kSampleRate)) {
                    pcm.clear();
                    any = true;
                    break;
                }
                any = true;
                pcm = {};
                want = kChunkSamples;
                pcm.reserve(want);
            }
        }
        if (!pcm.empty()) {
            any = on_chunk(std::move(pcm), kSampleRate) || any;
            any = true;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return any;
}

std::string encode_utf8(std::u32string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char32_t c : s) {
        const auto cp = static_cast<std::uint32_t>(c);
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

} // namespace stackchan::app::remote_voice
