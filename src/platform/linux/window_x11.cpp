#include "window_x11.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>

#include <algorithm>
#include <cstring>

#include "config.hpp"
#include "platform/linux/pointer_wayland.hpp"
#include "platform/linux/x11_keys.hpp"

namespace fx {

namespace {

bool hasCompositorSelection(Display* dpy) {
    const char* names[] = {"_NET_WM_CM_S0", "_NET_WM_CM_S1", nullptr};
    for (int i = 0; names[i]; ++i) {
        Atom a = XInternAtom(dpy, names[i], True);
        if (a != None && XGetSelectionOwner(dpy, a) != None) return true;
    }
    return false;
}

}  // namespace

bool X11Window::detectCompositor() { return hasCompositorSelection(dpy_); }

bool X11Window::init(const WindowOptions& options, std::string* error) {
    XInitThreads();

    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_) {
        if (error) *error = "无法连接 X 显示（需要 X11 会话）";
        return false;
    }
    compositor_ = detectCompositor();

    EGLDisplay edpy = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(dpy_));
    if (edpy == EGL_NO_DISPLAY || !eglInitialize(edpy, nullptr, nullptr)) {
        if (error) *error = "EGL 初始化失败";
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint cfgAttribs[] = {EGL_SURFACE_TYPE,
                                 EGL_WINDOW_BIT,
                                 EGL_RENDERABLE_TYPE,
                                 EGL_OPENGL_ES2_BIT,
                                 EGL_RED_SIZE,
                                 8,
                                 EGL_GREEN_SIZE,
                                 8,
                                 EGL_BLUE_SIZE,
                                 8,
                                 EGL_ALPHA_SIZE,
                                 8,
                                 EGL_STENCIL_SIZE,
                                 8,
                                 EGL_DEPTH_SIZE,
                                 0,
                                 EGL_NONE};
    EGLConfig configs[64];
    EGLint numConfigs = 0;
    if (!eglChooseConfig(edpy, cfgAttribs, configs, 64, &numConfigs) || numConfigs <= 0) {
        if (error) *error = "找不到合适的 EGL 配置（需要 RGBA8888 + stencil）";
        return false;
    }

    // 优先挑深度 32 的 ARGB 视觉，才能做逐像素透明
    Visual* visual = nullptr;
    int depth = 0;
    EGLConfig chosen = nullptr;
    for (int i = 0; i < numConfigs; ++i) {
        EGLint visualId = 0;
        if (!eglGetConfigAttrib(edpy, configs[i], EGL_NATIVE_VISUAL_ID, &visualId) || visualId == 0) continue;
        XVisualInfo tmpl{};
        tmpl.visualid = VisualID(visualId);
        int count = 0;
        XVisualInfo* infos = XGetVisualInfo(dpy_, VisualIDMask, &tmpl, &count);
        if (!infos || count <= 0) {
            if (infos) XFree(infos);
            continue;
        }
        if (!visual || (infos[0].depth == 32 && depth != 32)) {
            visual = infos[0].visual;
            depth = infos[0].depth;
            chosen = configs[i];
            if (depth == 32) {
                XFree(infos);
                break;
            }
        }
        XFree(infos);
    }
    if (!chosen || !visual) {
        if (error) *error = "找不到可用的 X 视觉";
        return false;
    }
    depth32_ = depth == 32;

    const int screen = DefaultScreen(dpy_);
    colormap_ = XCreateColormap(dpy_, RootWindow(dpy_, screen), visual, AllocNone);

    XSetWindowAttributes attrs{};
    attrs.colormap = colormap_;
    attrs.background_pixmap = None;
    attrs.border_pixel = 0;
    attrs.override_redirect = True;
    attrs.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | KeyReleaseMask |
                       ExposureMask | StructureNotifyMask | FocusChangeMask | EnterWindowMask | LeaveWindowMask;

    win_ = XCreateWindow(dpy_, RootWindow(dpy_, screen), options.x, options.y, unsigned(options.w), unsigned(options.h), 0,
                         depth, InputOutput, visual, CWColormap | CWBackPixmap | CWBorderPixel | CWOverrideRedirect | CWEventMask,
                         &attrs);
    if (!win_) {
        if (error) *error = "创建 X 窗口失败";
        return false;
    }
    width_ = options.w;
    height_ = options.h;
    x_ = options.x;
    y_ = options.y;

    applyWindowHints();

    EGLSurface esurf = eglCreateWindowSurface(edpy, chosen, reinterpret_cast<EGLNativeWindowType>(win_), nullptr);
    if (esurf == EGL_NO_SURFACE) {
        if (error) *error = "EGL 创建窗口表面失败";
        return false;
    }
    const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ectx = eglCreateContext(edpy, chosen, EGL_NO_CONTEXT, ctxAttribs);
    if (ectx == EGL_NO_CONTEXT) {
        if (error) *error = "EGL 创建 OpenGL ES2 上下文失败";
        return false;
    }

    eglDisplay_ = edpy;
    eglSurface_ = esurf;
    eglContext_ = ectx;
    if (!makeCurrent()) {
        if (error) *error = "EGL 上下文激活失败";
        return false;
    }
    eglSwapInterval(edpy, 1);

    GLint stencilBits = -1;
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
    EGLint cfgStencil = -1;
    eglGetConfigAttrib(edpy, chosen, EGL_STENCIL_SIZE, &cfgStencil);
    EGLint cfgAlpha = -1;
    eglGetConfigAttrib(edpy, chosen, EGL_ALPHA_SIZE, &cfgAlpha);
    logLine("GL 上下文: stencilBits=%d (config stencil=%d, alpha=%d)", stencilBits, cfgStencil, cfgAlpha);

    XMapWindow(dpy_, win_);
    XFlush(dpy_);

    int screenW = 0;
    int screenH = 0;
    screenSize(screenW, screenH);
    pointerInit(dpy_, screenW, screenH);

    if (options.clickThrough) setClickThrough(true);
    if (!options.visible) hide();

    int shapeEv = 0, shapeErr = 0;
    logLine("X11 窗口建立：depth=%d %s，合成器=%s，XShape=%d", depth, depth32_ ? "(ARGB)" : "(无 alpha)",
            compositor_ ? "有" : "无", XShapeQueryExtension(dpy_, &shapeEv, &shapeErr));
    return true;
}

