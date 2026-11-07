#include "platform/paths.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

#include "win_utf.hpp"

namespace fx {
namespace platform {
namespace {

std::wstring envWide(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return std::wstring();
    std::wstring value(size_t(size), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
    if (written == 0) return std::wstring();
    value.resize(size_t(written));
    return value;
}

void ensureDir(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path), ec);
}

std::string appDataDir(const wchar_t* envName, const wchar_t* fallbackRelative) {
    std::wstring base = envWide(envName);
    if (base.empty()) {
        const std::wstring profile = envWide(L"USERPROFILE");
        if (!profile.empty()) base = profile + L"\\" + fallbackRelative;
    }
    if (base.empty()) base = L".";
    const std::filesystem::path dir = std::filesystem::path(base) / L"fxpet";
    return dir.u8string();
}

}  // namespace

std::string joinPath(const std::string& a, const std::string& b) {
    return (std::filesystem::u8path(a) / std::filesystem::u8path(b)).u8string();
}

std::string exeDir() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
        if (n == 0) return ".";
        if (n < buffer.size()) {
            buffer.resize(size_t(n));
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    const std::filesystem::path parent = std::filesystem::path(buffer).parent_path();
    return parent.empty() ? std::string(".") : parent.u8string();
}

std::string homeDir() {
    const std::wstring profile = envWide(L"USERPROFILE");
    return profile.empty() ? std::string(".") : wideToUtf8(profile);
}

std::string configHome() {
    const std::string dir = appDataDir(L"APPDATA", L"AppData\\Roaming");
    ensureDir(dir);
    return dir;
}

std::string stateHome() {
    const std::string dir = appDataDir(L"LOCALAPPDATA", L"AppData\\Local");
    ensureDir(dir);
    return dir;
}

std::vector<std::string> cubismLibraryCandidates() {
    std::vector<std::string> out;
    const char* explicitPath = std::getenv("FXPET_CUBISM_CORE");
    if (explicitPath && *explicitPath) out.push_back(explicitPath);

    const std::string base = exeDir();
    out.push_back(joinPath(joinPath(base, "lib"), "Live2DCubismCore.dll"));
    out.push_back(joinPath(base, "Live2DCubismCore.dll"));
    out.push_back("Live2DCubismCore.dll");
    return out;
}

bool primaryScreenSize(int& w, int& h) {
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    return w > 0 && h > 0;
}

void initTiming() { timeBeginPeriod(1); }
void shutdownTiming() { timeEndPeriod(1); }

void attachConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole()) return;
    }
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
}

}  // namespace platform
}  // namespace fx
