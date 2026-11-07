// UTF-8 <-> UTF-16（Windows API 边界）
#pragma once

#include <windows.h>

#include <string>

namespace fx {
namespace platform {

inline std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    if (count <= 0) return std::wstring();
    std::wstring wide(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), count);
    return wide;
}

inline std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return std::string();
    std::string utf8(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), count, nullptr, nullptr);
    return utf8;
}

}  // namespace platform
}  // namespace fx