void X11Window::applyWindowHints() {
    Atom above = XInternAtom(dpy_, "_NET_WM_STATE_ABOVE", False);
    Atom skipTaskbar = XInternAtom(dpy_, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skipPager = XInternAtom(dpy_, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom sticky = XInternAtom(dpy_, "_NET_WM_STATE_STICKY", False);
    Atom wmState = XInternAtom(dpy_, "_NET_WM_STATE", False);
    Atom atoms[4] = {above, skipTaskbar, skipPager, sticky};
    XChangeProperty(dpy_, win_, wmState, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(atoms), 4);

    Atom wmType = XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE", False);
    Atom typeNotify = XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    XChangeProperty(dpy_, win_, wmType, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&typeNotify), 1);

    // 所有工作区可见
    Atom desktop = XInternAtom(dpy_, "_NET_WM_DESKTOP", False);
    long allDesktops = -1;
    XChangeProperty(dpy_, win_, desktop, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&allDesktops), 1);

    XClassHint hint{};
    hint.res_name = const_cast<char*>("fxpet");
    hint.res_class = const_cast<char*>("FxPet");
    XSetClassHint(dpy_, win_, &hint);
    XStoreName(dpy_, win_, "绯雪");

    // 桌宠自己画鼠标指针，所以把系统指针在窗口内隐藏（空光标）
    {
        char data[1] = {0};
        Pixmap blank = XCreateBitmapFromData(dpy_, win_, data, 1, 1);
        XColor dummy{};
        Cursor invisible = XCreatePixmapCursor(dpy_, blank, blank, &dummy, &dummy, 0, 0);
        XDefineCursor(dpy_, win_, invisible);
        XFreePixmap(dpy_, blank);
    }
}

void X11Window::destroy() {
    if (shapePixmap_ && dpy_) XFreePixmap(dpy_, shapePixmap_);
    if (shapeGc_ && dpy_) XFreeGC(dpy_, shapeGc_);
    shapePixmap_ = 0;
    shapeGc_ = nullptr;

    EGLDisplay edpy = eglDisplay_ ? static_cast<EGLDisplay>(eglDisplay_) : nullptr;
    if (edpy != EGL_NO_DISPLAY && edpy) {
        eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglSurface_ && eglSurface_ != EGL_NO_SURFACE) eglDestroySurface(edpy, static_cast<EGLSurface>(eglSurface_));
        if (eglContext_ && eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(edpy, static_cast<EGLContext>(eglContext_));
        eglTerminate(edpy);
    }
    eglDisplay_ = eglSurface_ = eglContext_ = nullptr;

    if (dpy_) {
        if (win_) XDestroyWindow(dpy_, win_);
        if (colormap_) XFreeColormap(dpy_, colormap_);
        XCloseDisplay(dpy_);
    }
    dpy_ = nullptr;
    win_ = 0;
}

bool X11Window::makeCurrent() {
    if (!eglDisplay_) return false;
    return eglMakeCurrent(static_cast<EGLDisplay>(eglDisplay_), static_cast<EGLSurface>(eglSurface_),
                          static_cast<EGLSurface>(eglSurface_), static_cast<EGLContext>(eglContext_)) == EGL_TRUE;
}

