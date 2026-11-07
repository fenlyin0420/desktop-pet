#include "input.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "config.hpp"

namespace fx {
namespace {

constexpr wchar_t kWindowClass[] = L"FxPetRawInput";

class RawInputBackend : public InputBackend {
public:
    ~RawInputBackend() override { stop(); }

    bool start(std::string* error) override;
    void stop() override;
    void poll(std::vector<InputEvent>& out) override { state_.poll(out); }
    bool keyDown(int vk) const override { return state_.keyDown(vk); }
    const char* backendName() const override { return hooksInstalled_ ? "LLHook" : "RawInput"; }
    bool inputAvailable() const override { return inputAvailable_.load(); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK mouseProc(int code, WPARAM wParam, LPARAM lParam);
    void run();
    void handleRawInput(LPARAM lParam);
    void handleKeyboard(const RAWKEYBOARD& keyboard);
    void handleMouse(const RAWMOUSE& mouse);
    void removeHooks();

    InputState state_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> inputAvailable_{false};
    std::atomic<DWORD> threadId_{0};
    HWND hwnd_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    std::atomic<bool> hooksInstalled_{false};
    std::string startupError_;
};

RawInputBackend* gHookBackend = nullptr;

LRESULT CALLBACK RawInputBackend::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    RawInputBackend* backend = reinterpret_cast<RawInputBackend*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        backend = static_cast<RawInputBackend*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(backend));
    }
    if (backend && message == WM_INPUT) {
        backend->handleRawInput(lParam);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK RawInputBackend::keyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && gHookBackend) {
        const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const int vk = int(keyboard->vkCode);
        const int source = (int(keyboard->scanCode) << 1) | ((keyboard->flags & LLKHF_EXTENDED) ? 1 : 0);
        if (vk != 0) gHookBackend->state_.key(vk, down, source);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK RawInputBackend::mouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && gHookBackend) {
        auto& state = gHookBackend->state_;
        switch (wParam) {
            case WM_LBUTTONDOWN: state.button(1, true); break;
            case WM_LBUTTONUP: state.button(1, false); break;
            case WM_MBUTTONDOWN: state.button(2, true); break;
            case WM_MBUTTONUP: state.button(2, false); break;
            case WM_RBUTTONDOWN: state.button(3, true); break;
            case WM_RBUTTONUP: state.button(3, false); break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
                const WORD button = HIWORD(mouse->mouseData);
                const int mapped = button == XBUTTON1 ? 5 : (button == XBUTTON2 ? 6 : 0);
                if (mapped) state.button(mapped, wParam == WM_XBUTTONDOWN);
                break;
            }
            default: break;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool RawInputBackend::start(std::string* error) {
    if (running_) return true;
    stopFlag_ = false;
    ready_ = false;
    inputAvailable_ = false;
    startupError_.clear();

    thread_ = std::thread([this] { run(); });
    for (int i = 0; i < 400 && !ready_ && !stopFlag_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!ready_ || !inputAvailable_) {
        stopFlag_ = true;
        if (threadId_) PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
        if (thread_.joinable()) thread_.join();
        if (error) *error = startupError_.empty() ? std::string("Raw Input 初始化失败") : startupError_;
        return false;
    }
    running_ = true;
    return true;
}

void RawInputBackend::stop() {
    stopFlag_ = true;
    if (threadId_) PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    threadId_ = 0;
    hwnd_ = nullptr;
    running_ = false;
}

void RawInputBackend::removeHooks() {
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    hooksInstalled_ = false;
    gHookBackend = nullptr;
}

void RawInputBackend::run() {
    threadId_ = GetCurrentThreadId();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &RawInputBackend::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        startupError_ = "RegisterClassExW 失败（GetLastError=" + std::to_string(GetLastError()) + "）";
        ready_ = true;
        return;
    }

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"", 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) {
        startupError_ = "创建 Raw Input 消息窗口失败（GetLastError=" + std::to_string(GetLastError()) + "）";
        ready_ = true;
        return;
    }

    RAWINPUTDEVICE devices[2]{};
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x06;  // 键盘
    devices[0].dwFlags = RIDEV_INPUTSINK;
    devices[0].hwndTarget = hwnd_;
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x02;  // 鼠标
    devices[1].dwFlags = RIDEV_INPUTSINK;
    devices[1].hwndTarget = hwnd_;

    // 默认 Raw Input；FXPET_INPUT_BACKEND=hook 可强制低级钩子（排错/兼容用）
    const char* requested = std::getenv("FXPET_INPUT_BACKEND");
    const bool wantHook = requested && std::strcmp(requested, "hook") == 0;

    auto installHooks = [&]() -> bool {
        gHookBackend = this;
        keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &RawInputBackend::keyboardProc, wc.hInstance, 0);
        mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &RawInputBackend::mouseProc, wc.hInstance, 0);
        if (keyboardHook_ && mouseHook_) return true;
        removeHooks();
        return false;
    };
    auto registerRaw = [&]() -> bool {
        return RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
    };

    bool started = false;
    if (!wantHook && registerRaw()) {
        inputAvailable_ = true;
        logLine("全局键鼠监听已开启（Raw Input）");
        started = true;
    }
    if (!started && installHooks()) {
        hooksInstalled_ = true;
        inputAvailable_ = true;
        logLine("全局键鼠监听已开启（低级钩子）");
        started = true;
    }
    if (!started && wantHook && registerRaw()) {
        inputAvailable_ = true;
        logLine("全局键鼠监听已开启（Raw Input）");
        started = true;
    }
    if (!started) {
        startupError_ = "RegisterRawInputDevices 与低级钩子都失败（GetLastError=" +
                        std::to_string(GetLastError()) + "）";
        ready_ = true;
        return;
    }

    ready_ = true;

    MSG message;
    while (!stopFlag_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    removeHooks();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(kWindowClass, wc.hInstance);
}

void RawInputBackend::handleRawInput(LPARAM lParam) {
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0) return;
    if (size == 0) return;
    std::vector<uint8_t> buffer(size);
    const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size,
                                      sizeof(RAWINPUTHEADER));
    if (read != size) return;
    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        handleKeyboard(raw->data.keyboard);
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        handleMouse(raw->data.mouse);
    }
}

void RawInputBackend::handleKeyboard(const RAWKEYBOARD& keyboard) {
    int vk = keyboard.VKey;
    if (vk == 0 || vk == 0xFF) {
        vk = int(MapVirtualKeyW(keyboard.MakeCode, MAPVK_VSC_TO_VK_EX));
    }
    if (vk == 0) return;
    const bool down = (keyboard.Flags & RI_KEY_BREAK) == 0;
    const int source = (int(keyboard.MakeCode) << 1) | ((keyboard.Flags & RI_KEY_E0) ? 1 : 0);
    state_.key(vk, down, source);
}

void RawInputBackend::handleMouse(const RAWMOUSE& mouse) {
    const USHORT flags = mouse.usButtonFlags;
    auto button = [&](USHORT downFlag, USHORT upFlag, int code) {
        if (flags & downFlag) state_.button(code, true);
        if (flags & upFlag) state_.button(code, false);
    };
    button(RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, 1);
    button(RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 2);
    button(RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, 3);
    // 配置里的侧键沿用 5/6
    button(RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, 5);
    button(RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, 6);
}

}  // namespace

std::unique_ptr<InputBackend> createInputBackend() { return std::make_unique<RawInputBackend>(); }

}  // namespace fx
