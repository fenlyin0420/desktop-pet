# fxpet —— 绯雪 Linux 桌宠

把 Windows 版的绯雪桌宠（Live2D 猫娘）重写为 Linux 原生程序：X11 + EGL/OpenGL ES 2.0，
只依赖系统图形/字体库，用 Live2D 官方 Cubism Core 直接驱动 moc3 模型。

打包在 `assets/` 下的图片、模型、音效资源来自原 Windows 版本，仅供自用。

## 与 Windows 版的对应关系

| Windows 版 | Linux 版 |
| --- | --- |
| 无边框置顶 WPF 窗口 | X11 override-redirect 窗口 + `_NET_WM_STATE_ABOVE`，32 位 ARGB 视觉 |
| 低级键盘钩子 | 全局键鼠监听：X11 用 XInput2 raw；Wayland 用 evdev 直读（见 [Wayland 会话](#wayland-会话)） |
| 按键 → 手部帧贴图 | 同一套 `config.json` 键位表，映射到 `img/standard/hand/*.png` |
| 按住 Ctrl 取消水印 | 按住 Ctrl 隐藏模型水印参数 |
| `Ctrl+Shift+T` 锁定 | 相同热键，锁定 = 鼠标穿透（XShape 输入区域置空） |
| 右键托盘图标菜单 | 窗口内自绘菜单（右键单击 / 中键） |
| 位置记忆工具 | 退出与拖动时自动写入 `~/.config/fxpet/state.ini` |

## 依赖

运行与编译需要（Debian/Ubuntu 包名）：

```
libx11-dev libxtst-dev libxext-dev libegl1-mesa-dev libgles2-mesa-dev
libpng-dev libfreetype-dev libfontconfig-dev cmake g++ pkg-config
```

编译还需要内核头文件 `linux/input.h`（`linux-libc-dev` / `libc6-dev` 自带），不需要 libinput。

运行时还需要一个能渲染 32 位 ARGB 窗口的**合成器**（GNOME、KDE 默认开启；i3 等需自行开 picom）。

## 编译

```bash
tools/fetch_live2d_core.sh          # 取 Live2D Cubism Core（Linux 静态库），只需一次
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/fxpet
```

`tools/fetch_live2d_core.sh` 从 Live2D 官方下载 SDK 压缩包并只取出
`Core/lib/linux/x86_64/libLive2DCubismCore.a`。Core 是 Live2D 的专有二进制，
仓库不包含它；下载即表示你接受 [Live2D 的许可条款](https://www.live2d.com/sdk/about/)。
也可以用本地已下载的 SDK：`tools/fetch_live2d_core.sh <SDK.zip>`，
或把 `.so` 放到 `lib/`（此时运行时 dlopen，无需重新编译）。

没有 Core 时程序仍能启动，会退化为静态图 `img/standard/cat.png`。

## 操作

| 操作 | 效果 |
| --- | --- |
| 左键拖动 | 移动 |
| 右键拖动 | 缩放（右下角为锚点） |
| 滚轮 | 缩放 |
| 右键单击 / 中键 | 菜单（锁定、隐藏、恢复大小、重载模型、退出） |
| `Ctrl+Shift+T` | 锁定 / 解锁。锁定后鼠标穿透，不挡操作 |
| `Ctrl+Shift+H` | 隐藏 / 显示 |
| `Ctrl+Shift+R` | 恢复默认大小 |
| `Ctrl+Shift+Q` | 退出 |
| 按住 `Ctrl` | 隐藏水印 |

## Wayland 会话

程序本体仍然是一个 XWayland 客户端，窗口显示、拖动、点击穿透、菜单都可用，
但两件事取决于合成器与权限（见下面的「已知限制」），并且 **Wayland 下拿不到全局键鼠**：

- 没有“全局键鼠监听”的协议，`XInput2` raw 事件只覆盖 XWayland 自己的窗口，
  所以按键手部动画、`Ctrl+Shift+*` 热键都会失灵；
- 也没有任何协议能让普通客户端查询**全局指针坐标**，眼睛/头部跟随鼠标就没了数据源。

因此 Wayland 下改成直接读 `/dev/input/event*`（evdev），一次授权即可：

```bash
tools/setup_wayland_input.sh check     # 默认动作，只看现在能不能读
tools/setup_wayland_input.sh uaccess   # 推荐：装 udev uaccess 规则（会话级，可撤销）
# 或
tools/setup_wayland_input.sh group     # 把当前用户永久加入 input 组（需重新登录）
```

之后日志（`~/.local/state/fxpet/fxpet.log`）应出现：

```text
输入后端：evdev（会话 wayland）
evdev: /dev/input/event3「AT Translated Set 2 keyboard」→ 键盘
evdev: 已接管 ...键盘..., ...鼠标...
指针跟踪：会话=wayland 屏幕=...x... 缩放=1.00（Xft.dpi） ...
```

程序先启动、之后才授权也没关系：一旦权限到位（udev 规则立刻生效）或插上新设备，
运行中的实例会自动接管，不必重启。

**授权范围要说清楚**：uaccess 与 input 组都是给“该用户/该会话的所有程序”读键鼠的权限，
不是只给 fxpet；这也意味着任何以你身份运行的程序都能读到按键。uaccess 只对当前活动的
本地会话生效（注销/切换用户即失效，删规则即可撤销），input 组则是永久的账号级改动。
fxpet 自己只把这些事件用于动画，不落盘、不外发，但能不能接受需要你自己权衡。

⚠ 在 evdev 还没授权之前，**不要按 `Ctrl+Shift+T` 锁定或 `Ctrl+Shift+H` 隐藏**：锁定会把窗口的
输入区域置空，而解除锁定只靠这两个热键；热键不通就只剩改配置这一条路
（删掉 `~/.config/fxpet/state.ini` 里的 `locked=1` / `visible=0` 后重启）。

行为与 X11 会话的差别：

| 项目 | X11 | Wayland + evdev |
| --- | --- | --- |
| 按键手部动画 / 热键 | XInput2 raw，精确 | evdev，精确（按物理键） |
| 鼠标左右键 / 侧键贴图 | 精确 | 精确 |
| 指针坐标 | `XQueryPointer`，精确 | 相对位移累积估算，**指针经过桌宠窗口或任何 XWayland 窗口时**用真值校正 |
| 滚轮缩放 | 窗口内可用 | 窗口内可用 |

指针坐标是估算，误差会随移动累积：Wayland 没有全局坐标接口，程序按 evdev 的相对位移
（除以缩放系数）累积一个位置，并在可信时刻用 X 根坐标覆盖回来——指针落到桌宠窗口上
（进入 / 移动 / 点击）时，以及 XWayland 的指针位置自身发生变化时（说明指针刚经过某个
X11 窗口）。两次校正之间偏差可达几十到几百像素（取决于缩放系数与指针加速是否一致）：
偏差期间头部朝向、鼠标贴图会整体偏移，指针靠近屏幕中线时左右方向甚至可能判反，
绿色指针擦过 X11 窗口或桌宠时即归位。混合 DPI 的多屏只有一个全局缩放系数，只能取折中值。

可调项（环境变量）：

| 变量 | 说明 |
| --- | --- |
| `FXPET_POINTER_SCALE` | evdev 设备像素 → 逻辑像素的换算系数，**值越大跟随越慢**。留空时取 `Xft.dpi/96`（XWayland 下这个值由桌面环境按缩放写进 X 资源：GNOME/KDE 会设，wlroots 系通常不设，200% 缩放 ≈ 2.0）；跟随偏快就调大、偏慢就调小。取不到时退化成 1.0，日志里会写明来源 |
| `FXPET_EVDEV_DIR` | 设备目录，默认 `/dev/input`（自检/排查用） |

另外：X11 会话下如果 XInput2 也起不来，程序会退到 evdev 读键盘/按钮，但指针位置仍然
以 `XQueryPointer` 为准（不使用估算）。`--debug-input` 每 30 帧会打印一行
`pointer motion_seen=0|1 (x,y) trusted=0|1 tracking=0|1 backend=evdev|XInput2`，用来确认走的是
哪条路径。`trusted=1` 表示已经拿到过可信锚点（指针进过桌宠窗口或别的 X11 窗口）：
没锚定前坐标只是从启动种子累加的估算，自绘光标不会画。

已知限制（Wayland）：

- 窗口定位、置顶、鼠标穿透都靠 XWayland 把 override-redirect 窗口交给合成器处理，
  合成器不保证照做（GNOME/KDE 上实测可用；某些平铺/精简合成器可能不认）。
- 拖动依赖 X core 指针抓取，某些合成器在抓取期间不一定持续投递 motion；程序用每帧
  「按全局坐标重算」兜底，但拖动可能比 X11 会话略钝。
- HiDPI 下整个 XWayland 窗口由合成器缩放，画面可能发虚；非整数缩放（125%/150%）时
  `Xft.dpi` 未必同步变化，需要手动设 `FXPET_POINTER_SCALE`。
- 没有 `INPUT_PROP_DIRECT` 的触摸屏（只报 `ABS_MT_POSITION_*`）不会参与指针估算；
  触摸板不是绝对指针源，也不会被误用。

## 配置

`assets/config.json` 沿用 Windows 版的字段，键位表 `standard.hand` 直接生效（VK 码）。
Linux 版特有的字段在 `linux` 段：

| 键 | 默认值 | 说明 |
| --- | --- | --- |
| `l2d_scale` | `1.0` | 模型缩放，`1.0` = 正好铺满窗口；Windows 版的 `decoration.l2d_correct` 语义不同，不沿用 |
| `l2d_flip_x` | `false` | 水平镜像模型（默认不镜像，否则模型自带的水印文字会反） |
| `hand_part` | `"Part50"` | 按键时隐藏的模型部件（模型自带的静止手）；留空则不隐藏 |
| `watermark_param` | `"Param24"` | 水印参数 id |
| `watermark_on` / `watermark_off` | `0.0` / `1.0` | 水印显示 / 隐藏时该参数取值（这个模型是 `Param24=1` 时隐藏） |

窗口位置与大小在 `~/.config/fxpet/state.ini`：`window=x,y,w,h`、`locked`、`visible`。
日志在 `~/.local/state/fxpet/fxpet.log`。

## 渲染上的几个注意点

- **drawable 可见性只看 opacity**：这个 moc3 里 `csmIsVisible` 标志跟实际 opacity 对不上，
  信它会把刘海、花朵等部件整片吞掉（表现为脸盖在头发上）。程序默认忽略该标志，
  仍按 opacity 渲染；`--respect-visible` 可以切回原行为做对比。
- **遮罩用离屏纹理 + 着色器裁剪**（Cubism 5 的做法）：把遮罩 drawable 画进一张 RGBA 纹理，
  绘制本体时按屏幕坐标采样遮罩 alpha 相乘，不再用模板缓冲。
- **按键时隐藏模型自带的静止手**：手部帧贴图和模型自己的手在同一位置，不隐藏就会出现两只手。
  隐藏哪个部件由 `linux.hand_part` 指定（这台机器上是 `Part50`），设成空字符串即关闭。
- **drawable 不透明度 = drawable × 父部件链**：Cubism 的部件显隐靠这一层链乘，漏掉的话
  `--part` 一类的控制完全不生效（也就是上面那条静止手隐藏会失效）。
- **眼睛参数是常规语义**：`ParamEyeLOpen` / `ParamEyeROpen` 1 = 睁开、0 = 闭合（眨眼动画写 `blink`）。
- **Core 的反向遮罩标志在本模型上与画面不符**：按标志取反会把虹膜裁没（表现为眼睛贴图错位、
  眼白一大片）。现在默认忽略该标志、一律按普通遮罩处理（`alpha *= maskAlpha`），
  与官方 SDK 的渲染结果一致；`--masks-invert` 可切回按标志处理做对比。
- **参数必须在 `csmUpdateModel()` 之前写**：形变是 update 时算的，update 之后再写只影响
  不透明度类参数，看起来就像“参数没反应”。调试用的 `--param` / `--eye-open` 现在是钉在 update 前写入。
- **自绘光标只在“指针确实位于桌宠窗口内且位置可信”时绘制**：窗口外本来就被裁掉看不见，
  而启动时指针坐标可能只有一个过期的种子（Wayland 下尤其），照画会得到一颗钉在窗口里的
  幽灵光标；指针真的在窗口里动过（或经过别的 X11 窗口）之后才恢复正常。锁定（鼠标穿透）
  状态下不画自绘光标——那时窗口不再接收指针，系统光标本来就正常显示。
- **颜色管线照抄官方 shader**：`tex.rgb * multiplyColor` 之后还要做一次 screen 混合
  （`rgb + screen - rgb*screen`），最后再预乘 alpha 输出。只做 multiply 会让靠屏幕色上色的
  图层（比如眼睛高光）发灰。

## 开机自启

放入 `~/.config/autostart/fxpet.desktop`：

```ini
[Desktop Entry]
Type=Application
Name=绯雪桌宠
Exec=/absolute/path/to/fxpet
X-GNOME-Autostart-enabled=true
```

## 排错

- **透明区域显示成黑色**：当前会话没有合成器，或驱动不支持 ARGB 视觉。开启桌面合成
  （GNOME/KDE 默认开启，i3 用 picom）；实在没有合成器时程序会退化为硬边裁剪。
- **Wayland 下按键没有反应 / 不跟随鼠标**：evdev 没权限。运行
  `tools/setup_wayland_input.sh check` 看可读设备数（脚本也会列设备名），`0/22` 就按上面的
  `uaccess` / `group` 授权；日志里会写明 `没有读 N 个输入设备的权限（EACCES）`。
  授权后运行中的实例会自动接管，日志出现 `evdev: 已接管 ...` 即可。
- **Wayland 下跟随鼠标偏快/偏慢**：`FXPET_POINTER_SCALE` 微调，值越大越慢（见上）；
  日志里 `缩放=...` 会写出它取的是 `Xft.dpi` 还是默认 1.0。
- **Wayland 下日志有设备但没有「键盘」标记**：说明只读到了电源键/手柄一类的设备，
  键盘节点仍不可读，回到上一条检查权限。
- **X11 会话下按键没有反应**：日志里会写明 XInput2 启动失败原因。
- **模型加载失败**：看 `~/.local/state/fxpet/fxpet.log`，多数是 Core 版本与 moc3 不匹配，
  换新一点的 SDK（`CUBISM_SDK_VERSION=5-r.5 tools/fetch_live2d_core.sh`）。
- **调试工具**：`--shot out.png` 渲染一帧存图、`--press <vk>` 模拟按键、`--dump` 打印
  模型参数/部件、`--debug-input` 打印全局键鼠事件、`--dump-frame out.png` 抓运行时画面、
  `--shape` 强制硬边裁剪、`--l2d-correct` 临时改缩放。

## 尚未实现（相对 Windows 版）

- 手柄模式、键盘/钢琴模式（免费版本身也没有这些素材的完整逻辑）
- 按键音效（`img/*/sounds/*.flac` 未接入音频输出）
- 双机网络同步（`config.json` 的 `network` 段）
- 表情与动作热键（免费版模型没有 motion3/exp3 之外的可用表情）

## 目录

```
src/window.*     X11 + EGL 窗口、裁剪形状、点击穿透
src/gl.*         GLES2 渲染（预乘 alpha、任意三角网格、FBO、模板遮罩）
src/l2d_core.*   Cubism Core 装载（静态链接或运行时 dlopen）
src/l2d_model.*  moc3 模型：参数、部件、drawable、遮罩、按 renderOrder 绘制
src/global_input.* 全局键鼠：X11 用 XInput2 raw / Wayland 用 evdev → 统一的 VK 码
src/evdev_input.*  直接读 /dev/input/event*（Wayland 下的全局键鼠，含热插拔）
src/pointer.*      全局指针位置：X11 直接查询，Wayland 用 evdev 相对位移累积 + 校正
src/pet.*        按键→手部帧、鼠标跟随、拖拽缩放、热键
src/menu.*       窗口内菜单
src/text.*       freetype + fontconfig 文字
```
