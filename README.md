# fxpet —— 绯雪桌宠

用 C++ 写的跨平台 Live2D 猫娘桌宠：Windows 使用 GLFW + WGL/OpenGL，Linux 使用 X11 + EGL/OpenGL ES 2.0。
只依赖系统图形/字体库，用 Live2D 官方 Cubism Core 直接驱动 moc3 模型。

`assets/` 下的图片、模型、音效资源来自原 Windows 版，仅供自用。

## 平台支持

| 能力 | Windows | Linux |
| --- | --- | --- |
| 无边框、置顶、透明窗口 | GLFW + DWM 逐像素 alpha | X11 ARGB visual + 合成器 |
| 鼠标穿透（锁定） | `WS_EX_TRANSPARENT` | XShape 输入区域置空 |
| 全局键鼠 | Raw Input（失败时退回低级钩子） | XInput2 raw / evdev |
| 全局指针位置 | `GetCursorPos`，精确 | X11 精确；Wayland 用 evdev 估算 + 校正 |
| 菜单 | 窗口内自绘 | 窗口内自绘 |
| 位置记忆 | `%APPDATA%\fxpet\state.ini` | `~/.config/fxpet/state.ini` |
| 日志 | `%LOCALAPPDATA%\fxpet\fxpet.log` | `~/.local/state/fxpet/fxpet.log` |

## 操作

| 操作 | 效果 |
| --- | --- |
| 左键拖动 | 移动 |
| 右键拖动 | 缩放（右下角为锚点） |
| 滚轮 | 缩放 |
| 右键单击 / 中键 | 菜单（锁定、隐藏、恢复大小、重载模型、退出） |
| `Ctrl+Shift+Alt+T` | 锁定 / 解锁。锁定后鼠标穿透，不挡操作 |
| `Ctrl+Shift+Alt+H` | 隐藏 / 显示 |
| `Ctrl+Shift+Alt+R` | 恢复默认大小 |
| `Ctrl+Shift+Alt+Q` | 退出 |

## Windows 构建

需要：

- Visual Studio 2022（Desktop development with C++）
- CMake 3.21+
- Git（CMake FetchContent 需要）
- 首次配置时能访问 GitHub（通过 CMake FetchContent 拉取 GLFW / GLEW / FreeType）

```powershell
tools\fetch_live2d_core.ps1       # 取 Windows 版 Cubism Core DLL，只需一次
cmake --preset windows
cmake --build --preset windows-release
.\build\windows\Release\fxpet.exe
```

输出目录里会自动带上 `assets/` 和 `lib/Live2DCubismCore.dll`。
没有 Core 时程序仍能启动，会退化为静态图 `cat.png`。

