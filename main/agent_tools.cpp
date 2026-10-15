// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#include "agent_tools.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "utf8.hpp"

namespace stackchan::app::agent_tools {

namespace conv = stackchan::conversation;

namespace {

constexpr const char* kTag = "agent-tools";

// ---- memory store: NVS namespace "memory" on the 1 MB "storage" partition ---
// (the main "nvs" partition is 16 KiB and essentially full). One JSON blob
// {"items":[{"k":...,"v":...,"t":<unix>}]}; small enough to rewrite whole.
constexpr const char* kPart = "storage";
constexpr const char* kNs = "memory";
constexpr const char* kKey = "items";
constexpr std::size_t kMaxItems = 32;
constexpr std::size_t kMaxKeyBytes = 48;
constexpr std::size_t kMaxValueBytes = 500;

bool ensure_partition()
{
    static int state = 0; // 0 unknown, 1 ok, -1 absent
    if (state != 0) return state == 1;
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kPart);
    if (part == nullptr) {
        state = -1;
        return false;
    }
    // Another user (avatar_vm / voice_db) may already have initialised it.
    const esp_err_t err = nvs_flash_init_partition_ptr(part);
    state = (err == ESP_OK || err == ESP_ERR_INVALID_STATE) ? 1 : -1;
    if (state != 1) ESP_LOGW(kTag, "nvs init on \"%s\" failed: %s", kPart, esp_err_to_name(err));
    return state == 1;
}

// Loaded items as a cJSON array (caller owns). Empty array when nothing stored.
cJSON* load_items()
{
    cJSON* arr = nullptr;
    nvs_handle_t h;
    if (ensure_partition() && nvs_open_from_partition(kPart, kNs, NVS_READONLY, &h) == ESP_OK) {
        std::size_t len = 0;
        if (nvs_get_blob(h, kKey, nullptr, &len) == ESP_OK && len > 0 && len < 64 * 1024) {
            std::string buf(len, '\0');
            if (nvs_get_blob(h, kKey, buf.data(), &len) == ESP_OK) {
                cJSON* root = cJSON_Parse(buf.c_str());
                if (root != nullptr) {
                    arr = cJSON_DetachItemFromObjectCaseSensitive(root, "items");
                    cJSON_Delete(root);
                }
            }
        }
        nvs_close(h);
    }
    if (arr == nullptr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        arr = cJSON_CreateArray();
    }
    return arr;
}

bool save_items(cJSON* arr) // takes ownership of arr
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "items", arr);
    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) return false;
    bool ok = false;
    nvs_handle_t h;
    if (ensure_partition() && nvs_open_from_partition(kPart, kNs, NVS_READWRITE, &h) == ESP_OK) {
        ok = nvs_set_blob(h, kKey, text, std::strlen(text) + 1) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    cJSON_free(text);
    return ok;
}

std::string json_string(const char* s)
{
    cJSON* node = cJSON_CreateString(s);
    char* q = cJSON_PrintUnformatted(node);
    cJSON_Delete(node);
    std::string out = q ? q : "\"\"";
    cJSON_free(q);
    return out;
}

std::string error_json(const char* msg)
{
    return std::string(R"({"ok":false,"error":)") + json_string(msg) + "}";
}

// ---- get_time ---------------------------------------------------------------

std::string tool_get_time()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2020) {
        return R"json({"ok":false,"error":"時計がまだ合っていません (Wi-Fi 未接続か NTP 未同期)"})json";
    }
    static const char* kWeekday[] = {"日", "月", "火", "水", "木", "金", "土"};
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  R"({"ok":true,"date":"%04d-%02d-%02d","time":"%02d:%02d","weekday":"%s","timezone":"JST"})",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, kWeekday[tm.tm_wday]);
    return buf;
}

// ---- get_status -------------------------------------------------------------

std::string tool_get_status(SharedState& state)
{
    std::string out = R"({"ok":true)";
    const auto& px = state.proximity;
    if (px.available.load(std::memory_order_relaxed)) {
        char b[96];
        std::snprintf(b, sizeof(b), R"(,"proximity":{"hand_near":%s,"raw":%u})",
                      px.near.load(std::memory_order_relaxed) ? "true" : "false",
                      static_cast<unsigned>(px.raw.load(std::memory_order_relaxed)));
        out += b;
    }
    const int pct = state.battery.pct.load(std::memory_order_relaxed);
    const int mv = state.battery.mv.load(std::memory_order_relaxed);
    if (pct >= 0 || mv >= 0) {
        char b[96];
        std::snprintf(b, sizeof(b), R"(,"battery":{"percent":%d,"millivolt":%d,"milliamp":%d})", pct, mv,
                      static_cast<int>(state.battery.ma.load(std::memory_order_relaxed)));
        out += b;
    }
    {
        char b[64];
        std::snprintf(b, sizeof(b), R"(,"uptime_min":%u)",
                      static_cast<unsigned>(esp_timer_get_time() / 60'000'000));
        out += b;
    }
    if (state.lt.active.load(std::memory_order_relaxed)) {
        char b[64];
        std::snprintf(b, sizeof(b), R"(,"lt_timer_remaining_s":%d)",
                      static_cast<int>(state.lt.remaining_s.load(std::memory_order_relaxed)));
        out += b;
    }
    out += "}";
    return out;
}

