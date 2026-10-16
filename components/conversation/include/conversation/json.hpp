// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#pragma once

// ArduinoJson の唯一の取り込み口。設定マクロは全翻訳単位で揃っている必要が
// あるので、<ArduinoJson.h> を直接 include せず必ずこのヘッダを使う。
//
// cJSON からの移行先 (2026-10-16)。cJSON はノード毎に malloc し、文字列化でも
// 全文を一度ヒープに複製するため、XiaoZhi の MCP tools/list のような 10 KiB 級の
// メッセージで内部 RAM を食い潰して TLS 送信が失敗した。ArduinoJson は 1 つの
// ドキュメントがアリーナ 1 つに収まり、アロケータを差し替えられるので、ここでは
// PSRAM 優先 (枯渇時のみ内部 RAM) のアロケータを既定にする。
// JSON と MessagePack を同じ API で読み書きできる (serializeMsgPack 等)。

#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#define ARDUINOJSON_ENABLE_STD_STRING 1
#define ARDUINOJSON_ENABLE_STD_STREAM 0
#define ARDUINOJSON_ENABLE_STRING_VIEW 1
// 既定 (2 B) でも 64 KiB まで扱えるが、system_prompt や長い応答に備えて余裕を持たせる。
#define ARDUINOJSON_STRING_LENGTH_SIZE 4

#include <ArduinoJson.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <esp_heap_caps.h>

namespace stackchan::conversation {

// PSRAM 優先アロケータ。JsonDocument のノード / 文字列プールがここに乗る。
class PsramJsonAllocator final : public ArduinoJson::Allocator {
public:
    static PsramJsonAllocator& instance()
    {
        static PsramJsonAllocator inst;
        return inst;
    }

    void* allocate(std::size_t size) override
    {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p != nullptr ? p : std::malloc(size);
    }

    void deallocate(void* p) override { std::free(p); }

    void* reallocate(void* p, std::size_t new_size) override
    {
        // heap_caps_realloc はブロックの元ヒープを維持するので、PSRAM に取れた
        // ものは PSRAM のまま伸びる。内部 RAM にフォールバックしたものも同様。
        return heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
    }
};

// PSRAM アリーナで JsonDocument を作る。使い方は素の JsonDocument と同じ。
inline ArduinoJson::JsonDocument make_json_doc()
{
    return ArduinoJson::JsonDocument{&PsramJsonAllocator::instance()};
}

// 直列化して std::string で返す (結果文字列は呼び出し側の std::string = 内部 RAM。
// ツール結果など数百バイトの用途向け)。
inline std::string json_to_string(ArduinoJson::JsonVariantConst v)
{
    std::string out;
    ArduinoJson::serializeJson(v, out);
    return out;
}

// 直列化して PSRAM 上のバッファ (NUL 終端付き) で返す。送信用の大きなメッセージ向け。
struct PsramBytes {
    char* data = nullptr;
    std::size_t len = 0;
    PsramBytes() = default;
    PsramBytes(const PsramBytes&) = delete;
    PsramBytes& operator=(const PsramBytes&) = delete;
    PsramBytes(PsramBytes&& o) noexcept : data{o.data}, len{o.len} { o.data = nullptr; o.len = 0; }
    ~PsramBytes() { std::free(data); }
};

inline PsramBytes json_to_psram(ArduinoJson::JsonVariantConst v)
{
    PsramBytes b;
    const std::size_t n = ArduinoJson::measureJson(v);
    b.data = static_cast<char*>(PsramJsonAllocator::instance().allocate(n + 1));
    if (b.data == nullptr) return b;
    b.len = ArduinoJson::serializeJson(v, b.data, n + 1);
    return b;
}

// JSON 文字列 1 個 ("..." 付き・エスケープ済み) を返す。手組みの JSON に文字列を埋め込む用。
inline std::string json_quote(std::string_view s)
{
    auto doc = make_json_doc();
    doc.set(s);
    return json_to_string(doc);
}

} // namespace stackchan::conversation