`tools/fetch_live2d_core.ps1` 从 Live2D 官方下载 SDK 压缩包并只取出
`Core/dll/windows/x86_64/Live2DCubismCore.dll`。Core 是 Live2D 的专有二进制，
仓库不包含它；下载即表示你接受 [Live2D 的许可条款](https://www.live2d.com/sdk/about/)。
也可以用本地已下载的 SDK：`tools\fetch_live2d_core.ps1 -SdkZip <SDK.zip>`。

## Linux 构建

运行与编译需要（Debian/Ubuntu 包名）：

```
libx11-dev libxtst-dev libxext-dev libegl1-mesa-dev libgles2-mesa-dev
libfreetype-dev libfontconfig-dev cmake g++ pkg-config
```

编译还需要内核头文件 `linux/input.h`（`linux-libc-dev` / `libc6-dev` 自带），不需要 libinput。

```bash
tools/fetch_live2d_core.sh          # 取 Linux 版 Cubism Core 静态库，只需一次
cmake --preset linux
cmake --build --preset linux-release
./build/linux/fxpet
```

Linux 运行时还需要一个能渲染 32 位 ARGB 窗口的**合成器**（GNOME、KDE 默认开启；
i3 等需自行开 picom）。

## 配置

`assets/config.json` 沿用原 Windows 版的字段，键位表 `standard.hand` 直接生效（VK 码）。
跨平台渲染字段在 `render` 段（同时兼容旧名字 `linux`）：

| 键 | 默认值 | 说明 |
| --- | --- | --- |
| `l2d_scale` | `1.0` | 模型缩放，`1.0` = 正好铺满窗口 |
| `l2d_flip_x` | `false` | 水平镜像模型（默认不镜像，否则模型自带的水印文字会反） |
| `physics` | `true` | 是否应用模型自带的 `physics3.json` 物理演算 |
| `hand_part` | `"Part50"` | 按键时隐藏的模型部件（模型自带的静止手）；留空则不隐藏 |
| `hand_drawables` | `"37"` | 按键时隐藏的 drawable 列表，逗号分隔 |

窗口位置与大小：

- Windows：`%APPDATA%\fxpet\state.ini`
- Linux：`~/.config/fxpet/state.ini`

字段：`window=x,y,w,h`、`locked`、`visible`。

## Windows 会话说明

- **透明**：需要 DWM 合成（Windows 8+ 默认开启）。窗口用 GLFW 的
  `GLFW_TRANSPARENT_FRAMEBUFFER`，四角是真正的逐像素 alpha；某些远程桌面/关闭 DWM 的
  环境拿不到逐像素透明时，程序会退化为 `SetWindowRgn` 硬边裁剪（`--shape` 可强制）。
- **置顶 / 不抢焦点**：`WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，
  不进任务栏、不进 Alt+Tab，点击桌宠不会把游戏/编辑器切走。
- **全局键鼠**：优先 Raw Input（`RIDEV_INPUTSINK`，失焦也能收到物理键鼠），
  Raw Input 不可用时退回 `WH_KEYBOARD_LL` / `WH_MOUSE_LL` 低级钩子。
- **已知限制**：前台是管理员权限窗口时，UIPI 可能挡住普通权限进程的全局输入；
  独占全屏游戏也可能拿不到。这与 Linux 下 Wayland 没授权是同一类问题。
  如果锁定后热键失灵，删掉 `state.ini` 里的 `locked=1` / `visible=0` 再重启。
- **DPI**：按 per-monitor DPI aware 运行，窗口尺寸、指针坐标都是物理像素；
  混合 DPI 多屏下建议把桌宠放在主屏。
- **SmartScreen**：未签名的 exe 首次运行可能提示“Windows 已保护你的电脑”，
  选择“更多信息 → 仍要运行”即可。

## Linux / Wayland 会话

程序本体仍然是一个 XWayland 客户端，窗口显示、拖动、点击穿透、菜单都可用，
但 Wayland 下没有“全局键鼠监听”的协议，`XInput2` raw 事件只覆盖 XWayland 自己的窗口，
所以按键手部动画和热键会失灵；也没有协议能让普通客户端查询全局指针坐标。

因此 Wayland 下直接读 `/dev/input/event*`（evdev），一次授权即可：

```bash
tools/setup_wayland_input.sh check     # 默认动作，只看现在能不能读
tools/setup_wayland_input.sh uaccess   # 推荐：装 udev uaccess 规则（会话级，可撤销）
# 或
tools/setup_wayland_input.sh group     # 把当前用户永久加入 input 组（需重新登录）
```

**授权范围**：uaccess 与 input 组都是给“该用户/该会话的所有程序”读键鼠的权限，
不是只给 fxpet；任何以你身份运行的程序都能读到按键。fxpet 自己只把这些事件用于动画，
不落盘、不外发。

行为与 X11 会话的差别：

| 项目 | X11 | Wayland + evdev |
| --- | --- | --- |
| 按键手部动画 / 热键 | XInput2 raw，精确 | evdev，精确（按物理键） |
| 鼠标左右键 / 侧键贴图 | 精确 | 精确 |
| 指针坐标 | `XQueryPointer`，精确 | 相对位移累积估算，经过 X11 窗口时用真值校正 |
| 滚轮缩放 | 窗口内可用 | 窗口内可用 |

指针坐标是估算，误差会随移动累积。Wayland 没有全局坐标接口，程序按 evdev 的相对位移
（除以缩放系数）累积一个位置，并在可信时刻用 X 根坐标覆盖回来。两次校正之间偏差可达
几十到几百像素；指针靠近屏幕中线时左右方向甚至可能判反，绿色指针擦过 X11 窗口或桌宠时
即归位。混合 DPI 的多屏只有一个全局缩放系数，只能取折中值。

可调项（环境变量）：

| 变量 | 说明 |
| --- | --- |
| `FXPET_POINTER_SCALE` | evdev 设备像素 → 逻辑像素的换算系数，**值越大跟随越慢**。留空时取 `Xft.dpi/96`；取不到时退化成 1.0 |
| `FXPET_EVDEV_DIR` | 设备目录，默认 `/dev/input`（自检/排查用） |

已知限制（Wayland）：

- 窗口定位、置顶、鼠标穿透都靠 XWayland 把 override-redirect 窗口交给合成器处理，
  合成器不保证照做（GNOME/KDE 上实测可用；某些平铺/精简合成器可能不认）。
- 拖动依赖 X core 指针抓取，某些合成器在抓取期间不一定持续投递 motion。
- HiDPI 下整个 XWayland 窗口由合成器缩放，画面可能发虚；非整数缩放时
  `Xft.dpi` 未必同步变化，需要手动设 `FXPET_POINTER_SCALE`。

## 渲染上的几个注意点

- **drawable 可见性只看 opacity**：这个 moc3 里 `csmIsVisible` 标志跟实际 opacity 对不上，
  信它会把刘海、花朵等部件整片吞掉。程序默认忽略该标志，仍按 opacity 渲染；
  `--respect-visible` 可以切回原行为做对比。
- **遮罩用离屏纹理 + 着色器裁剪**（Cubism 5 的做法）：把遮罩 drawable 画进一张 RGBA 纹理，
  绘制本体时按屏幕坐标采样遮罩 alpha 相乘，不再用模板缓冲。
- **按键时隐藏模型自带的静止手**：手部帧贴图和模型自己的手在同一位置，不隐藏就会出现两只手。
  隐藏哪个部件由 `render.hand_part` 指定，设成空字符串即关闭。
- **drawable 不透明度 = drawable × 父部件链**：Cubism 的部件显隐靠这一层链乘。
- **眼睛参数是常规语义**：`ParamEyeLOpen` / `ParamEyeROpen` 1 = 睁开、0 = 闭合。
- **参数必须在 `csmUpdateModel()` 之前写**：形变是 update 时算的。
- **物理演算也在 `csmUpdateModel()` 之前求值**：`physics3.json` 的输出写回参数。
- **颜色管线照抄官方 shader**：`tex.rgb * multiplyColor` 之后再做 screen 混合，
  最后预乘 alpha 输出。

## 开机自启

Windows（当前用户，注册表 Run）：

```powershell
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v FxPet /t REG_SZ /d "\"C:\path\to\fxpet.exe\"" /f
```

Linux：放入 `~/.config/autostart/fxpet.desktop`：

```ini
[Desktop Entry]
Type=Application
Name=绯雪桌宠
Exec=/absolute/path/to/fxpet
X-GNOME-Autostart-enabled=true
```

## 排错

- **Windows 透明区域发黑 / 边缘发硬**：检查 DWM 是否开启（远程桌面/精简系统可能关闭）。
  程序会在日志里写 `transparent=1/0`；不可用时自动退化为硬边裁剪。
- **Windows 全局按键没有反应**：看日志里的输入后端。默认用 Raw Input，失败时自动退回低级钩子；
  如果前台是管理员权限窗口或独占全屏游戏，可能被 UIPI 挡住。可以尝试强制低级钩子：
  `$env:FXPET_INPUT_BACKEND="hook"` 后启动程序（默认值 `raw`）。
- **Windows 跟随鼠标偏移**：多屏混合 DPI 下先确认桌宠在哪个屏；程序使用
  `GetCursorPos` 的物理坐标，窗口位置也来自 GLFW，二者应一致。
- **Linux 透明区域显示成黑色**：当前会话没有合成器，或驱动不支持 ARGB 视觉。
- **Wayland 下按键没有反应 / 不跟随鼠标**：evdev 没权限，运行
  `tools/setup_wayland_input.sh check` 看可读设备数；按上面的 `uaccess` / `group` 授权。
- **模型加载失败**：看日志，多数是 Core 版本与 moc3 不匹配，换新一点的 SDK
  （`CUBISM_SDK_VERSION=5-r.5 tools/fetch_live2d_core.sh` 或
  `tools\fetch_live2d_core.ps1 -Version 5-r.5`）。
- **调试工具**：`--shot out.png` 渲染一帧存图、`--menu` 打开菜单后截图、
  `--press <vk>` 模拟按键、`--dump` 打印模型参数/部件、`--debug-input` 打印全局键鼠事件、
  `--dump-frame out.png` 抓运行时画面、`--shape` 强制硬边裁剪、
  `--l2d-correct` 临时改缩放、`--no-physics` 关闭物理演算做对比。
  Windows 下加 `--console` 尝试附加控制台；也可以直接用
  `cmake -S . -B build/windows-console -DFXPET_CONSOLE=ON` 构建控制台版本。

## 尚未实现

- 手柄模式、键盘/钢琴模式
- 按键音效（`sounds/*.flac` 未接入音频输出）
- 双机网络同步（`config.json` 的 `network` 段）
- 表情与动作热键

## 目录

```
src/app/main.cpp        CLI、主循环
src/core/              平台无关：GL、Live2D、物理、Pet、菜单、文字、配置、JSON
src/platform/          窗口/输入/路径/动态库/字体接口
src/platform/linux/    X11 + EGL、XInput2、evdev、Wayland 指针估算
src/platform/windows/  GLFW + WGL、Raw Input、Win32 路径/字体/Core DLL
third_party/stb/       PNG 编解码（stb_image / stb_image_write）
tools/                 Core 下载脚本、Wayland 输入授权脚本
```
