#pragma once

// 跨平台文件 I/O 工具。
//
// 核心原理：用 std::filesystem::path 作为路径的"通用适配器"。
//
// 重要：MSVC 的 std::filesystem::path(const std::string&) 始终按系统 ANSI
// 代码页解释，/utf-8 编译选项无法改变此行为。本项目所有路径字符串都是
// UTF-8 编码（源码字面量、GetOpenFileNameW 返回值转换、GLFW drop callback
// 均为 UTF-8），因此在 Windows 上必须先转 UTF-16 再构造 path。
//
// - Windows：utf8_to_wstring() → std::filesystem::path(wstring) → ifstream
// - Linux/macOS：std::filesystem::path(utf8_string) 原生 UTF-8，无转换

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#ifdef _WIN32
// 必须在 windows.h 之前定义 NOMINMAX，避免 min/max 宏污染 std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bmv {

#ifdef _WIN32
// UTF-8 → UTF-16 转换。供 to_path() / _wpopen / CreateProcessW 等 Win32 API 使用。
inline std::wstring utf8_to_wstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                  static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()), &result[0], len);
    return result;
}
#endif

// 将 UTF-8 路径字符串转为 std::filesystem::path。
// Windows: 先转 UTF-16 再构造（path(string) 按 ANSI 解释，会出错）。
// Linux/macOS: 直接构造（原生 UTF-8）。
inline std::filesystem::path to_path(const std::string& utf8_path) {
#ifdef _WIN32
    return std::filesystem::path(utf8_to_wstring(utf8_path));
#else
    return std::filesystem::path(utf8_path);
#endif
}

// 以二进制模式读取整个文件。失败返回空 vector。
inline std::vector<uint8_t> read_file_binary(const std::string& utf8_path) {
    std::ifstream f(to_path(utf8_path), std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize size = f.tellg();
    if (size < 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(data.data()), size);
        if (static_cast<std::streamsize>(data.size()) != f.gcount()) return {};
    }
    return data;
}

// 以二进制模式读取整个文件并作为字符串返回（保留原始字节，不做编码转换）。
// 失败返回空字符串。
inline std::string read_file_text_raw(const std::string& utf8_path) {
    std::ifstream f(to_path(utf8_path), std::ios::binary);
    if (!f) return std::string();
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// 以二进制模式写入整个文件。成功返回 true。
inline bool write_file_binary(const std::string& utf8_path,
                              const void* data, size_t size) {
    std::ofstream f(to_path(utf8_path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size > 0) f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

inline bool write_file_text(const std::string& utf8_path, const std::string& text) {
    return write_file_binary(utf8_path, text.data(), text.size());
}

// 返回文件大小（字节），失败返回 -1。
inline std::streamoff file_size(const std::string& utf8_path) {
    std::ifstream f(to_path(utf8_path), std::ios::binary | std::ios::ate);
    if (!f) return -1;
    return f.tellg();
}

// 判断文件是否存在（跨平台，正确处理 UTF-8 路径）。
inline bool file_exists(const std::string& utf8_path) {
    std::error_code ec;
    return std::filesystem::exists(to_path(utf8_path), ec);
}

} // namespace bmv
