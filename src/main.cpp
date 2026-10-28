// fxpet —— 绯雪的 Linux 桌宠（C++ / X11 + EGL / OpenGL ES2 / Live2D Cubism Core）
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <png.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "config.hpp"
#include "gl.hpp"
#include "global_input.hpp"
#include "pet.hpp"
#include "pointer.hpp"
#include "text.hpp"
#include "window.hpp"

namespace {

volatile std::sig_atomic_t gStop = 0;

void onSignal(int) { gStop = 1; }

bool writePng(const std::string& path, const uint8_t* rgba, int w, int h) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        return false;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, png_uint_32(w), png_uint_32(h), 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    std::vector<png_bytep> rows;
    rows.resize(size_t(h));
    for (int y = 0; y < h; ++y) rows[size_t(y)] = const_cast<png_bytep>(rgba + size_t(h - 1 - y) * size_t(w) * 4);
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
    return true;
}

void usage() {
    std::printf(
        "fxpet —— 绯雪 Linux 桌宠\n"
        "用法: fxpet [选项]\n"
        "  --config <文件>    指定 config.json\n"
        "  --assets <目录>    指定资源目录（模型与图片直接在其下）\n"
        "  --size <WxH>       覆盖窗口尺寸\n"
        "  --l2d-correct <x>  Live2D 缩放系数（1.0 = 铺满窗口）\n"
        "  --no-flip          不水平翻转 Live2D 模型\n"
        "  --no-mouse         不绘制鼠标指针贴图\n"
        "  --shape            强制用 1 比特裁剪（没有合成器时更可靠）\n"
        "  --no-shape          不用裁剪形状（逐像素透明）\n"
        "  --shot <文件>      渲染一帧存成 PNG（自检，不显示窗口）\n"
        "  --press <vk>       模拟按下某个虚拟键码（自检手部帧）\n"
        "  --dump             打印模型参数/部件/drawable\n"
        "  --param id=value   覆盖某个 Live2D 参数（调试用，可重复）\n"
        "  --part id=value    覆盖某个部件不透明度（调试用，可重复）\n"
        "  --only i,j,k       只画这些 drawable（调试用）\n"
        "  --hide i,j,k       跳过这些 drawable（调试用）\n"
        "  --respect-visible  按模型的可见性标志过滤（默认行为）\n"
        "  --show-all-full    无视可见性和 opacity 全部绘制（调试用）\n"
        "  --no-masks         不做遮罩裁剪（调试用）\n"
        "  --masks-invert     按 Core 的反向遮罩标志处理（默认行为）\n"
        "  --opaque           所有 drawable 按不透明绘制（调试用）\n"
        "  --no-physics       关闭 physics3.json 物理演算（调试对比用）\n"
        "  --eye-open <v>     强制 ParamEyeLOpen/ROpen（调试用）\n"
        "  --mouth-open <v>   强制 ParamMouthOpenY（调试用）\n"
        "  -h, --help         显示帮助\n"
        "\n"
        "Wayland 会话下全局键鼠需要读 /dev/input：先运行 tools/setup_wayland_input.sh uaccess\n"
        "热键: Ctrl+Shift+T 锁定/解锁（锁定 = 鼠标穿透）\n"
        "      Ctrl+Shift+H 隐藏/显示   Ctrl+Shift+R 重置大小   Ctrl+Shift+Q 退出\n"
        "      鼠标左键拖动 = 移动    右键拖动 = 缩放   右键单击 / 中键 = 菜单   滚轮 = 缩放\n");
}

double nowSeconds() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

}  // namespace

