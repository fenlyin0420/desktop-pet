// 全局指针位置。
//
// X11 会话：位置直接问 X 服务器（XQueryPointer），准确。
// Wayland 会话：没有任何协议能让普通客户端查询全局指针坐标，因此这里用 evdev
// 的相对位移累积出一个估计坐标，并抓住一切“可信时刻”用 X 根坐标校正：
// 指针落在本程序窗口（XWayland 窗口）上时，XQueryPointer / 事件里的 root 坐标
// 就是真值。
#pragma once

namespace fx {

// 会话类型探测（XDG_SESSION_TYPE / WAYLAND_DISPLAY）
bool isWaylandSession();
const char* sessionTypeName();

// 在主线程、窗口创建之后调用。不启用累积跟踪时只记录屏幕尺寸与缩放。
void pointerInit(void* x11Display, int screenW, int screenH);
// evdev 全局监听就绪后调用，打开/关闭累积跟踪
void pointerSetTracking(bool on);
bool pointerTrackingActive();

// evdev 相对位移（设备像素）
void pointerFeedMotion(int dx, int dy);
// evdev 绝对坐标设备（触摸屏/数位板）：axis 0=X / 1=Y，值为归一化 0..1
void pointerFeedAbsoluteNorm(int axis, double value);
// 用可信的 X 根坐标校正（指针位于本程序窗口内时）。
// anchor = 这次校正能否当作“指针确实在那里”的锚点：只有真实指针动作（移动/按键）
// 才算；窗口自己移到指针底下产生的 EnterNotify 可能是 XWayland 的过期坐标，不算。
void pointerResync(int x, int y, bool anchor = true);
// 每帧调用一次：X 指针位置一变化就说明指针刚在 X11 窗口上，用它校正
void pointerPollX(void* x11Display);
// 一次取样：位置 + 是否可信（同一把锁里取，避免跨线程只对上一半）。
// 未启用累积跟踪时返回 false（调用方改用 XQueryPointer）
bool pointerSample(int& x, int& y, bool& trusted);
// 当前位置是否可信：X11 下恒为真；Wayland 下要等第一次位移/校正之后
// （启动时只有从 X 拿到的“上次位置”种子，可能已过期，不能当作光标在窗口里）
bool pointerPositionTrusted();
// 当前累积坐标（不论是否跟踪），调试用；valid 表示收到过位移或校正
void pointerDebugState(int& x, int& y, bool& valid);

}  // namespace fx
