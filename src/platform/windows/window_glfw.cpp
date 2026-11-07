#include "platform/window.hpp"

#include <windows.h>
#include <dwmapi.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "config.hpp"

namespace fx {
namespace {

bool gGlfwInitialized = false;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif

uint32_t currentMods(GLFWwindow* window) {
    uint32_t mods = 0;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
        mods |= ModCtrl;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        mods |= ModShift;
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
        mods |= ModAlt;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
        mods |= ModSuper;
    return mods;
}

int glfwKeyToVk(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return key;       // ASCII 大写
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return key;       // ASCII 数字
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return 0x70 + (key - GLFW_KEY_F1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return 0x60 + (key - GLFW_KEY_KP_0);

    switch (key) {
        case GLFW_KEY_ESCAPE: return 0x1B;
        case GLFW_KEY_TAB: return 0x09;
        case GLFW_KEY_ENTER: return 0x0D;
        case GLFW_KEY_KP_ENTER: return 0x0D;
        case GLFW_KEY_BACKSPACE: return 0x08;
        case GLFW_KEY_SPACE: return 0x20;
        case GLFW_KEY_PAUSE: return 0x13;
        case GLFW_KEY_CAPS_LOCK: return 0x14;
        case GLFW_KEY_PAGE_UP: return 0x21;
        case GLFW_KEY_PAGE_DOWN: return 0x22;
        case GLFW_KEY_END: return 0x23;
        case GLFW_KEY_HOME: return 0x24;
        case GLFW_KEY_LEFT: return 0x25;
        case GLFW_KEY_UP: return 0x26;
        case GLFW_KEY_RIGHT: return 0x27;
        case GLFW_KEY_DOWN: return 0x28;
        case GLFW_KEY_PRINT_SCREEN: return 0x2C;
        case GLFW_KEY_INSERT: return 0x2D;
        case GLFW_KEY_DELETE: return 0x2E;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT: return 0x10;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return 0x11;
        case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT: return 0x12;
        case GLFW_KEY_LEFT_SUPER:
        case GLFW_KEY_RIGHT_SUPER: return 0x5B;
        case GLFW_KEY_MENU: return 0x5D;
        case GLFW_KEY_NUM_LOCK: return 0x90;
        case GLFW_KEY_SCROLL_LOCK: return 0x91;
        case GLFW_KEY_SEMICOLON: return 0xBA;
        case GLFW_KEY_EQUAL: return 0xBB;
        case GLFW_KEY_COMMA: return 0xBC;
        case GLFW_KEY_MINUS: return 0xBD;
        case GLFW_KEY_PERIOD: return 0xBE;
        case GLFW_KEY_SLASH: return 0xBF;
        case GLFW_KEY_GRAVE_ACCENT: return 0xC0;
        case GLFW_KEY_LEFT_BRACKET: return 0xDB;
        case GLFW_KEY_BACKSLASH: return 0xDC;
        case GLFW_KEY_RIGHT_BRACKET: return 0xDD;
        case GLFW_KEY_APOSTROPHE: return 0xDE;
        default: return 0;
    }
}

int glfwButtonToMouse(int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return 1;
        case GLFW_MOUSE_BUTTON_MIDDLE: return 2;
        case GLFW_MOUSE_BUTTON_RIGHT: return 3;
        case GLFW_MOUSE_BUTTON_4: return 4;
        case GLFW_MOUSE_BUTTON_5: return 5;
        case GLFW_MOUSE_BUTTON_6: return 6;
        case GLFW_MOUSE_BUTTON_7: return 7;
        case GLFW_MOUSE_BUTTON_8: return 8;
        default: return 0;
    }
}

class GlfwWindow : public Window {
public:
    ~GlfwWindow() override { destroy(); }

    bool init(const WindowOptions& options, std::string* error);

    void destroy() override;
    bool makeCurrent() override;
    void swapBuffers() override;
    void pumpEvents(std::vector<WinEvent>& out) override;

    int width() const override { return width_; }
    int height() const override { return height_; }
    int x() const override { return x_; }
    int y() const override { return y_; }
    void move(int x, int y) override;
    void resize(int w, int h) override;

    void show() override;
    void hide() override;
    void raise() override;

    void setClickThrough(bool on) override;
    void grabPointer(bool on) override;
    bool globalPointer(int& x, int& y, bool& trusted) const override;
    bool screenSize(int& w, int& h) const override;
    void setShapeFromAlpha(const uint8_t* rgba, int w, int h) override;
    bool supportsPerPixelAlpha() const override { return supportsAlpha_; }

private:
    static GlfwWindow* self(GLFWwindow* window) {
        return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    }