// ---- remember / recall / forget ----------------------------------------------

std::string tool_remember(const std::string& args)
{
    cJSON* root = cJSON_Parse(args.c_str());
    if (root == nullptr) return error_json("bad arguments");
    const cJSON* k = cJSON_GetObjectItemCaseSensitive(root, "key");
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(root, "text");
    std::string key = cJSON_IsString(k) && k->valuestring ? k->valuestring : "";
    std::string text = cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
    cJSON_Delete(root);
    if (key.empty() || text.empty()) return error_json("key と text が必要です");
    if (key.size() > kMaxKeyBytes) key.resize(kMaxKeyBytes);
    if (text.size() > kMaxValueBytes) return error_json("text が長すぎます (500 バイトまで)");

    cJSON* arr = load_items();
    // Replace an existing key in place; otherwise append (dropping the
    // oldest when full).
    cJSON* item = nullptr;
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, arr) {
        const cJSON* ik = cJSON_GetObjectItemCaseSensitive(it, "k");
        if (cJSON_IsString(ik) && key == ik->valuestring) { item = it; break; }
    }
    if (item == nullptr) {
        while (cJSON_GetArraySize(arr) >= static_cast<int>(kMaxItems)) cJSON_DeleteItemFromArray(arr, 0);
        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "k", key.c_str());
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_DeleteItemFromObjectCaseSensitive(item, "v");
    cJSON_AddStringToObject(item, "v", text.c_str());
    cJSON_DeleteItemFromObjectCaseSensitive(item, "t");
    cJSON_AddNumberToObject(item, "t", static_cast<double>(std::time(nullptr)));
    const int n = cJSON_GetArraySize(arr);
    if (!save_items(arr)) return error_json("保存に失敗しました");
    ESP_LOGI(kTag, "remember: %s (%d items)", key.c_str(), n);
    char b[64];
    std::snprintf(b, sizeof(b), R"({"ok":true,"stored":%d})", n);
    return b;
}

std::string tool_recall(const std::string& args)
{
    std::string key;
    if (cJSON* root = cJSON_Parse(args.c_str()); root != nullptr) {
        const cJSON* k = cJSON_GetObjectItemCaseSensitive(root, "key");
        if (cJSON_IsString(k) && k->valuestring) key = k->valuestring;
        cJSON_Delete(root);
    }
    cJSON* arr = load_items();
    cJSON* out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON* list = cJSON_AddArrayToObject(out, "memories");
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, arr) {
        const cJSON* ik = cJSON_GetObjectItemCaseSensitive(it, "k");
        const cJSON* iv = cJSON_GetObjectItemCaseSensitive(it, "v");
        if (!cJSON_IsString(ik) || !cJSON_IsString(iv)) continue;
        if (!key.empty() && key != ik->valuestring) continue;
        cJSON* e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "key", ik->valuestring);
        cJSON_AddStringToObject(e, "text", iv->valuestring);
        cJSON_AddItemToArray(list, e);
    }
    cJSON_Delete(arr);
    char* text = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    std::string result = text ? text : R"({"ok":false,"error":"oom"})";
    cJSON_free(text);
    return result;
}

std::string tool_forget(const std::string& args)
{
    cJSON* root = cJSON_Parse(args.c_str());
    if (root == nullptr) return error_json("bad arguments");
    const cJSON* k = cJSON_GetObjectItemCaseSensitive(root, "key");
    std::string key = cJSON_IsString(k) && k->valuestring ? k->valuestring : "";
    cJSON_Delete(root);
    if (key.empty()) return error_json("key が必要です");
    cJSON* arr = load_items();
    int removed = 0;
    for (int i = cJSON_GetArraySize(arr) - 1; i >= 0; --i) {
        const cJSON* ik = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(arr, i), "k");
        if (cJSON_IsString(ik) && key == ik->valuestring) { cJSON_DeleteItemFromArray(arr, i); ++removed; }
    }
    if (!save_items(arr)) return error_json("保存に失敗しました");
    char b[64];
    std::snprintf(b, sizeof(b), R"({"ok":true,"removed":%d})", removed);
    return b;
}

// ---- set_timer --------------------------------------------------------------

