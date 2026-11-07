#include "input.hpp"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "config.hpp"
#include "evdev_input.hpp"
#include "platform/linux/pointer_wayland.hpp"
#include "platform/linux/x11_keys.hpp"

namespace fx {
namespace {

enum class Backend { Null, XInput2, Evdev };  // 注意：X11 把 None 定义成了宏

class X11InputBackend : public InputBackend {
public:
    ~X11InputBackend() override { stop(); }

    bool start(std::string* error) override;
    void stop() override;
    void poll(std::vector<InputEvent>& out) override { state_.poll(out); }
    bool keyDown(int vk) const override { return state_.keyDown(vk); }

    const char* backendName() const override {
        switch (backend_.load()) {
            case Backend::XInput2: return "XInput2";
            case Backend::Evdev: return "evdev";
            default: return "无";
        }
    }
    bool inputAvailable() const override { return inputAvailable_.load(); }

private:
    bool probeXInput2(std::string* why);
    bool startX11Thread();
    void runX11();
    bool startEvdev(std::string* error);
    void handleKey(int keycode, bool down);
    void handleButton(int button, bool down) { state_.button(mapX11Button(button), down); }

    InputState state_;
    void* display_ = nullptr;  // 监听连接（只在该线程内使用）
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> ready_{false};
    std::atomic<Backend> backend_{Backend::Null};
    std::atomic<bool> inputAvailable_{false};
    EvdevInput evdev_;
    std::string startupError_;
};

bool X11InputBackend::probeXInput2(std::string* why) {
    // 先在主线程探测一次，避免“线程启动失败/启动很慢”与后端回退互相打架
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        *why = "无法连接 X 显示";
        return false;
    }
    int opcode = 0, event = 0, error = 0;
    bool ok = false;
    if (!XQueryExtension(dpy, "XInputExtension", &opcode, &event, &error)) {
        *why = "X 服务器没有 XInput2 扩展";
    } else {
        int major = 2, minor = 2;
        if (XIQueryVersion(dpy, &major, &minor) != Success) {
            *why = "XInput2 版本过低（需要 2.1+ 的 raw 事件）";
        } else {
            ok = true;
        }
    }
    XCloseDisplay(dpy);
    return ok;
}

bool X11InputBackend::startX11Thread() {
    running_ = true;
    ready_ = false;
    thread_ = std::thread([this] { runX11(); });
    // 等监听线程把 XInput2 订阅搭好
    for (int i = 0; i < 400 && !stopFlag_ && !ready_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (ready_) return true;
    stopFlag_ = true;
    running_ = false;
    // 不能 join：线程可能已经订阅成功并阻塞在 XNextEvent 里（stop() 里已注明这个约束），
    // 这里只放手让它自己走完（runX11 会在写 backend_/ready_ 前检查 stopFlag_）。
    if (thread_.joinable()) thread_.detach();
    backend_ = Backend::Null;
    return false;
}

bool X11InputBackend::startEvdev(std::string* error) {
    EvdevCallbacks callbacks;
    callbacks.key = [this](int vk, bool down, int source) { state_.key(vk, down, source); };
    callbacks.button = [this](int button, bool down) { state_.button(button, down); };
    callbacks.motion = [](int dx, int dy) { pointerFeedMotion(dx, dy); };
    callbacks.absolute = [](int axis, double value) { pointerFeedAbsoluteNorm(axis, value); };
    callbacks.reset = [this] { state_.reset(); };
    callbacks.devicesChanged = [this](bool haveDevices, bool havePointer) {
        inputAvailable_ = haveDevices;
        // 只有能提供指针位置的设备（相对/绝对）才能打开累积跟踪；
        // 只读到键盘时仍用 X 的旧位置当参照，不能拿冻结的估算值盖掉真值
        pointerSetTracking(havePointer);
    };

    std::string err;
    if (!evdev_.start(callbacks, &err)) {
        if (error) *error = err.empty() ? std::string("evdev 设备不可用") : err;
        return false;
    }
    backend_ = Backend::Evdev;
    running_ = true;
    inputAvailable_ = evdev_.hasDevices();
    pointerSetTracking(evdev_.hasPointerSource());
    if (inputAvailable_) {
        logLine("全局键鼠监听已开启（evdev：%s）", evdev_.summary().c_str());
    } else {
        logLine("evdev 监听已就绪但还没有可用设备（%s），授权/插设备后会自动接管", evdev_.summary().c_str());
    }
    return true;
}

bool X11InputBackend::start(std::string* error) {
    if (running_) return true;
    stopFlag_ = false;

    std::string why;
    const bool haveXInput2 = probeXInput2(&why);

    // Wayland 下 XInput2 raw 事件只覆盖 XWayland 自己的窗口，优先用 evdev
    if (isWaylandSession()) {
        std::string evdevErr;
        if (startEvdev(&evdevErr)) return true;
        logLine("Wayland 会话下 evdev 监听不可用：%s", evdevErr.c_str());
        if (haveXInput2 && startX11Thread()) {
            logLine("退回 XInput2 监听：只有 XWayland 窗口获得焦点时才能收到按键");
            return true;
        }
        if (error) *error = evdevErr + "\nWayland 下还需要读 /dev/input/event* 的权限";
        return false;
    }

    if (haveXInput2 && startX11Thread()) return true;
    if (!haveXInput2) logLine("XInput2 不可用（%s），尝试 evdev", why.c_str());

    std::string evdevErr;
    if (startEvdev(&evdevErr)) {
        logLine("改用 evdev 监听（X11 会话下指针仍以 XQueryPointer 为准）");
        return true;
    }
    if (error) {
        const std::string x11Reason =
            startupError_.empty() ? (why.empty() ? std::string("启动失败") : why) : startupError_;
        *error = "无法启动全局键鼠监听：XInput2（" + x11Reason + "）与 evdev（" + evdevErr + "）都失败";
    }
    return false;
}

void X11InputBackend::runX11() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        startupError_ = "无法连接 X 显示，全局键鼠监听不可用";
        return;
    }
    if (stopFlag_) {  // 已经被放弃（回退到 evdev）
        XCloseDisplay(dpy);
        return;
    }
    display_ = dpy;

