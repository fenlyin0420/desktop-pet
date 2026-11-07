// X11 + EGL 窗口实现。
#pragma once

#include <X11/Xlib.h>

#include <cstdint>
#include <string>
#include <vector>

#include "platform/window.hpp"

namespace fx {

class X11Window : public Window {
public:
    ~X11Window() override { destroy(); }

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
    bool supportsPerPixelAlpha() const override { return depth32_ && compositor_; }

private:
    void resyncPointerFromEvent(int x, int y, int rootX, int rootY, bool anchor);
    void applyWindowHints();
    void updateShapeBounds(int w, int h);
    bool detectCompositor();

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
};

}  // namespace fx