std::string tool_set_timer(const std::string& args, SharedState& state)
{
    cJSON* root = cJSON_Parse(args.c_str());
    if (root == nullptr) return error_json("bad arguments");
    const cJSON* s = cJSON_GetObjectItemCaseSensitive(root, "seconds");
    const cJSON* d = cJSON_GetObjectItemCaseSensitive(root, "message");
    const cJSON* r = cJSON_GetObjectItemCaseSensitive(root, "reading");
    const double seconds = cJSON_IsNumber(s) ? s->valuedouble : -1;
    std::string display = cJSON_IsString(d) && d->valuestring ? d->valuestring : "";
    std::string reading = cJSON_IsString(r) && r->valuestring ? r->valuestring : "";
    cJSON_Delete(root);
    if (seconds <= 0 || seconds > 12 * 3600) return error_json("seconds は 1〜43200 の範囲で指定してください");
    if (display.empty()) display = "じかんだよ";
    if (reading.empty()) reading = display;
    if (display.size() > 120) display.resize(120);
    if (reading.size() > 120) reading.resize(120);
    const std::uint32_t now_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    const std::uint32_t due_ms = now_ms + static_cast<std::uint32_t>(seconds * 1000.0);
    state.set_reminder(due_ms, display, reading);
    ESP_LOGI(kTag, "set_timer: %u s -> \"%s\"", static_cast<unsigned>(seconds), display.c_str());
    char b[64];
    std::snprintf(b, sizeof(b), R"({"ok":true,"fires_in_s":%u})", static_cast<unsigned>(seconds));
    return b;
}

} // namespace

std::vector<conv::ToolDefinition> definitions()
{
    return {
        conv::ToolDefinition{
            .name = "get_time",
            .description = "現在の日付・時刻・曜日 (日本時間) を取得する。時間や日付を聞かれたら必ずこれで確認する。",
            .parameters_json = R"({"type":"object","properties":{}})",
        },
        conv::ToolDefinition{
            .name = "get_status",
            .description =
                "スタックチャン自身の状態を取得する: 近接センサー (手が近くにあるか)、バッテリー残量、"
                "起動からの時間、LT タイマーの残り。体調や電池、近くに何かあるか聞かれたときに使う。",
            .parameters_json = R"({"type":"object","properties":{}})",
        },
        conv::ToolDefinition{
            .name = "remember",
            .description =
                "ユーザーに頼まれたことや大事な情報を、電源を切っても残るメモとして保存する。"
                "key は短い見出し (例: \"ユーザーの名前\")、text は内容。同じ key は上書き。",
            .parameters_json = R"({"type":"object","properties":{"key":{"type":"string","maxLength":48},)"
                               R"("text":{"type":"string","maxLength":500}},"required":["key","text"]})",
        },
        conv::ToolDefinition{
            .name = "recall",
            .description = "保存してあるメモを読み出す。key を省略すると全部返す。以前の話や約束を聞かれたら使う。",
            .parameters_json = R"({"type":"object","properties":{"key":{"type":"string"}}})",
        },
        conv::ToolDefinition{
            .name = "forget",
            .description = "保存してあるメモを key を指定して消す。",
            .parameters_json = R"({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})",
        },
        conv::ToolDefinition{
            .name = "set_timer",
            .description =
                "指定秒数後にスタックチャンが自分から声をかける (タイマー / リマインダー)。"
                "message は画面に出す文、reading はその読み (ひらがな・カタカナのみ、漢字不可)。"
                "設定できるのは 1 件だけで、再起動すると消える。",
            .parameters_json =
                R"({"type":"object","properties":{"seconds":{"type":"integer","minimum":1,"maximum":43200},)"
                R"("message":{"type":"string","maxLength":60},"reading":{"type":"string","maxLength":80}},)"
                R"("required":["seconds","message","reading"]})",
        },
    };
}

const char* guidance()
{
    return "\n\n[ツール] 時刻や日付を聞かれたら get_time、電池や近くの様子を聞かれたら get_status を使って"
           "確かめてから答えてください (推測で答えない)。覚えておいてと言われたら remember、"
           "前に話したことを聞かれたら recall を使います。「N 分後に教えて」は set_timer を使います "
           "(reading はひらがなで)。ツールは必ず関数呼び出し (function calling) の仕組みで呼び、"
           "「tool_code」や関数名を文章・音声として話さないでください。ツールの結果は要点だけ短く話してください。";
}

std::optional<std::string> dispatch(const std::string& name, const std::string& args, SharedState& state)
{
    if (name == "get_time") return tool_get_time();
    if (name == "get_status") return tool_get_status(state);
    if (name == "remember") return tool_remember(args);
    if (name == "recall") return tool_recall(args);
    if (name == "forget") return tool_forget(args);
    if (name == "set_timer") return tool_set_timer(args, state);
    return std::nullopt;
}

} // namespace stackchan::app::agent_tools