void X11Window::swapBuffers() {
    if (eglDisplay_) eglSwapBuffers(static_cast<EGLDisplay>(eglDisplay_), static_cast<EGLSurface>(eglSurface_));
}

// 指针事件发生在窗口内时，X 根坐标就是真值（Wayland 下用来校正累积估算）。
// 抓取期间指针可以跑到窗口外，此时 x_root 可能是外推值，不做校正。
void X11Window::resyncPointerFromEvent(int x, int y, int rootX, int rootY, bool anchor) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    pointerResync(rootX, rootY, anchor);
}

void X11Window::pumpEvents(std::vector<WinEvent>& out) {
    if (!dpy_) return;
    // Wayland 下 X 指针一动就说明它刚经过某个 X11 窗口，用真值校正累积估算
    pointerPollX(dpy_);

    while (XPending(dpy_)) {
        XEvent ev;
        XNextEvent(dpy_, &ev);
        WinEvent we;
        switch (ev.type) {
            case ButtonPress: {
                resyncPointerFromEvent(ev.xbutton.x, ev.xbutton.y, ev.xbutton.x_root, ev.xbutton.y_root, true);
                if (ev.xbutton.button == 4 || ev.xbutton.button == 5) {
                    we.type = WinEventType::Wheel;
                    we.wheelDelta = ev.xbutton.button == 4 ? 120 : -120;
                    out.push_back(we);
                    break;
                }
                we.type = WinEventType::MousePress;
                we.button = mapX11Button(ev.xbutton.button);
                we.x = ev.xbutton.x;
                we.y = ev.xbutton.y;
                we.mods = modsFromState(ev.xbutton.state);
                out.push_back(we);
                break;
            }
            case ButtonRelease: {
                resyncPointerFromEvent(ev.xbutton.x, ev.xbutton.y, ev.xbutton.x_root, ev.xbutton.y_root, true);
                if (ev.xbutton.button == 4 || ev.xbutton.button == 5) break;
                we.type = WinEventType::MouseRelease;
                we.button = mapX11Button(ev.xbutton.button);
                we.x = ev.xbutton.x;
                we.y = ev.xbutton.y;
                we.mods = modsFromState(ev.xbutton.state);
                out.push_back(we);
                break;
            }
            case MotionNotify:
                resyncPointerFromEvent(ev.xmotion.x, ev.xmotion.y, ev.xmotion.x_root, ev.xmotion.y_root, true);
                we.type = WinEventType::MouseMove;
                we.x = ev.xmotion.x;
                we.y = ev.xmotion.y;
                we.mods = modsFromState(ev.xmotion.state);
                out.push_back(we);
                break;
            case KeyPress: {
                we.type = WinEventType::KeyDown;
                we.vk = keysymToVk(XLookupKeysym(&ev.xkey, 0), ev.xkey.keycode);
                we.mods = modsFromState(ev.xkey.state);
                out.push_back(we);
                break;
            }
            case KeyRelease: {
                we.type = WinEventType::KeyUp;
                we.vk = keysymToVk(XLookupKeysym(&ev.xkey, 0), ev.xkey.keycode);
                we.mods = modsFromState(ev.xkey.state);
                out.push_back(we);
                break;
            }
            case EnterNotify:
                // 窗口自己移到（可能已过期的）X 指针底下也会来 EnterNotify，不能当作锚点
                resyncPointerFromEvent(ev.xcrossing.x, ev.xcrossing.y, ev.xcrossing.x_root, ev.xcrossing.y_root, false);
                break;
            case FocusOut:
                we.type = WinEventType::FocusLost;
                out.push_back(we);
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != width_ || ev.xconfigure.height != height_) {
                    width_ = ev.xconfigure.width;
                    height_ = ev.xconfigure.height;
                    we.type = WinEventType::Resize;
                    out.push_back(we);
                }
                if (ev.xconfigure.x != x_ || ev.xconfigure.y != y_) {
                    x_ = ev.xconfigure.x;
                    y_ = ev.xconfigure.y;
                    we.type = WinEventType::Moved;
                    out.push_back(we);
                }
                break;
            case ClientMessage: {
                Atom wmDelete = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
                if (Atom(ev.xclient.data.l[0]) == wmDelete) {
                    we.type = WinEventType::Close;
                    out.push_back(we);
                }
                break;
            }
            default: break;
        }
    }
}

void X11Window::move(int x, int y) {
    if (!dpy_ || !win_) return;
    if (x == x_ && y == y_) return;  // 拖拽兜底每帧都会调用，值没变就别打扰 X
    x_ = x;
    y_ = y;
    XMoveWindow(dpy_, win_, x, y);
    XFlush(dpy_);
}

