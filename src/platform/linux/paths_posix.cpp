#include "platform/paths.hpp"

#include <X11/Xlib.h>
#include <glob.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fx {
namespace platform {
namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

void ensureDir(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path), ec);
}

}  // namespace

std::string joinPath(const std::string& a, const std::string& b) {
    return (std::filesystem::u8path(a) / std::filesystem::u8path(b)).u8string();
}

std::string exeDir() {
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    const std::string p(buf);
    const std::filesystem::path parent = std::filesystem::u8path(p).parent_path();
    return parent.empty() ? std::string(".") : parent.u8string();
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    return (h && *h) ? std::string(h) : std::string(".");
}

std::string configHome() {
    const std::string dir = envOr("XDG_CONFIG_HOME", homeDir() + "/.config") + "/fxpet";
    ensureDir(dir);
    return dir;
}

std::string stateHome() {
    const std::string dir = envOr("XDG_STATE_HOME", homeDir() + "/.local/state") + "/fxpet";
    ensureDir(dir);
    return dir;
}

std::vector<std::string> cubismLibraryCandidates() {
    std::vector<std::string> out;
    const std::string explicitPath = envOr("FXPET_CUBISM_CORE", "");
    if (!explicitPath.empty()) out.push_back(explicitPath);

    std::vector<std::string> bases;
    const std::string envBase = envOr("FXPET_HOME", "");
    if (!envBase.empty()) bases.push_back(envBase);
    bases.push_back(exeDir());
    bases.push_back(".");
    for (const std::string& base : bases) {
        out.push_back(base + "/lib/libLive2DCubismCore.so");
        out.push_back(base + "/lib/live2d/_live2d.so");
        out.push_back(base + "/lib/live2d_pkg/live2d/_live2d.so");
    }
    out.push_back("libLive2DCubismCore.so");

    // 已安装的 live2d-py 轮子里带着官方 Core（仅在本机复用，不随仓库分发）
    const char* home = std::getenv("HOME");
    std::vector<std::string> patterns;
    if (home && *home) {
        patterns.push_back(std::string(home) + "/.local/lib/python3*/site-packages/live2d/_live2d.so");
        patterns.push_back(std::string(home) + "/.local/lib/python3*/dist-packages/live2d/_live2d.so");
        patterns.push_back(std::string(home) + "/.venv/lib/python3*/site-packages/live2d/_live2d.so");
    }
    patterns.push_back("/usr/lib/python3*/site-packages/live2d/_live2d.so");
    patterns.push_back("/usr/lib/python3/dist-packages/live2d/_live2d.so");
    patterns.push_back("/usr/local/lib/python3*/dist-packages/live2d/_live2d.so");
    patterns.push_back("/usr/local/lib/python3*/site-packages/live2d/_live2d.so");

    for (const std::string& pat : patterns) {
        glob_t g{};
        if (glob(pat.c_str(), 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) out.push_back(g.gl_pathv[i]);
        }
        globfree(&g);
    }
    out.push_back("libLive2DCubismCore.so");
    return out;
}

bool primaryScreenSize(int& w, int& h) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;
    const int screen = DefaultScreen(dpy);
    w = DisplayWidth(dpy, screen);
    h = DisplayHeight(dpy, screen);
    XCloseDisplay(dpy);
    return w > 0 && h > 0;
}

void initTiming() {}
void shutdownTiming() {}
void attachConsole() {}

}  // namespace platform
}  // namespace fx
