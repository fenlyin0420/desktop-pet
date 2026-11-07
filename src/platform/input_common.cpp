#include "input.hpp"

namespace fx {

void InputState::key(int vk, bool down, int source) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto sourceIt = keySources.find(source);
    int emittedVk = vk;
    if (down) {
        if (sourceIt != keySources.end()) return;  // 同一个物理键重复（自动重复）
        keySources[source] = vk;
        const int count = ++keyCounts[vk];
        if (count != 1) return;  // 同一 VK 的另一个物理键（左右 Shift），已经在按下状态
    } else {
        if (sourceIt == keySources.end()) return;  // 没记过按下：忽略（热插拔后的残尾）
        emittedVk = sourceIt->second;             // 用记录时的 VK 扣减，别信抬起事件里的映射
        keySources.erase(sourceIt);
        const auto countIt = keyCounts.find(emittedVk);
        if (countIt == keyCounts.end()) return;
        if (--countIt->second > 0) return;
        keyCounts.erase(countIt);
    }
    InputEvent ev;
    ev.type = down ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
    ev.code = emittedVk;
    pending.push_back(ev);
}

void InputState::button(int button, bool down) {
    std::lock_guard<std::mutex> lock(mutex);
    if (down) {
        if (!buttons.insert(button).second) return;  // 已经在按下状态
    } else if (buttons.erase(button) == 0) {
        return;  // 没记过按下
    }
    InputEvent ev;
    ev.type = down ? InputEvent::Type::MouseDown : InputEvent::Type::MouseUp;
    ev.code = button;
    pending.push_back(ev);
}

void InputState::reset() {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& entry : keyCounts) {
        InputEvent ev;
        ev.type = InputEvent::Type::KeyUp;
        ev.code = entry.first;
        pending.push_back(ev);
    }
    for (int button : buttons) {
        InputEvent ev;
        ev.type = InputEvent::Type::MouseUp;
        ev.code = button;
        pending.push_back(ev);
    }
    keySources.clear();
    keyCounts.clear();
    buttons.clear();
}

void InputState::poll(std::vector<InputEvent>& out) {
    std::lock_guard<std::mutex> lock(mutex);
    out.insert(out.end(), pending.begin(), pending.end());
    pending.clear();
}

bool InputState::keyDown(int vk) const {
    std::lock_guard<std::mutex> lock(mutex);
    return keyCounts.count(vk) != 0;
}

GlobalInput::GlobalInput() : backend_(createInputBackend()) {}
GlobalInput::~GlobalInput() = default;

bool GlobalInput::start(std::string* error) { return backend_ && backend_->start(error); }
void GlobalInput::stop() {
    if (backend_) backend_->stop();
}
void GlobalInput::poll(std::vector<InputEvent>& out) { backend_->poll(out); }
bool GlobalInput::keyDown(int vk) const { return backend_ && backend_->keyDown(vk); }
const char* GlobalInput::backendName() const { return backend_ ? backend_->backendName() : "无"; }
bool GlobalInput::inputAvailable() const { return backend_ && backend_->inputAvailable(); }

}  // namespace fx
