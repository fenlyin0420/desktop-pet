#include "pointer.hpp"

#include <X11/Xlib.h>
#include <X11/Xresource.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

#include "config.hpp"

namespace fx {
namespace {

std::mutex gMutex;
// 用浮点累加：HiDPI 下 dx/gScale 常是小数（如 1/2），整数四舍五入会单向漂移
double gXf = 0.0;
double gYf = 0.0;
bool gValid = false;   // 是否收到过真实位移/校正（种子不算）
bool gTracking = false;
int gScreenW = 0;
int gScreenH = 0;
float gScale = 1.0f;
int gResyncCount = 0;
// 上一次从 X 读到根坐标；XWayland 只在指针位于 X11 窗口上时才更新它，
// 所以“值发生变化”本身就是一次可信时刻的信号
int gLastXPointer = std::numeric_limits<int>::min();
int gLastYPointer = std::numeric_limits<int>::min();
bool gSeedNextPoll = false;  // 刚开启跟踪：下一次轮询只记下 X 值当基准，不校正
// 是否见过“可信的指针真值”（窗口内事件 / evdev 位移 / X 指针变化）。
// Wayland 启动时只有过期的种子，靠它避免把幽灵位置当真。
bool gTruthSeen = false;

float readXftDpi(Display* dpy, bool& fromResources) {
    fromResources = false;
    if (!dpy) return 96.0f;
    const char* resources = XResourceManagerString(dpy);
    if (!resources) return 96.0f;
    XrmInitialize();
    XrmDatabase db = XrmGetStringDatabase(resources);
    if (!db) return 96.0f;
    float dpi = 96.0f;
    char* type = nullptr;
    XrmValue value{};
    if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) {
        const float parsed = float(std::atof(value.addr));
        if (parsed >= 24.0f && parsed <= 768.0f) {
            dpi = parsed;
            fromResources = true;
        }
    }
    XrmDestroyDatabase(db);
    return dpi;
}

int clampToScreen(int v, int span) {
    if (span <= 1) return 0;
    if (v < 0) return 0;
    if (v > span - 1) return span - 1;
    return v;
}

// 累积坐标夹在屏幕内（真实指针不可能跑到屏幕外）。
// 注意这里不能在夹取时取整：小数部分必须保留，否则 HiDPI 下每批不足 1 像素的
// 位移会被反复舍掉，产生单向漂移。
void clampLocked() {
    if (gScreenW <= 1) {
        gXf = 0.0;
    } else {
        gXf = std::min(std::max(gXf, 0.0), double(gScreenW - 1));
    }
    if (gScreenH <= 1) {
        gYf = 0.0;
    } else {
        gYf = std::min(std::max(gYf, 0.0), double(gScreenH - 1));
    }
}

}  // namespace

bool isWaylandSession() {
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    const char* session = std::getenv("XDG_SESSION_TYPE");
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    cached = (session && std::strcmp(session, "wayland") == 0) || (wl && *wl) ? 1 : 0;
    return cached == 1;
}

const char* sessionTypeName() { return isWaylandSession() ? "wayland" : "x11"; }

void pointerInit(void* x11Display, int screenW, int screenH) {
    Display* dpy = static_cast<Display*>(x11Display);

    bool fromResources = false;
    float scale = 0.0f;
    const char* envScale = std::getenv("FXPET_POINTER_SCALE");
    if (envScale && *envScale) scale = float(std::atof(envScale));
    const char* scaleSource = "FXPET_POINTER_SCALE";
    if (scale <= 0.01f) {
        scale = readXftDpi(dpy, fromResources) / 96.0f;
        scaleSource = fromResources ? "Xft.dpi" : "无 Xft.dpi，默认 1.0（可用 FXPET_POINTER_SCALE 覆盖）";
    }
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 8.0f) scale = 8.0f;

    std::lock_guard<std::mutex> lock(gMutex);
    gScale = scale;
    gScreenW = screenW > 0 ? screenW : 1;
    gScreenH = screenH > 0 ? screenH : 1;

    // 用当前 X 指针位置做种子。Wayland 下这个值可能已经过期（指针在 Wayland
    // 原生窗口上时 XWayland 收不到更新），但比屏幕中心更接近真值。
    int sx = gScreenW / 2;
    int sy = gScreenH / 2;
    bool seeded = false;
    if (dpy) {
        ::Window rootRet = 0;
        ::Window childRet = 0;
        int wx = 0;
        int wy = 0;
        unsigned mask = 0;
        if (XQueryPointer(dpy, DefaultRootWindow(dpy), &rootRet, &childRet, &sx, &sy, &wx, &wy, &mask)) {
            seeded = sx >= 0 && sy >= 0 && sx < gScreenW && sy < gScreenH;
        }
    }
    gXf = double(clampToScreen(sx, gScreenW));
    gYf = double(clampToScreen(sy, gScreenH));
    gValid = false;  // 种子不是“观测到的位移”，先不算有效
    logLine("指针跟踪：会话=%s 屏幕=%dx%d 缩放=%.2f（%s） 种子=%s(%d,%d)", sessionTypeName(), gScreenW, gScreenH,
            double(gScale), scaleSource, seeded ? "X 指针" : "屏幕中心", int(gXf), int(gYf));
}

