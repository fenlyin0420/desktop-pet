#include "evdev_input.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.hpp"

namespace fx {
namespace {

constexpr int kMaxEventsPerRead = 64;

template <size_t N>
bool testBit(const unsigned long (&bits)[N], unsigned int bit) {
    constexpr unsigned int kWordBits = unsigned(8 * sizeof(unsigned long));
    const size_t word = bit / kWordBits;
    if (word >= N) return false;
    return (bits[word] >> (bit % kWordBits)) & 1UL;
}

// Linux 输入码 → VK 码（与 config.json 里的 Windows 虚拟键码一致）
struct VkMapEntry {
    unsigned int code;
    int vk;
};

const VkMapEntry kVkMap[] = {
    {KEY_ESC, 0x1B},        {KEY_BACKSPACE, 0x08},  {KEY_TAB, 0x09},        {KEY_ENTER, 0x0D},
    {KEY_KPENTER, 0x0D},    {KEY_CAPSLOCK, 0x14},   {KEY_SPACE, 0x20},      {KEY_PAUSE, 0x13},
    {KEY_SYSRQ, 0x2C},      {KEY_INSERT, 0x2D},     {KEY_DELETE, 0x2E},     {KEY_HOME, 0x24},
    {KEY_END, 0x23},        {KEY_PAGEUP, 0x21},     {KEY_PAGEDOWN, 0x22},   {KEY_LEFT, 0x25},
    {KEY_UP, 0x26},         {KEY_RIGHT, 0x27},      {KEY_DOWN, 0x28},       {KEY_LEFTSHIFT, 0x10},
    {KEY_RIGHTSHIFT, 0x10}, {KEY_LEFTCTRL, 0x11},   {KEY_RIGHTCTRL, 0x11},  {KEY_LEFTALT, 0x12},
    {KEY_RIGHTALT, 0x12},   {KEY_LEFTMETA, 0x5B},   {KEY_RIGHTMETA, 0x5B},  {KEY_COMPOSE, 0x5D},
    {KEY_NUMLOCK, 0x90},    {KEY_SCROLLLOCK, 0x91}, {KEY_SEMICOLON, 0xBA},  {KEY_EQUAL, 0xBB},
    {KEY_COMMA, 0xBC},      {KEY_MINUS, 0xBD},      {KEY_DOT, 0xBE},        {KEY_SLASH, 0xBF},
    {KEY_GRAVE, 0xC0},      {KEY_LEFTBRACE, 0xDB},  {KEY_BACKSLASH, 0xDC},  {KEY_RIGHTBRACE, 0xDD},
    {KEY_APOSTROPHE, 0xDE},
};

const VkMapEntry kLetterMap[] = {
    {KEY_Q, 'Q'}, {KEY_W, 'W'}, {KEY_E, 'E'}, {KEY_R, 'R'}, {KEY_T, 'T'}, {KEY_Y, 'Y'}, {KEY_U, 'U'},
    {KEY_I, 'I'}, {KEY_O, 'O'}, {KEY_P, 'P'}, {KEY_A, 'A'}, {KEY_S, 'S'}, {KEY_D, 'D'}, {KEY_F, 'F'},
    {KEY_G, 'G'}, {KEY_H, 'H'}, {KEY_J, 'J'}, {KEY_K, 'K'}, {KEY_L, 'L'}, {KEY_Z, 'Z'}, {KEY_X, 'X'},
    {KEY_C, 'C'}, {KEY_V, 'V'}, {KEY_B, 'B'}, {KEY_N, 'N'}, {KEY_M, 'M'},
};

const VkMapEntry kDigitMap[] = {
    {KEY_1, '1'}, {KEY_2, '2'}, {KEY_3, '3'}, {KEY_4, '4'}, {KEY_5, '5'},
    {KEY_6, '6'}, {KEY_7, '7'}, {KEY_8, '8'}, {KEY_9, '9'}, {KEY_0, '0'},
};

template <size_t N>
int lookup(const VkMapEntry (&table)[N], unsigned int code) {
    for (size_t i = 0; i < N; ++i) {
        if (table[i].code == code) return table[i].vk;
    }
    return 0;
}

int linuxKeyToVk(unsigned int code) {
    const int mapIds[] = {lookup(kVkMap, code), lookup(kLetterMap, code), lookup(kDigitMap, code)};
    for (int vk : mapIds) {
        if (vk) return vk;
    }
    if (code >= KEY_F1 && code <= KEY_F12) return 0x70 + int(code - KEY_F1);
    if (code >= KEY_KP1 && code <= KEY_KP9) return 0x61 + int(code - KEY_KP1);
    if (code == KEY_KP0) return 0x60;
    // 未识别：退回 X keycode（Linux 码 + 8），配置里可以直接写 keycode
    return int(code) + 8;
}

// 鼠标按钮 → 程序内部按钮号（1 左 / 2 中 / 3 右 / 4,5,6 侧键）
int linuxButtonToButton(unsigned int code) {
    switch (code) {
        case BTN_LEFT: return 1;
        case BTN_MIDDLE: return 2;
        case BTN_RIGHT: return 3;
        case BTN_SIDE: return 4;
        case BTN_EXTRA: return 5;
        case BTN_FORWARD: return 6;
        case BTN_BACK: return 7;
        default: return 0;  // BTN_TOUCH 等触摸类不当作鼠标按键
    }
}

// 便于自检/排查：允许把设备目录指到别处（默认 /dev/input）
std::string deviceDir() {
    const char* env = std::getenv("FXPET_EVDEV_DIR");
    return (env && *env) ? std::string(env) : std::string("/dev/input");
}

std::string deviceName(int fd) {
    char name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) return std::string();
    return std::string(name);
}

}  // namespace

