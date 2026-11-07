// 平台窗口接口：不包含任何系统头文件。
// Linux 由 X11 + EGL 实现，Windows 由 GLFW + WGL 实现。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fx {

enum class WinEventType {
    MousePress,
    MouseRelease,
    MouseMove,
    Wheel,
    KeyDown,
    KeyUp,
    FocusLost,
    Resize,
    Moved,
    Close,
};

// 与 config.json 的 VK 语义保持一致；WinEvent 里的 vk 也是 VK 码。
enum Modifier : uint32_t {
    ModCtrl = 1u << 0,
    ModShift = 1u << 1,
    ModAlt = 1u << 2,
    ModSuper = 1u << 3,
};

struct WinEvent {
    WinEventType type = WinEventType::MouseMove;
    int x = 0;              // 窗口内坐标
    int y = 0;
    int button = 0;         // 1=左 2=中 3=右 5/6=侧键
    int wheelDelta = 0;     // 120 = 一格
    int vk = 0;             // Windows VK 码
    uint32_t mods = 0;      // Modifier 位
};

struct WindowOptions {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool transparent = true;
    bool topmost = true;
    bool skipTaskbar = true;
    bool clickThrough = false;
    bool visible = true;
};

class Window {
public:
    // 创建窗口并建立、激活 OpenGL 上下文。失败返回 nullptr 并写 error。
    static std::unique_ptr<Window> create(const WindowOptions& options, std::string* error);

    virtual ~Window() = default;

    virtual void destroy() = 0;
    virtual bool makeCurrent() = 0;
    virtual void swapBuffers() = 0;

    virtual void pumpEvents(std::vector<WinEvent>& out) = 0;

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int x() const = 0;
    virtual int y() const = 0;
    virtual void move(int x, int y) = 0;
    virtual void resize(int w, int h) = 0;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void raise() = 0;

    // 锁定状态：鼠标穿透
    virtual void setClickThrough(bool on) = 0;
    // 拖动期间抓取指针（Windows 用 SetCapture，X11 用 XGrabPointer）
    virtual void grabPointer(bool on) = 0;

    // 屏幕坐标
    virtual bool globalPointer(int& x, int& y, bool& trusted) const = 0;
    bool globalPointer(int& x, int& y) const {
        bool trusted = false;
        return globalPointer(x, y, trusted);
    }
    virtual bool screenSize(int& w, int& h) const = 0;

    // 只有 X11 无合成器时才需要；Windows 为空实现
    virtual void setShapeFromAlpha(const uint8_t* rgba, int w, int h) = 0;
    // 当前窗口是否支持逐像素透明
    virtual bool supportsPerPixelAlpha() const = 0;
};

}  // namespace fx