void pointerSetTracking(bool on) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (on && !gTracking) gSeedNextPoll = true;  // 重新接管时别拿旧坐标去“校正”
    gTracking = on;
}

bool pointerTrackingActive() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gTracking;
}

void pointerFeedMotion(int dx, int dy) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gTracking || (!dx && !dy)) return;
    // 设备像素 → 逻辑像素。合成器还会做指针加速，这里不做，误差靠校正兜底。
    gXf += double(dx) / double(gScale);
    gYf += double(dy) / double(gScale);
    clampLocked();
    gValid = true;
}

void pointerFeedAbsoluteNorm(int axis, double value) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gTracking) return;
    if (axis == 0) {
        gXf = value * double(gScreenW - 1);
    } else {
        gYf = value * double(gScreenH - 1);
    }
    clampLocked();
    gValid = true;
}

void pointerResync(int x, int y, bool anchor) {
    // X11 下 XQueryPointer 一直是真值，这里的“校正/日志”没有意义
    if (!isWaylandSession()) return;
    std::lock_guard<std::mutex> lock(gMutex);
    if (anchor) gTruthSeen = true;
    if (!gTracking) return;
    const int nx = clampToScreen(x, gScreenW);
    const int ny = clampToScreen(y, gScreenH);
    const bool bigJump = std::abs(nx - int(std::lround(gXf))) > 4 || std::abs(ny - int(std::lround(gYf))) > 4;
    gXf = double(nx);
    gYf = double(ny);
    // 校正来自 X 服务器的真值，即便还没收到过位移也算有效
    gValid = true;
    if (bigJump && gResyncCount < 20) {
        ++gResyncCount;
        logLine("指针校正 → (%d,%d)%s", nx, ny, gResyncCount == 20 ? "（后续校正不再记日志）" : "");
    }
}

void pointerPollX(void* x11Display) {
    Display* dpy = static_cast<Display*>(x11Display);
    if (!dpy) return;
    // 只有 Wayland 需要估算/校正；X11 下 XQueryPointer 每帧本来就问，不必重复
    if (!isWaylandSession() || !pointerTrackingActive()) return;

    ::Window rootRet = 0;
    ::Window childRet = 0;
    int rx = 0;
    int ry = 0;
    int wx = 0;
    int wy = 0;
    unsigned mask = 0;
    if (!XQueryPointer(dpy, DefaultRootWindow(dpy), &rootRet, &childRet, &rx, &ry, &wx, &wy, &mask)) return;

    std::lock_guard<std::mutex> lock(gMutex);
    // XWayland 只在指针位于 X11 窗口上时更新这个值：一旦变化，说明刚有一次
    // “指针确实在 X11 窗口上”的时刻，此时它是真值，用来校正累积估算。
    // 但第一次轮询只是取基准：那个值是“上次落在 X11 窗口时的位置”，可能早就过期，
    // 不能当锚点（否则启动瞬间就会把幽灵位置认定为可信）。
    if (gLastXPointer == std::numeric_limits<int>::min() || gSeedNextPoll) {
        gLastXPointer = rx;
        gLastYPointer = ry;
        gSeedNextPoll = false;
        return;
    }
    const bool changed = rx != gLastXPointer || ry != gLastYPointer;
    gLastXPointer = rx;
    gLastYPointer = ry;
    if (!changed) return;
    gXf = double(clampToScreen(rx, gScreenW));
    gYf = double(clampToScreen(ry, gScreenH));
    gValid = true;
    gTruthSeen = true;
}

bool pointerSample(int& x, int& y, bool& trusted) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gTracking) {
        trusted = false;
        return false;
    }
    x = int(std::lround(gXf));
    y = int(std::lround(gYf));
    // Wayland 下只有“锚定过”的估算才可信：gValid 只代表收到过位移，而位移是从
    // 启动种子累加出来的——种子可能过期（XWayland 只记录指针最后一次落在 X11
    // 窗口时的位置），只靠它会把光标画到完全错误的地方。
    trusted = isWaylandSession() ? gTruthSeen : true;
    return true;
}

bool pointerPositionTrusted() {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!isWaylandSession()) return true;  // X11 下 XQueryPointer 始终是真值
    return gTruthSeen;                     // Wayland 下必须锚定过才算可信
}

void pointerDebugState(int& x, int& y, bool& valid) {
    std::lock_guard<std::mutex> lock(gMutex);
    x = int(std::lround(gXf));
    y = int(std::lround(gYf));
    valid = gValid;
}

}  // namespace fx