void X11Window::resize(int w, int h) {
    if (!dpy_ || !win_) return;
    if (w == width_ && h == height_) return;
    width_ = w;
    height_ = h;
    XResizeWindow(dpy_, win_, unsigned(w), unsigned(h));
    XFlush(dpy_);
}

void X11Window::show() {
    if (!dpy_ || !win_) return;
    XMapWindow(dpy_, win_);
    XFlush(dpy_);
}

void X11Window::hide() {
    if (!dpy_ || !win_) return;
    XUnmapWindow(dpy_, win_);
    XFlush(dpy_);
}

void X11Window::raise() {
    if (!dpy_ || !win_) return;
    XRaiseWindow(dpy_, win_);
    XFlush(dpy_);
}

void X11Window::updateShapeBounds(int w, int h) {
    if (clickThrough_) {
        // 输入区域置空 → 鼠标穿透
        XShapeCombineRectangles(dpy_, win_, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
    } else {
        XRectangle rect{0, 0, static_cast<unsigned short>(w), static_cast<unsigned short>(h)};
        XShapeCombineRectangles(dpy_, win_, ShapeInput, 0, 0, &rect, 1, ShapeSet, Unsorted);
    }
    XFlush(dpy_);
}

void X11Window::setClickThrough(bool on) {
    if (!dpy_ || !win_) return;
    clickThrough_ = on;
    updateShapeBounds(width_, height_);
}

void X11Window::setShapeFromAlpha(const uint8_t* rgba, int w, int h) {
    if (!dpy_ || !win_ || !rgba || w <= 0 || h <= 0) return;
    const int stride = (w + 7) / 8;
    // XBM 位图按 LSB-first 排列，用 XCreateBitmapFromData 避免自造 XImage 的位序坑
    std::vector<uint8_t> bits(size_t(stride) * size_t(h), 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = rgba + size_t(h - 1 - y) * size_t(w) * 4;  // GL 原点在左下
        uint8_t* out = &bits[size_t(y) * size_t(stride)];
        for (int x = 0; x < w; ++x) {
            if (row[size_t(x) * 4 + 3] >= 128) out[x >> 3] |= uint8_t(1u << (x & 7));
        }
    }
    Pixmap pm = XCreateBitmapFromData(dpy_, win_, reinterpret_cast<const char*>(bits.data()), unsigned(w), unsigned(h));
    if (!pm) return;
    XShapeCombineMask(dpy_, win_, ShapeBounding, 0, 0, pm, ShapeSet);
    XFreePixmap(dpy_, pm);
    XFlush(dpy_);
    static int applied = 0;
    if (applied < 3) {
        logLine("裁剪形状已应用 %dx%d（第 %d 次）", w, h, applied + 1);
        ++applied;
    }
}

void X11Window::grabPointer(bool on) {
    if (!dpy_ || !win_) return;
    if (on) {
        XGrabPointer(dpy_, win_, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                     GrabModeAsync, None, None, CurrentTime);
    } else {
        XUngrabPointer(dpy_, CurrentTime);
    }
    XFlush(dpy_);
}

bool X11Window::globalPointer(int& x, int& y, bool& trusted) const {
    trusted = false;
    if (!dpy_) return false;
    // 只有 Wayland 会话才需要 evdev 累积估算（X11 下 XQueryPointer 就是真值，
    // 即使全局监听退回了 evdev，也不能拿估算值换掉精确值）
    if (isWaylandSession()) {
        if (pointerSample(x, y, trusted)) return true;
        // 没有 evdev 跟踪时退回 X 指针：位置可能已过期，只有窗口内事件校正过才可信
        trusted = pointerPositionTrusted();
    }
    ::Window rootRet = 0, childRet = 0;
    int wx = 0, wy = 0;
    unsigned mask = 0;
    if (!XQueryPointer(dpy_, DefaultRootWindow(dpy_), &rootRet, &childRet, &x, &y, &wx, &wy, &mask)) {
        trusted = false;
        return false;
    }
    if (!isWaylandSession()) trusted = true;
    return true;
}

bool X11Window::screenSize(int& w, int& h) const {
    if (!dpy_) return false;
    const int screen = DefaultScreen(dpy_);
    w = DisplayWidth(dpy_, screen);
    h = DisplayHeight(dpy_, screen);
    return w > 0 && h > 0;
}

std::unique_ptr<Window> Window::create(const WindowOptions& options, std::string* error) {
    auto window = std::make_unique<X11Window>();
    if (!window->init(options, error)) return nullptr;
    return window;
}

}  // namespace fx
