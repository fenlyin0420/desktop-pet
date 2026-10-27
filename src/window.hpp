// X11 + EGL 窗口：拿 32 位 ARGB 视觉 → 有合成器时逐像素透明；
// 没有合成器时退化为 1 比特裁剪形状。
#pragma once

#include <X11/Xlib.h>

#include <cstdint>
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

struct WinEvent {
    WinEventType type = WinEventType::MouseMove;
    int x = 0;       // 窗口内坐标
    int y = 0;
    int button = 0;  // 鼠标按钮
    int delta = 0;   // 滚轮方向
    unsigned int key = 0;   // keysym
    unsigned int state = 0; // X11 修饰键掩码
};

class Window {
public:
    ~Window() { destroy(); }

    bool create(int x, int y, int w, int h, std::string* error);
    void destroy();

    bool makeCurrent();
    void swapBuffers();

    void pumpEvents(std::vector<WinEvent>& out);

    int width() const { return width_; }
    int height() const { return height_; }
    int x() const { return x_; }
    int y() const { return y_; }
    void move(int x, int y);
    void resize(int w, int h);

    void show();
    void hide();
    void raise();

    // 点击穿透（锁定状态）
    void setClickThrough(bool on);
    // 没有合成器时用渲染结果的 alpha 生成裁剪形状
    void setShapeFromAlpha(const uint8_t* rgba, int w, int h);
    bool hasAlphaVisual() const { return depth32_; }
    bool compositorPresent() const { return compositor_; }

    void grabPointer(bool on);
    bool globalPointer(int& x, int& y) const;
    bool screenSize(int& w, int& h) const;

    Display* display() const { return dpy_; }

private:
    Display* dpy_ = nullptr;
    ::Window win_ = 0;
    Colormap colormap_ = 0;
    int width_ = 0;
    int height_ = 0;
    int x_ = 0;
    int y_ = 0;
    bool depth32_ = false;
    bool compositor_ = false;

    void* eglDisplay_ = nullptr;
    void* eglSurface_ = nullptr;
    void* eglContext_ = nullptr;

    Pixmap shapePixmap_ = 0;
    GC shapeGc_ = nullptr;
    int shapeW_ = 0, shapeH_ = 0;
    bool clickThrough_ = false;

    void applyWindowHints();
    void updateShapeBounds(int w, int h);
    bool detectCompositor();
};

}  // namespace fx
