#include "global_input.hpp"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include <chrono>
#include <cstring>

#include "config.hpp"

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

bool GlobalInput::start(std::string* error) {
    if (running_) return true;
    stopFlag_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    // 等监听线程把 XInput2 订阅搭好
    for (int i = 0; i < 400 && running_ && !ready_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!ready_) {
        if (error) {
            *error = startupError_.empty() ? std::string("无法启动全局键鼠监听（XInput2）。Wayland 会话下不可用，请改用 X11 会话。")
                                           : startupError_;
        }
        running_ = false;
        return false;
    }
    return true;
}

void GlobalInput::run() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        startupError_ = "无法连接 X 显示，全局键鼠监听不可用";
        running_ = false;
        return;
    }
    display_ = dpy;

    int xiOpcode = 0, xiEvent = 0, xiError = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &xiOpcode, &xiEvent, &xiError)) {
        startupError_ = "X 服务器没有 XInput2 扩展";
        running_ = false;
        return;
    }
    int major = 2, minor = 2;
    if (XIQueryVersion(dpy, &major, &minor) != Success) {
        startupError_ = "XInput2 版本过低（需要 2.1+ 的 raw 事件）";
        running_ = false;
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
    logLine("XInput2 raw 事件监听已开启（%d.%d）", major, minor);
    ready_ = true;

    while (!stopFlag_) {
        XEvent ev;
        XNextEvent(dpy, &ev);
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
    const int vk = keysymToVk(ks, keycode);
    InputEvent ev;
    ev.type = down ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
    ev.code = vk;
    std::lock_guard<std::mutex> lock(mutex_);
    if (down) {
        if (pressed_.count(vk)) return;  // 自动重复
        pressed_.insert(vk);
    } else {
        pressed_.erase(vk);
    }
    pending_.push_back(ev);
}

void GlobalInput::handleButton(int button, bool down) {
    InputEvent ev;
    ev.type = down ? InputEvent::Type::MouseDown : InputEvent::Type::MouseUp;
    ev.code = button;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(ev);
}

void GlobalInput::stop() {
    // XRecordEnableContext 只能在它自己的连接上阻塞退出；跨线程调用 Xlib 会死锁，
    // 因此这里只置退出标志，线程随进程一起结束（X 服务器会回收连接）。
    stopFlag_ = true;
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
    return pressed_.count(vk) != 0;
}

}  // namespace fx