    int xiOpcode = 0, xiEvent = 0, xiError = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &xiOpcode, &xiEvent, &xiError)) {
        startupError_ = "X 服务器没有 XInput2 扩展";
        XCloseDisplay(dpy);
        display_ = nullptr;
        return;
    }
    int major = 2, minor = 2;
    if (XIQueryVersion(dpy, &major, &minor) != Success) {
        startupError_ = "XInput2 版本过低（需要 2.1+ 的 raw 事件）";
        XCloseDisplay(dpy);
        display_ = nullptr;
        return;
    }

    // 在根窗口订阅 raw 事件：全局可见，且不像 XRecord 那样会被别的客户端占用
    unsigned char maskBits[XIMaskLen(XI_LASTEVENT)] = {0};
    XIEventMask mask;
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(maskBits);
    mask.mask = maskBits;
    XISetMask(maskBits, XI_RawKeyPress);
    XISetMask(maskBits, XI_RawKeyRelease);
    XISetMask(maskBits, XI_RawButtonPress);
    XISetMask(maskBits, XI_RawButtonRelease);
    // 让服务器对自动重复只发 KeyPress（不发成对的 KeyRelease），否则按住键会被判成抬起
    Bool supported = False;
    XkbSetDetectableAutoRepeat(dpy, True, &supported);
    logLine("可检测自动重复: %s", supported ? "支持" : "不支持");
    XISelectEvents(dpy, DefaultRootWindow(dpy), &mask, 1);
    XSync(dpy, False);
    if (stopFlag_) {
        XCloseDisplay(dpy);
        display_ = nullptr;
        return;
    }
    backend_ = Backend::XInput2;
    logLine("XInput2 raw 事件监听已开启（%d.%d）", major, minor);
    ready_ = true;

    while (!stopFlag_) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (stopFlag_) break;
        if (ev.type != GenericEvent || ev.xcookie.extension != xiOpcode) continue;
        if (!XGetEventData(dpy, &ev.xcookie)) continue;
        const auto* raw = static_cast<const XIRawEvent*>(ev.xcookie.data);
        const int detail = raw->detail;
        switch (ev.xcookie.evtype) {
            case XI_RawKeyPress: handleKey(detail, true); break;
            case XI_RawKeyRelease: handleKey(detail, false); break;
            case XI_RawButtonPress: handleButton(detail, true); break;
            case XI_RawButtonRelease: handleButton(detail, false); break;
            default: break;
        }
        XFreeEventData(dpy, &ev.xcookie);
    }
    running_ = false;
}

void X11InputBackend::handleKey(int keycode, bool down) {
    Display* dpy = static_cast<Display*>(display_);
    const KeySym ks = dpy ? XkbKeycodeToKeysym(dpy, KeyCode(keycode), 0, 0) : NoSymbol;
    state_.key(keysymToVk(ks, keycode), down, keycode);
}

void X11InputBackend::stop() {
    // XNextEvent 阻塞在监听线程里，跨线程调用 Xlib 会死锁，所以只置退出标志；
    // evdev 线程可以安全 join。
    stopFlag_ = true;
    evdev_.stop();
    running_ = false;
}

}  // namespace

std::unique_ptr<InputBackend> createInputBackend() { return std::make_unique<X11InputBackend>(); }

}  // namespace fx