EvdevInput::~EvdevInput() { stop(); }

std::string EvdevInput::summary() const {
    std::lock_guard<std::mutex> lock(summaryMutex_);
    return summary_;
}

bool EvdevInput::start(const EvdevCallbacks& callbacks, std::string* error) {
    if (running_) return true;
    callbacks_ = callbacks;
    stopFlag_ = false;

    // 先建监听再扫描：这样“程序先启动、用户后补授权/插设备”也能自动接上
    inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd_ >= 0 &&
        inotify_add_watch(inotifyFd_, deviceDir().c_str(), IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVED_TO) < 0) {
        logLine("evdev: inotify 无法监视 %s（%s），热插拔与后补授权将不会被感知", deviceDir().c_str(),
                std::strerror(errno));
        close(inotifyFd_);
        inotifyFd_ = -1;
    }

    const bool haveDevices = rescan();
    if (!haveDevices && inotifyFd_ < 0) {
        if (error) {
            *error = "没有可读的 /dev/input/event* 设备，且无法监视设备目录（设备列表原因见日志）";
        }
        return false;
    }

    running_ = true;
    thread_ = std::thread([this] { run(); });
    if (error) error->clear();
    return true;
}

bool EvdevInput::rescan() {
    closeDevices();

    int denied = 0;
    int candidates = 0;
    int unusable = 0;
    int nextId = 0;
    std::string found;
    std::string signature;
    std::vector<int> heldKeys;     // 重新扫描后仍按住的物理键来源（拔插键盘/丢事件后自愈）
    std::vector<int> heldButtons;  // 仍按住的鼠标按钮

    DIR* dir = opendir(deviceDir().c_str());
    if (!dir) {
        logLine("evdev: 无法打开 %s（%s）", deviceDir().c_str(), std::strerror(errno));
        {
            std::lock_guard<std::mutex> lock(summaryMutex_);
            summary_ = "0 个设备（目录不可读）";
        }
        hasDevices_ = false;
        hasPointer_ = false;
        if (callbacks_.reset) callbacks_.reset();
        if (callbacks_.devicesChanged) callbacks_.devicesChanged(false, false);
        return false;
    }
    while (dirent* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) continue;
        ++candidates;
        const std::string path = deviceDir() + "/" + entry->d_name;
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES || errno == EPERM) ++denied;
            continue;
        }

        unsigned long evBits[(EV_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0) {
            close(fd);
            continue;
        }

        Device dev;
        dev.fd = fd;
        dev.id = nextId++;
        dev.path = path;
        dev.name = deviceName(fd);

        if (testBit(evBits, EV_KEY)) {
            unsigned long keyBits[(KEY_CNT / (8 * sizeof(unsigned long))) + 1] = {0};
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
                dev.keyboard = testBit(keyBits, KEY_A) || testBit(keyBits, KEY_Z) || testBit(keyBits, KEY_SPACE) ||
                               testBit(keyBits, KEY_ENTER);
                dev.buttons = testBit(keyBits, BTN_LEFT) || testBit(keyBits, BTN_RIGHT) || testBit(keyBits, BTN_MIDDLE) ||
                              testBit(keyBits, BTN_TOUCH);
            }
        }
        if (testBit(evBits, EV_REL)) {
            unsigned long relBits[(REL_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
            if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits) >= 0) {
                dev.motion = testBit(relBits, REL_X) && testBit(relBits, REL_Y);
            }
        }
        // 只有“直接定位”设备（触摸屏 / 数位板）才把 ABS_X/ABS_Y 当屏幕绝对坐标；
        // 触摸板、摇杆也导出 ABS_X/ABS_Y，但那是设备面内的坐标，映射到整屏会乱跳。
        if (testBit(evBits, EV_ABS)) {
            unsigned long propBits[(INPUT_PROP_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
            const bool direct = ioctl(fd, EVIOCGPROP(sizeof(propBits)), propBits) >= 0 &&
                                testBit(propBits, INPUT_PROP_DIRECT);
            input_absinfo ax{};
            input_absinfo ay{};
            if (direct && ioctl(fd, EVIOCGABS(ABS_X), &ax) >= 0 && ioctl(fd, EVIOCGABS(ABS_Y), &ay) >= 0 &&
                ax.maximum > ax.minimum && ay.maximum > ay.minimum) {
                dev.absolute = true;
                dev.absXMin = ax.minimum;
                dev.absXMax = ax.maximum;
                dev.absYMin = ay.minimum;
                dev.absYMax = ay.maximum;
            }
        }

        if (!dev.keyboard && !dev.buttons && !dev.motion && !dev.absolute) {
            ++unusable;
            close(fd);
            continue;
        }

        char desc[256];
        std::snprintf(desc, sizeof(desc), "%s%s%s%s", dev.keyboard ? "键盘" : "", dev.buttons ? "按钮" : "",
                      dev.motion ? "相对移动" : "", dev.absolute ? "绝对坐标" : "");
        logLine("evdev: %s「%s」→ %s", path.c_str(), dev.name.c_str(), desc);
        if (!found.empty()) found += ", ";
        found += dev.name.empty() ? path : dev.name;
        signature += path + ":" + desc + ";";

        // 记录当前按住的键：rescan 后补发 KeyDown，避免“按住 Ctrl 时插拔键盘”导致状态丢失
        if (dev.keyboard) {
            unsigned long keyBits[(KEY_CNT / (8 * sizeof(unsigned long))) + 1] = {0};
            if (ioctl(fd, EVIOCGKEY(sizeof(keyBits)), keyBits) >= 0) {
                for (unsigned int code = 0; code < KEY_CNT; ++code) {
                    if (!testBit(keyBits, code)) continue;
                    if (const int button = linuxButtonToButton(code)) {
                        if (dev.buttons) heldButtons.push_back(button);
                    } else if (code < BTN_MISC && dev.keyboard) {
                        heldKeys.push_back((dev.id << 16) | int(code));
                    }
                }
            }
        }

        devices_.push_back(dev);
    }
    closedir(dir);

    bool havePointer = false;
    for (const Device& dev : devices_) {
        if (dev.motion || dev.absolute) havePointer = true;
    }
    {
        std::lock_guard<std::mutex> lock(summaryMutex_);
        summary_ = found;
    }
    hasDevices_ = !devices_.empty();
    hasPointer_ = havePointer;

    if (devices_.empty()) {
        if (candidates == 0) {
            logLine("evdev: %s 下没有 event* 设备", deviceDir().c_str());
            std::lock_guard<std::mutex> lock(summaryMutex_);
            summary_ = "0 个设备";
        } else if (denied > 0) {
            logLine(
                "evdev: 没有读 %d 个输入设备的权限（EACCES）。运行 tools/setup_wayland_input.sh uaccess"
                "（推荐，会话级、可撤销）或 tools/setup_wayland_input.sh group（加入 input 组，需重新登录）",
                denied);
            std::lock_guard<std::mutex> lock(summaryMutex_);
            summary_ = "0 个可读设备（权限不足）";
        } else if (unusable > 0) {
            logLine("evdev: %d 个 event 设备里没有可用的键盘/鼠标", unusable);
            std::lock_guard<std::mutex> lock(summaryMutex_);
            summary_ = "0 个可用的键盘/鼠标";
        } else {
            logLine("evdev: %d 个 event 设备都打不开（原因见上）", candidates);
            std::lock_guard<std::mutex> lock(summaryMutex_);
            summary_ = "0 个可用设备";
        }
    }

    const bool changed = signature != signature_;
    signature_ = signature;
    // 每次重扫都重新对齐一次状态：先清空上层的按键/按钮，再按 EVIOCGKEY 播种实际
    // 按住的键。这样热插拔、SYN_DROPPED、以及重开 fd 期间丢失的事件都能自愈，
    // 也允许 dev.id 在每次扫描时重新分配（否则残留计数会让修饰键永远等不到 KeyUp）。
    if (callbacks_.reset) callbacks_.reset();
    if (changed && callbacks_.devicesChanged) callbacks_.devicesChanged(hasDevices_, hasPointer_);
    if (changed && hasDevices_) logLine("evdev: 已接管 %s", summary().c_str());
    for (int source : heldKeys) {
        if (callbacks_.key) callbacks_.key(linuxKeyToVk(unsigned(source & 0xffff)), true, source);
    }
    for (int button : heldButtons) {
        if (callbacks_.button) callbacks_.button(button, true);
    }
    return hasDevices_;
}

