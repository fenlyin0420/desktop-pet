#include "platform/paths.hpp"

#include <windows.h>

#include <filesystem>
#include <system_error>
#include <vector>

#include "win_utf.hpp"

namespace fx {
namespace platform {

std::string findUiFont() {
    wchar_t windowsDir[MAX_PATH] = {0};
    const UINT n = GetWindowsDirectoryW(windowsDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string();

    const std::filesystem::path fontDir = std::filesystem::path(windowsDir) / L"Fonts";
    const wchar_t* candidates[] = {
        L"msyh.ttc",      // 微软雅黑
        L"msyhbd.ttc",
        L"simhei.ttf",    // 黑体
        L"simsun.ttc",    // 宋体
        L"segoeui.ttf",   // 无中文字形，但至少能显示 ASCII
        L"arial.ttf",
    };
    for (const wchar_t* name : candidates) {
        std::error_code ec;
        const std::filesystem::path path = fontDir / name;
        if (std::filesystem::is_regular_file(path, ec)) return path.u8string();
    }
    return std::string();
}

}  // namespace platform
}  // namespace fx
