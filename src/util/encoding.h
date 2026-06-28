#pragma once
#include <string>
#include <iterator>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

#include "fs_util.h"

namespace bmv {

// ---- 文件对话框专用 UTF-16 <-> UTF-8 转换 ----
//
// 注意：普通文件 I/O 已统一使用 fs_util.h（基于 std::filesystem::path），
//      无需手工 utf8_to_wstring。此处仅保留 wstring_to_utf8，供
//      GetOpenFileNameW 等 Win32 API 返回值转换使用。

#ifdef _WIN32
inline std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                                  static_cast<int>(wstr.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                        static_cast<int>(wstr.size()),
                        &result[0], len, nullptr, nullptr);
    return result;
}
#endif

// ---- UTF-8 校验（无外部依赖） ----
inline bool is_valid_utf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        int len;
        if (c <= 0x7F)               len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return false;
        if (i + static_cast<size_t>(len) > s.size()) return false;
        for (int j = 1; j < len; ++j) {
            if ((static_cast<uint8_t>(s[i + j]) & 0xC0) != 0x80)
                return false;
        }
        i += static_cast<size_t>(len);
    }
    return true;
}

#ifdef _WIN32
// Shift-JIS (CP932) → UTF-8，使用显式长度（不依赖空终止符，二进制安全）
inline std::string shift_jis_to_utf8(const std::string& sjis) {
    if (sjis.empty()) return "";
    int slen = static_cast<int>(sjis.size());
    int wlen = MultiByteToWideChar(932, 0, sjis.data(), slen, nullptr, 0);
    if (wlen <= 0) return sjis;
    std::wstring wstr(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(932, 0, sjis.data(), slen, &wstr[0], wlen);

    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                                   nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return sjis;
    std::string utf8(static_cast<size_t>(ulen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                        &utf8[0], ulen, nullptr, nullptr);
    return utf8;
}

// UTF-16 LE → UTF-8
inline std::string utf16le_to_utf8(const char* data, size_t bytes) {
    if (bytes < 2) return "";
    int wlen = static_cast<int>(bytes / 2);
    std::wstring wstr(reinterpret_cast<const wchar_t*>(data),
                      static_cast<size_t>(wlen));
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                                   nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return "";
    std::string utf8(static_cast<size_t>(ulen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                        &utf8[0], ulen, nullptr, nullptr);
    return utf8;
}
#else
// 非 Windows 平台：暂未接入 iconv，原样返回
inline std::string shift_jis_to_utf8(const std::string& sjis) { return sjis; }
inline std::string utf16le_to_utf8(const char*, size_t) { return ""; }
#endif

// 读取文本文件并返回 UTF-8 内容。
// 检测顺序：UTF-8 BOM → UTF-16 LE BOM → UTF-16 BE BOM → UTF-8 有效性 → 回退 Shift-JIS (CP932)
inline std::string read_file_as_utf8(const std::string& filepath) {
    std::string raw = read_file_text_raw(filepath);
    if (raw.empty()) return "";

    // UTF-8 BOM
    if (raw.size() >= 3 &&
        static_cast<uint8_t>(raw[0]) == 0xEF &&
        static_cast<uint8_t>(raw[1]) == 0xBB &&
        static_cast<uint8_t>(raw[2]) == 0xBF) {
        return raw.substr(3);
    }
    // UTF-16 LE BOM
    if (raw.size() >= 2 &&
        static_cast<uint8_t>(raw[0]) == 0xFF &&
        static_cast<uint8_t>(raw[1]) == 0xFE) {
        return utf16le_to_utf8(raw.data() + 2, raw.size() - 2);
    }
    // UTF-16 BE BOM → 字节交换后转 LE
    if (raw.size() >= 2 &&
        static_cast<uint8_t>(raw[0]) == 0xFE &&
        static_cast<uint8_t>(raw[1]) == 0xFF) {
        std::string swapped;
        swapped.reserve(raw.size() - 2);
        for (size_t i = 2; i + 1 < raw.size(); i += 2) {
            swapped.push_back(raw[i + 1]);
            swapped.push_back(raw[i]);
        }
        return utf16le_to_utf8(swapped.data(), swapped.size());
    }
    // 合法 UTF-8（含纯 ASCII）
    if (is_valid_utf8(raw)) return raw;
    // 回退：假定 Shift-JIS (CP932)——日文 BMS 最常见的编码
    return shift_jis_to_utf8(raw);
}

} // namespace bmv