void EvdevInput::closeDevices() {
    for (Device& dev : devices_) {
        if (dev.fd >= 0) close(dev.fd);
    }
    devices_.clear();
}

void EvdevInput::drainInotify() {
    if (inotifyFd_ < 0) return;
    char buf[4096];
    while (true) {
        const ssize_t n = read(inotifyFd_, buf, sizeof(buf));
        if (n <= 0) break;
    }
}

void EvdevInput::run() {
    std::vector<pollfd> pfds;
    while (!stopFlag_) {
        pfds.clear();
        if (inotifyFd_ >= 0) pfds.push_back(pollfd{inotifyFd_, POLLIN, 0});
        for (const Device& dev : devices_) pfds.push_back(pollfd{dev.fd, POLLIN, 0});

        const int ready = poll(pfds.data(), nfds_t(pfds.size()), 200);
        if (ready < 0) {
            if (errno == EINTR) continue;
            logLine("evdev: poll 失败：%s", std::strerror(errno));
            break;
        }
        if (ready == 0) continue;

        size_t index = 0;
        if (inotifyFd_ >= 0) {
            if (pfds[0].revents & POLLIN) {
                drainInotify();
                needRescan_ = true;
            }
            index = 1;
        }
        bool rescanNeeded = false;
        for (size_t i = 0; i < devices_.size(); ++i, ++index) {
            const short revents = pfds[index].revents;
            if (!revents) continue;
            if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                rescanNeeded = true;
                continue;
            }
            if (revents & POLLIN) readDevice(devices_[i]);
        }

        if (needRescan_ || rescanNeeded) {
            needRescan_ = false;
            rescan();
        }
    }
    closeDevices();
    running_ = false;
}

