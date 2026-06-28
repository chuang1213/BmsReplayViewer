#pragma once
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bmv {

#ifdef _WIN32
// 使用 Windows API 将 Shift-JIS 转换为 UTF-8
inline std::string shift_jis_to_utf8(const std::string& sjis) {
    if (sjis.empty()) return "";
    
    // Shift-JIS -> UTF-16
    int wlen = MultiByteToWideChar(932, 0, sjis.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return sjis; // 转换失败，返回原字符串
    
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(932, 0, sjis.c_str(), -1, &wstr[0], wlen);
    
    // UTF-16 -> UTF-8
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return sjis;
    
    std::string utf8(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], ulen, nullptr, nullptr);
    
    // 移除末尾的空字符
    if (!utf8.empty() && utf8.back() == '\0') {
        utf8.pop_back();
    }
    
    return utf8;
}
#else
// 非 Windows 平台的简化实现（仅处理 ASCII）
inline std::string shift_jis_to_utf8(const std::string& sjis) {
    // TODO: 使用 iconv 或其他库实现完整转换
    return sjis;
}
#endif

} // namespace bmv
