// 全局键鼠监听。
//
// X11 会话：XInput2 raw 事件（无需 root，不抢焦点，全局可见）。
// Wayland 会话：XWayland 拿不到别处发生的按键/指针，改用 evdev 直读（见 evdev_input）。
#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "evdev_input.hpp"

namespace fx {

struct InputEvent {
    enum class Type { KeyDown, KeyUp, MouseDown, MouseUp };
    Type type = Type::KeyDown;
    int code = 0;  // 键盘为 VK 码（与 config.json 一致），鼠标为按钮号（1=左,2=中,3=右,5/6=侧键）
};

class GlobalInput {
public:
    ~GlobalInput();

    bool start(std::string* error);
    void stop();

    // 取走上一帧以来累积的事件
    void poll(std::vector<InputEvent>& out);

    bool keyDown(int vk) const;
    bool modifier(int vk) const { return keyDown(vk); }
    static const char* vkName(int vk);

    // 当前使用的后端（日志/诊断用）
    const char* backendName() const;
    // evdev 后端是否真的拿到了设备（Wayland + 未授权时为 false）
    bool inputAvailable() const { return inputAvailable_; }

private:
    enum class Backend { Null, XInput2, Evdev };  // 注意：X11 把 None 定义成了宏

    bool probeXInput2(std::string* why);
    bool startX11Thread();
    void runX11();
    bool startEvdev(std::string* error);
    void handleKey(int keycode, bool down);  // X keycode → VK
    void handleVk(int vk, bool down, int source);  // source = 物理按键来源
    void handleButton(int button, bool down);
    void resetPressed();

    void* display_ = nullptr;  // 监听连接（只在该线程内使用）
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> ready_{false};
    std::atomic<Backend> backend_{Backend::Null};
    std::atomic<bool> inputAvailable_{false};
    EvdevInput evdev_;
    std::string startupError_;

    mutable std::mutex mutex_;
    std::vector<InputEvent> pending_;
    // 物理按键来源 → VK（自动重复/同一 VK 的左右 Shift 都靠它区分）
    std::map<int, int> keySources_;
    // VK → 当前按住的物理源数量（左右 Shift/Ctrl 等共用同一 VK，不能简单去重）
    std::map<int, int> keyCounts_;
    std::set<int> buttons_;
};

// 全局鼠标位置（屏幕坐标）
bool queryPointer(int& x, int& y);
bool queryPointerOn(void* display, int& x, int& y);
void* openMainDisplay();

}  // namespace fx