int main(int argc, char** argv) {
    // 全局输入线程与主线程各自使用独立的 X 连接，必须先开 Xlib 线程支持
    XInitThreads();

    fx::Config cfg;
    fx::resolvePaths(cfg);

    std::string shotPath;
    bool noMouse = false;
    bool noFlip = false;
    bool dumpModel = false;
    std::vector<std::string> paramOverrides;
    std::vector<std::string> partOverrides;
    std::string onlyList, hideList;
    bool respectVisible = true;
    bool forceFullAll = false;
    bool noMasks = false;
    bool masksInvert = false;
    bool opaqueAll = false;
    bool physicsOff = false;
    float eyeOpen = 0.0f, mouthOpen = 0.0f;
    bool hasEyeOpen = false, hasMouthOpen = false;
    bool forceShape = false;
    bool debugInput = false;
    std::string liveFramePath;
    int dumpFrameCountdown = 0;
    bool forceNoShape = false;
    float overrideCorrect = -1.0f;
    int pressVk = 0;
    int forceW = 0, forceH = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--config" || a == "--assets") && i + 1 < argc) {
            if (a == "--config") cfg.configPath = argv[++i];
            else {
                cfg.assetDir = argv[++i];
                cfg.configPath = cfg.assetDir + "/config.json";
            }
        } else if (a == "--shot" && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (a == "--size" && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &forceW, &forceH);
        } else if (a == "--l2d-correct" && i + 1 < argc) {
            overrideCorrect = float(std::atof(argv[++i]));
        } else if (a == "--press" && i + 1 < argc) {
            pressVk = std::atoi(argv[++i]);
        } else if (a == "--no-mouse") {
            noMouse = true;
        } else if (a == "--shape") {
            forceShape = true;
        } else if (a == "--debug-input") {
            debugInput = true;
        } else if (a == "--no-shape") {
            forceNoShape = true;
        } else if (a == "--no-flip") {
            noFlip = true;
        } else if (a == "--param" && i + 1 < argc) {
            paramOverrides.push_back(argv[++i]);
        } else if (a == "--part" && i + 1 < argc) {
            partOverrides.push_back(argv[++i]);
        } else if (a == "--only" && i + 1 < argc) {
            onlyList = argv[++i];
        } else if (a == "--hide" && i + 1 < argc) {
            hideList = argv[++i];
        } else if (a == "--respect-visible") {
            respectVisible = true;
        } else if (a == "--show-all-full") {
            forceFullAll = true;
        } else if (a == "--no-masks") {
            noMasks = true;
        } else if (a == "--masks-invert") {
            masksInvert = true;
        } else if (a == "--opaque") {
            opaqueAll = true;
        } else if (a == "--no-physics") {
            physicsOff = true;
        } else if (a == "--eye-open" && i + 1 < argc) {
            eyeOpen = float(std::atof(argv[++i]));
            hasEyeOpen = true;
        } else if (a == "--mouth-open" && i + 1 < argc) {
            mouthOpen = float(std::atof(argv[++i]));
            hasMouthOpen = true;
        } else if (a == "--dump") {
            dumpModel = true;
        } else if (a == "--dump-frame" && i + 1 < argc) {
            liveFramePath = argv[++i];
            dumpFrameCountdown = 90;
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "未知参数: %s\n", a.c_str());
            usage();
            return 2;
        }
    }

    std::string err;
    if (!fx::loadConfig(cfg, &err)) fx::logLine("配置读取失败：%s（使用内置默认值）", err.c_str());
    if (noMouse) cfg.standardMouse = false;
    if (noFlip) cfg.l2dFlipX = false;
    if (overrideCorrect > 0.0f) cfg.l2dCorrect = overrideCorrect;

    fx::State state;
    fx::loadState(cfg, state);

    int winW = state.window.w > 0 ? state.window.w : std::max(160, int(float(cfg.windowW) * cfg.scalarX * 0.55f));
    int winH = state.window.h > 0 ? state.window.h : std::max(120, int(float(cfg.windowH) * cfg.scalarY * 0.55f));
    if (forceW > 0 && forceH > 0) {
        winW = forceW;
        winH = forceH;
    }
    int posX = state.window.x;
    int posY = state.window.y;
    {
        // 首次运行放右下角；换显示器/分辨率后也要保证窗口还在屏幕内
        Display* dpy = XOpenDisplay(nullptr);
        if (dpy) {
            const int sw = DisplayWidth(dpy, DefaultScreen(dpy));
            const int sh = DisplayHeight(dpy, DefaultScreen(dpy));
            if (state.window.w <= 0) {
                posX = sw - winW - 24;
                posY = sh - winH - 48;
            } else {
                // 保证窗口整体留在屏幕内（窗口比屏幕还大时至少贴住左上角）
                posX = winW >= sw ? 0 : std::max(0, std::min(posX, sw - winW));
                posY = winH >= sh ? 0 : std::max(0, std::min(posY, sh - winH));
            }
            XCloseDisplay(dpy);
        }
    }

    fx::Window window;
    std::string winErr;
    if (!window.create(posX, posY, winW, winH, &winErr)) {
        fx::logLine("窗口创建失败：%s", winErr.c_str());
        std::fprintf(stderr, "%s\n", winErr.c_str());
        return 1;
    }

    // Wayland 会话下全局指针坐标只能靠 evdev 累积估算，先建好状态再启动监听
    {
        int screenW = 0, screenH = 0;
        window.screenSize(screenW, screenH);
        fx::pointerInit(window.display(), screenW, screenH);
    }

    fx::GLContext gl;
    std::string glErr;
    if (!gl.init(&glErr)) {
        fx::logLine("%s", glErr.c_str());
        std::fprintf(stderr, "%s\n", glErr.c_str());
        return 1;
    }

    fx::TextRenderer text;
    std::string textErr;
    if (!text.init(&textErr)) fx::logLine("%s", textErr.c_str());

    fx::Pet pet;
    std::string petErr;
    pet.init(cfg, state, gl, text, window, &petErr);
    if (!petErr.empty()) fx::logLine("提示：%s", petErr.c_str());
    if (!state.visible) window.hide();
    for (const std::string& spec : paramOverrides) {
        const size_t eq = spec.find('=');
        if (eq == std::string::npos) continue;
        pet.debugSetParam(spec.substr(0, eq), float(std::atof(spec.c_str() + eq + 1)));
    }
    auto parseIntList = [](const std::string& text) {
        std::vector<int> out;
        size_t start = 0;
        while (start < text.size()) {
            size_t comma = text.find(',', start);
            const std::string piece = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!piece.empty()) out.push_back(std::atoi(piece.c_str()));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return out;
    };
    if (respectVisible) pet.debugSetRespectVisible(true);
    if (forceFullAll) {
        pet.debugSetRespectVisible(false);
        pet.debugSetForceFull(true);
    }
    if (noMasks) pet.debugSetDisableMasks(true);
    if (masksInvert) pet.debugSetForceInvertedMask(true);
    if (opaqueAll) pet.debugSetForceOpaque(true);
    if (physicsOff) pet.debugSetPhysics(false);
    if (hasEyeOpen) pet.debugSetEyeOpen(eyeOpen);
    if (hasMouthOpen) pet.debugSetMouthOpen(mouthOpen);
    if (!onlyList.empty()) pet.debugSetOnly(parseIntList(onlyList));
    if (!hideList.empty()) pet.debugSetHidden(parseIntList(hideList));
    {
        std::map<std::string, float> parts;
        for (const std::string& spec : partOverrides) {
            const size_t eq = spec.find('=');
            if (eq == std::string::npos) continue;
            parts[spec.substr(0, eq)] = float(std::atof(spec.c_str() + eq + 1));
        }
        if (!parts.empty()) pet.debugSetParts(parts);
    }
    if (pressVk != 0) pet.debugPress(pressVk);

    if (dumpModel) {
        pet.dumpModel();
        pet.shutdown(gl);
        window.destroy();
        return 0;
    }

    if (!shotPath.empty()) {
        gl.beginFrame(winW, winH, fx::Color{0, 0, 0, 0});
        pet.frame(0.016f, winW, winH, gl);
        std::vector<uint8_t> pixels(size_t(winW) * size_t(winH) * 4);
        gl.readPixels(0, 0, winW, winH, pixels.data());
        const bool ok = writePng(shotPath, pixels.data(), winW, winH);
        std::printf("%s\n", ok ? ("已保存: " + shotPath).c_str() : "写出 PNG 失败");
        pet.shutdown(gl);
        window.destroy();
        return ok ? 0 : 1;
    }

    std::string dumpFramePath = liveFramePath;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    fx::GlobalInput input;
    std::string inputErr;
    if (!input.start(&inputErr)) {
        fx::logLine("%s", inputErr.c_str());
        std::fprintf(stderr, "警告：%s\n", inputErr.c_str());
    } else {
        fx::logLine("输入后端：%s（会话 %s）", input.backendName(), fx::sessionTypeName());
        if (fx::isWaylandSession() && !input.inputAvailable()) {
            std::fprintf(stderr,
                         "提示：Wayland 会话下还没有可读的输入设备，按键动画与鼠标跟随不可用。\n"
                         "      运行 tools/setup_wayland_input.sh check 查看，"
                         "或 tools/setup_wayland_input.sh uaccess 授权（之后无需重启程序）。\n");
        }
    }

    // 没有 32 位视觉或没有合成器时，逐像素透明不可用 → 用 1 比特裁剪形状
    bool needShape = forceShape || !window.hasAlphaVisual() || !window.compositorPresent();
    if (forceNoShape) needShape = false;
    bool lastLocked = state.locked;
    window.setClickThrough(state.locked);
    if (needShape) fx::logLine("逐像素透明不可用，改用硬边裁剪（想要柔和边缘请启用桌面合成）");

    bool running = true;
    std::vector<fx::WinEvent> winEvents;
    std::vector<fx::InputEvent> inputEvents;
    double last = nowSeconds();
    double shapeTimer = 0.0;
    const int targetFps = std::max(10, std::min(240, cfg.framerate));
    const double frameBudget = 1.0 / double(targetFps);

    while (running && !gStop) {
        const double frameStart = nowSeconds();

        // Wayland 下 X 指针一动就说明它刚经过某个 X11 窗口，用真值校正累积估算
        fx::pointerPollX(window.display());

        winEvents.clear();
        window.pumpEvents(winEvents);
        if (!winEvents.empty()) pet.handleWindowEvents(winEvents);
        if (debugInput) {
            static int debugTick = 0;
            if ((++debugTick % 30) == 0) {
                int px = 0, py = 0;
                bool valid = false;
                fx::pointerDebugState(px, py, valid);
                std::printf("pointer motion_seen=%d (%d,%d) trusted=%d tracking=%d backend=%s\n", valid ? 1 : 0, px, py,
                            int(fx::pointerPositionTrusted()), int(fx::pointerTrackingActive()), input.backendName());
                std::fflush(stdout);
            }
        }

        inputEvents.clear();
        input.poll(inputEvents);
        if (!inputEvents.empty()) {
            if (debugInput) {
                for (const auto& e : inputEvents) {
                    std::printf("input type=%d code=%d\n", int(e.type), e.code);
                    std::fflush(stdout);
                }
            }
            pet.handleGlobalInput(inputEvents, input);
        }

        if (state.locked != lastLocked) {
            lastLocked = state.locked;
            window.setClickThrough(state.locked);
        }

        int w = window.width();
        int h = window.height();
        if (w < 16) w = 16;
        if (h < 16) h = 16;

        const double now = nowSeconds();
        float dt = float(now - last);
        last = now;
        if (dt <= 0.0f) dt = 0.001f;
        if (dt > 0.2f) dt = 0.2f;

        pet.frame(dt, w, h, gl);

        if (!dumpFramePath.empty()) {
            dumpFrameCountdown -= 1;
            if (dumpFrameCountdown <= 0) {
                std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
                gl.readPixels(0, 0, w, h, px.data());
                writePng(dumpFramePath, px.data(), w, h);
                fx::logLine("已保存实时帧 %s", dumpFramePath.c_str());
                dumpFramePath.clear();
            }
        }

        if (needShape && state.visible) {
            shapeTimer += dt;
            if (shapeTimer >= 0.1) {
                shapeTimer = 0.0;
                std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 4);
                gl.readPixels(0, 0, w, h, pixels.data());
                window.setShapeFromAlpha(pixels.data(), w, h);
            }
        }

        if (state.visible) {
            gl.present();
            window.swapBuffers();
        }

        state.window = fx::Rect{window.x(), window.y(), window.width(), window.height()};
        running = !pet.quitRequested();

        const double spent = nowSeconds() - frameStart;
        if (spent < frameBudget) {
            struct timespec ts {};
            const double sleep = frameBudget - spent;
            ts.tv_sec = time_t(sleep);
            ts.tv_nsec = long((sleep - double(ts.tv_sec)) * 1e9);
            nanosleep(&ts, nullptr);
        }
    }

    fx::saveState(cfg, state);
    fx::logLine("退出");
    // XRecord 监听线程阻塞在 XRecordEnableContext 里，无法安全地从别的线程取消；
    // 直接结束进程，X 连接与 GL 资源由内核/服务器回收。
    std::fflush(nullptr);
    std::_Exit(0);
    return 0;
}