void EvdevInput::readDevice(Device& dev) {
    input_event events[kMaxEventsPerRead];
    int dx = 0;
    int dy = 0;
    while (true) {
        const ssize_t bytes = read(dev.fd, events, sizeof(events));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == ENODEV) needRescan_ = true;
            break;
        }
        if (bytes == 0) break;
        const size_t count = size_t(bytes) / sizeof(input_event);
        for (size_t i = 0; i < count; ++i) {
            const input_event& ev = events[i];
            if (ev.type == EV_SYN) {
                // 客户端队列被打满时内核会发 SYN_DROPPED，其后到 SYN_REPORT 之间的事件是残缺的
                if (ev.code == SYN_DROPPED) {
                    dev.dropUntilSync = true;
                    needRescan_ = true;  // 重扫会清空并按 EVIOCGKEY 重建按键状态
                } else if (ev.code == SYN_REPORT) {
                    dev.dropUntilSync = false;
                }
                continue;
            }
            if (dev.dropUntilSync) continue;

            if (ev.type == EV_KEY) {
                const int button = linuxButtonToButton(unsigned(ev.code));
                if (button) {
                    if (dev.buttons && callbacks_.button) callbacks_.button(button, ev.value != 0);
                    continue;
                }
                if (dev.keyboard && ev.value >= 0 && ev.value <= 2) {
                    if (ev.value == 2) continue;  // 自动重复：状态没变，上层不需要
                    if (callbacks_.key) {
                        const int vk = linuxKeyToVk(unsigned(ev.code));
                        callbacks_.key(vk, ev.value != 0, (dev.id << 16) | int(ev.code));
                    }
                }
            } else if (ev.type == EV_REL) {
                if (!dev.motion) continue;
                if (ev.code == REL_X) dx += ev.value;
                else if (ev.code == REL_Y) dy += ev.value;
            } else if (ev.type == EV_ABS) {
                if (!dev.absolute) continue;
                if (ev.code == ABS_X) {
                    const double nx = double(ev.value - dev.absXMin) / double(dev.absXMax - dev.absXMin);
                    if (callbacks_.absolute) callbacks_.absolute(0, nx);
                } else if (ev.code == ABS_Y) {
                    const double ny = double(ev.value - dev.absYMin) / double(dev.absYMax - dev.absYMin);
                    if (callbacks_.absolute) callbacks_.absolute(1, ny);
                }
            }
        }
    }
    if ((dx || dy) && callbacks_.motion) callbacks_.motion(dx, dy);
}

void EvdevInput::stop() {
    stopFlag_ = true;
    if (thread_.joinable()) thread_.join();
    if (inotifyFd_ >= 0) {
        close(inotifyFd_);
        inotifyFd_ = -1;
    }
    running_ = false;
}

}  // namespace fx
