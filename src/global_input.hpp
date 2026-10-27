// 全局键鼠监听：X11 XRecord（无需 root，不抢焦点）。
#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

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

private:
    void run();
    void handleKey(int keycode, bool down);
    void handleButton(int button, bool down);

    void* display_ = nullptr;  // 监听连接（只在该线程内使用）
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> ready_{false};
    std::string startupError_;

    mutable std::mutex mutex_;
    std::vector<InputEvent> pending_;
    std::set<int> pressed_;
};

// 全局鼠标位置（屏幕坐标）
bool queryPointer(int& x, int& y);
bool queryPointerOn(void* display, int& x, int& y);
void* openMainDisplay();

}  // namespace fx