    void onResize(int w, int h);
    void onMove(int x, int y);
    void onFocus(bool focused);
    void onCursor(double x, double y);
    void onButton(int button, int action);
    void onScroll(double x, double y);
    void onKey(int key, int action);
    void push(const WinEvent& event) { pending_.push_back(event); }

    static void callbackResize(GLFWwindow* w, int width, int height) { self(w)->onResize(width, height); }
    static void callbackMove(GLFWwindow* w, int x, int y) { self(w)->onMove(x, y); }
    static void callbackFocus(GLFWwindow* w, int focused) { self(w)->onFocus(focused != 0); }
    static void callbackCursor(GLFWwindow* w, double x, double y) { self(w)->onCursor(x, y); }
    static void callbackButton(GLFWwindow* w, int button, int action, int) { self(w)->onButton(button, action); }
    static void callbackScroll(GLFWwindow* w, double x, double y) { self(w)->onScroll(x, y); }
    static void callbackKey(GLFWwindow* w, int key, int, int action, int) { self(w)->onKey(key, action); }
    static void callbackClose(GLFWwindow* w) {
        WinEvent event;
        event.type = WinEventType::Close;
        self(w)->push(event);
    }

    GLFWwindow* window_ = nullptr;
    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int x_ = 0;
    int y_ = 0;
    bool supportsAlpha_ = false;
    bool topmost_ = true;
    std::vector<WinEvent> pending_;
};

bool GlfwWindow::init(const WindowOptions& options, std::string* error) {
    if (!gGlfwInitialized) {
        glfwSetErrorCallback([](int code, const char* description) {
            logLine("GLFW 错误 %d: %s", code, description ? description : "");
        });
        if (!glfwInit()) {
            if (error) *error = "GLFW 初始化失败";
            return false;
        }
        gGlfwInitialized = true;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, options.topmost ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, options.transparent ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifdef GLFW_SCALE_FRAMEBUFFER
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
#endif
#ifdef GLFW_SCALE_TO_MONITOR
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
#endif
#ifdef GLFW_MOUSE_PASSTHROUGH
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, options.clickThrough ? GLFW_TRUE : GLFW_FALSE);
#endif

    window_ = glfwCreateWindow(options.w, options.h, "绯雪", nullptr, nullptr);
    if (!window_) {
        // 部分老驱动不支持 3.3 compatibility，退到 3.0
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        window_ = glfwCreateWindow(options.w, options.h, "绯雪", nullptr, nullptr);
    }
    if (!window_) {
        if (error) *error = "无法创建 GLFW 窗口（需要 OpenGL 3.0+ 兼容上下文）";
        return false;
    }

    width_ = options.w;
    height_ = options.h;
    x_ = options.x;
    y_ = options.y;
    topmost_ = options.topmost;

    glfwSetWindowUserPointer(window_, this);
    glfwSetWindowSizeCallback(window_, callbackResize);
    glfwSetWindowPosCallback(window_, callbackMove);
    glfwSetWindowFocusCallback(window_, callbackFocus);
    glfwSetCursorPosCallback(window_, callbackCursor);
    glfwSetMouseButtonCallback(window_, callbackButton);
    glfwSetScrollCallback(window_, callbackScroll);
    glfwSetKeyCallback(window_, callbackKey);
    glfwSetWindowCloseCallback(window_, callbackClose);

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowPos(window_, options.x, options.y);

    hwnd_ = glfwGetWin32Window(window_);
    if (hwnd_) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
        SetWindowPos(hwnd_, options.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const int corner = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    }

    setClickThrough(options.clickThrough);
#ifdef GLFW_TRANSPARENT_FRAMEBUFFER
    supportsAlpha_ = glfwGetWindowAttrib(window_, GLFW_TRANSPARENT_FRAMEBUFFER) != 0;
#else
    supportsAlpha_ = false;
#endif

    if (options.visible) show();

    logLine("GLFW 窗口建立：%dx%d transparent=%d topmost=%d", width_, height_, supportsAlpha_ ? 1 : 0,
            options.topmost ? 1 : 0);
    return true;
}

void GlfwWindow::destroy() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    hwnd_ = nullptr;
    if (gGlfwInitialized) {
        glfwTerminate();
        gGlfwInitialized = false;
    }
}

bool GlfwWindow::makeCurrent() {
    if (!window_) return false;
    glfwMakeContextCurrent(window_);
    return true;
}

void GlfwWindow::swapBuffers() {
    if (window_) glfwSwapBuffers(window_);
}

void GlfwWindow::pumpEvents(std::vector<WinEvent>& out) {
    glfwPollEvents();
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

void GlfwWindow::move(int x, int y) {
    if (!window_) return;
    if (x == x_ && y == y_) return;
    x_ = x;
    y_ = y;
    glfwSetWindowPos(window_, x, y);
}

void GlfwWindow::resize(int w, int h) {
    if (!window_) return;
    if (w == width_ && h == height_) return;
    width_ = w;
    height_ = h;
    glfwSetWindowSize(window_, w, h);
}

void GlfwWindow::show() {
    if (!window_) return;
    glfwShowWindow(window_);
    if (topmost_ && hwnd_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void GlfwWindow::hide() {
    if (window_) glfwHideWindow(window_);
}

void GlfwWindow::raise() {
    if (hwnd_) {
        SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void GlfwWindow::setClickThrough(bool on) {
    if (!window_) return;
#ifdef GLFW_MOUSE_PASSTHROUGH
    glfwSetWindowAttrib(window_, GLFW_MOUSE_PASSTHROUGH, on ? GLFW_TRUE : GLFW_FALSE);
#else
    if (hwnd_) {
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        if (on)
            exStyle |= WS_EX_TRANSPARENT;
        else
            exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
    }
#endif
    // 窗口内默认隐藏系统光标（程序自绘）；锁定时系统光标正常显示
    glfwSetInputMode(window_, GLFW_CURSOR, on ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
}

void GlfwWindow::grabPointer(bool on) {
    if (!hwnd_) return;
    if (on)
        SetCapture(hwnd_);
    else
        ReleaseCapture();
}

bool GlfwWindow::globalPointer(int& x, int& y, bool& trusted) const {
    POINT point{};
    if (!GetCursorPos(&point)) {
        trusted = false;
        return false;
    }
    x = point.x;
    y = point.y;
    trusted = true;
    return true;
}

bool GlfwWindow::screenSize(int& w, int& h) const {
    w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return w > 0 && h > 0;
}

void GlfwWindow::setShapeFromAlpha(const uint8_t* rgba, int w, int h) {
    if (!hwnd_ || !rgba || w <= 0 || h <= 0) return;
    // 没有逐像素透明时退化为 1 比特窗口区域（与 X11 的 ShapeBounding 等价）
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (!region) return;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = rgba + size_t(h - 1 - y) * size_t(w) * 4;  // GL 原点在左下
        int runStart = -1;
        for (int x = 0; x <= w; ++x) {
            const bool solid = x < w && row[size_t(x) * 4 + 3] >= 128;
            if (solid && runStart < 0) {
                runStart = x;
            } else if (!solid && runStart >= 0) {
                HRGN scan = CreateRectRgn(runStart, y, x, y + 1);
                if (scan) {
                    CombineRgn(region, region, scan, RGN_OR);
                    DeleteObject(scan);
                }
                runStart = -1;
            }
        }
    }
    SetWindowRgn(hwnd_, region, TRUE);
    // SetWindowRgn 成功后区域归系统所有，不能再 DeleteObject
}

void GlfwWindow::onResize(int w, int h) {
    width_ = w;
    height_ = h;
    WinEvent event;
    event.type = WinEventType::Resize;
    push(event);
}

void GlfwWindow::onMove(int x, int y) {
    x_ = x;
    y_ = y;
    WinEvent event;
    event.type = WinEventType::Moved;
    push(event);
}

void GlfwWindow::onFocus(bool focused) {
    if (focused) return;
    WinEvent event;
    event.type = WinEventType::FocusLost;
    push(event);
}

void GlfwWindow::onCursor(double x, double y) {
    WinEvent event;
    event.type = WinEventType::MouseMove;
    event.x = int(x);
    event.y = int(y);
    event.mods = currentMods(window_);
    push(event);
}

void GlfwWindow::onButton(int button, int action) {
    const int mapped = glfwButtonToMouse(button);
    if (mapped == 0) return;
    WinEvent event;
    event.type = action == GLFW_PRESS ? WinEventType::MousePress : WinEventType::MouseRelease;
    if (action != GLFW_PRESS && action != GLFW_RELEASE) return;
    event.button = mapped;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window_, &x, &y);
    event.x = int(x);
    event.y = int(y);
    event.mods = currentMods(window_);
    push(event);
}

void GlfwWindow::onScroll(double, double y) {
    WinEvent event;
    event.type = WinEventType::Wheel;
    event.wheelDelta = int(y * 120.0);
    push(event);
}

void GlfwWindow::onKey(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_RELEASE) return;
    WinEvent event;
    event.type = action == GLFW_PRESS ? WinEventType::KeyDown : WinEventType::KeyUp;
    event.vk = glfwKeyToVk(key);
    event.mods = currentMods(window_);
    push(event);
}

}  // namespace

std::unique_ptr<Window> Window::create(const WindowOptions& options, std::string* error) {
    auto window = std::make_unique<GlfwWindow>();
    if (!window->init(options, error)) return nullptr;
    return window;
}

}  // namespace fx
