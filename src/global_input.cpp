#include "global_input.hpp"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include <chrono>
#include <cstring>

#include "config.hpp"
#include "pointer.hpp"

namespace fx {

static int keysymToVk(KeySym ks, int keycode) {
    // 字母/数字：VK 与 ASCII 大写一致
    if (ks >= 'a' && ks <= 'z') return int(ks - 'a' + 'A');
    if (ks >= 'A' && ks <= 'Z') return int(ks);
    if (ks >= '0' && ks <= '9') return int(ks);
    switch (ks) {
        case XK_BackSpace: return 0x08;
        case XK_Tab: return 0x09;
        case XK_Return: case XK_KP_Enter: return 0x0D;
        case XK_Pause: return 0x13;
        case XK_Caps_Lock: return 0x14;
        case XK_Escape: return 0x1B;
        case XK_space: return 0x20;
        case XK_Page_Up: return 0x21;
        case XK_Page_Down: return 0x22;
        case XK_End: return 0x23;
        case XK_Home: return 0x24;
        case XK_Left: return 0x25;
        case XK_Up: return 0x26;
        case XK_Right: return 0x27;
        case XK_Down: return 0x28;
        case XK_Print: return 0x2C;
        case XK_Insert: return 0x2D;
        case XK_Delete: return 0x2E;
        case XK_Shift_L: case XK_Shift_R: return 0x10;
        case XK_Control_L: case XK_Control_R: return 0x11;
        case XK_Alt_L: case XK_Alt_R: return 0x12;
        case XK_Super_L: case XK_Super_R: return 0x5B;
        case XK_Menu: return 0x5D;
        case XK_Num_Lock: return 0x90;
        case XK_Scroll_Lock: return 0x91;
        case XK_semicolon: return 0xBA;
        case XK_equal: return 0xBB;
        case XK_comma: return 0xBC;
        case XK_minus: return 0xBD;
        case XK_period: return 0xBE;
        case XK_slash: return 0xBF;
        case XK_grave: return 0xC0;
        case XK_bracketleft: return 0xDB;
        case XK_backslash: return 0xDC;
        case XK_bracketright: return 0xDD;
        case XK_apostrophe: return 0xDE;
        default: break;
    }
    if (ks >= XK_F1 && ks <= XK_F12) return 0x70 + int(ks - XK_F1);
    if (ks >= XK_KP_0 && ks <= XK_KP_9) return 0x60 + int(ks - XK_KP_0);
    return keycode;  // 未识别：退回 X keycode（配置里也可用 keycode 指定）
}

const char* GlobalInput::vkName(int vk) {
    static char buf[8];
    if (vk >= 0x41 && vk <= 0x5A) {
        buf[0] = char(vk);
        buf[1] = 0;
        return buf;
    }
    snprintf(buf, sizeof(buf), "%d", vk);
    return buf;
}

const char* GlobalInput::backendName() const {
    switch (backend_.load()) {
        case Backend::XInput2: return "XInput2";
        case Backend::Evdev: return "evdev";
        default: return "无";
    }
}

void* openMainDisplay() { return XOpenDisplay(nullptr); }

bool queryPointerOn(void* display, int& x, int& y) {
    Display* dpy = static_cast<Display*>(display);
    if (!dpy) return false;
    Window root = DefaultRootWindow(dpy);
    Window child = 0, rootRet = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    if (!XQueryPointer(dpy, root, &rootRet, &child, &rx, &ry, &wx, &wy, &mask)) return false;
    x = rx;
    y = ry;
    return true;
}

bool queryPointer(int& x, int& y) {
    static Display* dpy = nullptr;
    if (!dpy) {
        dpy = XOpenDisplay(nullptr);
        if (!dpy) return false;
    }
    return queryPointerOn(dpy, x, y);
}

bool GlobalInput::probeXInput2(std::string* why) {
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

bool GlobalInput::startX11Thread() {
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

bool GlobalInput::startEvdev(std::string* error) {
    EvdevCallbacks callbacks;
    callbacks.key = [this](int vk, bool down, int source) { handleVk(vk, down, source); };
    callbacks.button = [this](int button, bool down) { handleButton(button, down); };
    callbacks.motion = [](int dx, int dy) { pointerFeedMotion(dx, dy); };
    callbacks.absolute = [](int axis, double value) { pointerFeedAbsoluteNorm(axis, value); };
    callbacks.reset = [this] { resetPressed(); };
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

bool GlobalInput::start(std::string* error) {
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
        if (error) {
            *error = evdevErr + "\nWayland 下还需要读 /dev/input/event* 的权限";
        }
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
        const std::string x11Reason = startupError_.empty() ? (why.empty() ? std::string("启动失败") : why) : startupError_;
        *error = "无法启动全局键鼠监听：XInput2（" + x11Reason + "）与 evdev（" + evdevErr + "）都失败";
    }
    return false;
}

void GlobalInput::runX11() {
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

void GlobalInput::handleKey(int keycode, bool down) {
    Display* dpy = static_cast<Display*>(display_);
    const KeySym ks = dpy ? XkbKeycodeToKeysym(dpy, KeyCode(keycode), 0, 0) : NoSymbol;
    handleVk(keysymToVk(ks, keycode), down, keycode);
}

void GlobalInput::handleVk(int vk, bool down, int source) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto sourceIt = keySources_.find(source);
    int emittedVk = vk;
    if (down) {
        if (sourceIt != keySources_.end()) return;  // 同一个物理键重复（自动重复）
        keySources_[source] = vk;
        const int count = ++keyCounts_[vk];
        if (count != 1) return;  // 同一 VK 的另一个物理键（左右 Shift），已经在按下状态
    } else {
        if (sourceIt == keySources_.end()) return;  // 没记过按下：忽略（热插拔后的残尾）
        emittedVk = sourceIt->second;  // 用记录时的 VK 扣减，别信抬起事件里的映射
        keySources_.erase(sourceIt);
        const auto countIt = keyCounts_.find(emittedVk);
        if (countIt == keyCounts_.end()) return;
        if (--countIt->second > 0) return;
        keyCounts_.erase(countIt);
    }
    InputEvent ev;
    ev.type = down ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
    ev.code = emittedVk;
    pending_.push_back(ev);
}

void GlobalInput::handleButton(int button, bool down) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (down) {
        if (!buttons_.insert(button).second) return;  // 已经在按下状态
    } else if (buttons_.erase(button) == 0) {
        return;  // 没记过按下
    }
    InputEvent ev;
    ev.type = down ? InputEvent::Type::MouseDown : InputEvent::Type::MouseUp;
    ev.code = button;
    pending_.push_back(ev);
}

void GlobalInput::resetPressed() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : keyCounts_) {
        InputEvent ev;
        ev.type = InputEvent::Type::KeyUp;
        ev.code = entry.first;
        pending_.push_back(ev);
    }
    for (int button : buttons_) {
        InputEvent ev;
        ev.type = InputEvent::Type::MouseUp;
        ev.code = button;
        pending_.push_back(ev);
    }
    keySources_.clear();
    keyCounts_.clear();
    buttons_.clear();
}

void GlobalInput::stop() {
    // XNextEvent 阻塞在监听线程里，跨线程调用 Xlib 会死锁，所以只置退出标志；
    // evdev 线程可以安全 join。
    stopFlag_ = true;
    evdev_.stop();
    running_ = false;
}

GlobalInput::~GlobalInput() { stop(); }

void GlobalInput::poll(std::vector<InputEvent>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

bool GlobalInput::keyDown(int vk) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return keyCounts_.count(vk) != 0;
}

}  // namespace fx
