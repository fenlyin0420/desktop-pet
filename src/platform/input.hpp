// 全局键鼠输入接口。
// Linux：XInput2 raw / evdev；Windows：Raw Input。
// 公共的按键去重状态在 input_common.cpp，各平台只负责把事件送进来。
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fx {

struct InputEvent {
    enum class Type { KeyDown, KeyUp, MouseDown, MouseUp };
    Type type = Type::KeyDown;
    int code = 0;  // 键盘为 VK 码，鼠标为 1=左 2=中 3=右 5/6=侧键
};

// 平台后端共用的按键状态机：自动重复、左右 Shift 同 VK、热插拔后的残尾。
struct InputState {
    mutable std::mutex mutex;
    std::vector<InputEvent> pending;
    std::map<int, int> keySources;  // 物理来源 → VK
    std::map<int, int> keyCounts;   // VK → 按住的物理来源数量
    std::set<int> buttons;

    void key(int vk, bool down, int source);
    void button(int button, bool down);
    void reset();
    void poll(std::vector<InputEvent>& out);
    bool keyDown(int vk) const;
};

class InputBackend {
public:
    virtual ~InputBackend() = default;

    virtual bool start(std::string* error) = 0;
    virtual void stop() = 0;
    virtual void poll(std::vector<InputEvent>& out) = 0;
    virtual bool keyDown(int vk) const = 0;
    virtual const char* backendName() const = 0;
    virtual bool inputAvailable() const = 0;
};

// 由 platform/<os>/input_*.cpp 实现
std::unique_ptr<InputBackend> createInputBackend();

// 保持原有调用方式：main/pet 只依赖这个薄包装。
class GlobalInput {
public:
    GlobalInput();
    ~GlobalInput();

    bool start(std::string* error);
    void stop();
    void poll(std::vector<InputEvent>& out);
    bool keyDown(int vk) const;
    const char* backendName() const;
    bool inputAvailable() const;

private:
    std::unique_ptr<InputBackend> backend_;
};

}  // namespace fx
