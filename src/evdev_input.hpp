// evdev 全局键鼠监听：直接读 /dev/input/event*，不依赖 X / 合成器。
//
// 为什么需要它：Wayland 下不存在“全局键鼠监听”的协议，XWayland 也只在自己
// 的窗口有焦点时才转发事件，XInput2 raw 事件拿不到别处发生的按键。
// 直接读 evdev 是唯一与桌面环境无关的办法，代价是需要设备读权限：把用户加入
// input 组，或装一条 udev uaccess 规则（tools/setup_wayland_input.sh）。
//
// 这里只读不抓（不调用 EVIOCGRAB），不影响正常使用；设备热插拔、以及“先启动
// 程序后补权限”的情况都用 inotify 感知后重扫。
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fx {

struct EvdevCallbacks {
    // 键盘：VK 码（与 config.json 一致）+ 物理按键来源标识（设备 id 与键码组合），
    // 上层按来源去重，自动重复与“左右 Shift 同 VK”都能正确处理
    std::function<void(int vk, bool down, int source)> key;
    std::function<void(int button, bool down)> button;   // 鼠标按钮：1 左 2 中 3 右 4/5/6 侧键
    std::function<void(int dx, int dy)> motion;          // 相对位移（设备像素）
    std::function<void(int axis, double value)> absolute;  // 绝对坐标设备：axis 0=X/1=Y，值归一化 0..1
    std::function<void()> reset;                         // 设备集合变化：清空按键/按钮状态
    // 可用设备出现/消失；havePointer 表示其中有能提供指针位置的设备
    std::function<void(bool haveDevices, bool havePointer)> devicesChanged;
};

class EvdevInput {
public:
    ~EvdevInput();

    // 打开 /dev/input 并启动读线程。没有任何可用设备时也会继续监听（等授权后
    // 自动接管）；只有连监听都无法建立时才返回 false 并写 error。
    bool start(const EvdevCallbacks& callbacks, std::string* error);
    void stop();

    bool hasDevices() const { return hasDevices_.load(); }
    bool hasPointerSource() const { return hasPointer_.load(); }
    std::string summary() const;

private:
    struct Device {
        int fd = -1;
        int id = 0;  // 扫描内唯一，用于拼“物理按键来源”标识
        std::string path;
        std::string name;
        bool dropUntilSync = false;  // SYN_DROPPED 之后的残缺事件丢弃到下一个 SYN_REPORT
        bool keyboard = false;
        bool buttons = false;
        bool motion = false;
        bool absolute = false;
        int absXMin = 0, absXMax = 0, absYMin = 0, absYMax = 0;
    };

    void run();
    bool rescan();
    void closeDevices();
    void readDevice(Device& dev);
    void drainInotify();

    EvdevCallbacks callbacks_;
    std::vector<Device> devices_;
    std::string summary_;
    std::string signature_;
    mutable std::mutex summaryMutex_;
    std::atomic<bool> hasDevices_{false};
    std::atomic<bool> hasPointer_{false};
    int inotifyFd_ = -1;
    std::atomic<bool> needRescan_{false};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopFlag_{false};
};

}  // namespace fx
