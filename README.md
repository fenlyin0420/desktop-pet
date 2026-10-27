# fxpet —— 绯雪 Linux 桌宠

把 Windows 版的绯雪桌宠（Live2D 猫娘）重写为 Linux 原生程序：X11 + EGL/OpenGL ES 2.0，
只依赖系统图形/字体库，用 Live2D 官方 Cubism Core 直接驱动 moc3 模型。

打包在 `assets/` 下的图片、模型、音效资源来自原 Windows 版本，仅供自用。

## 与 Windows 版的对应关系

| Windows 版 | Linux 版 |
| --- | --- |
| 无边框置顶 WPF 窗口 | X11 override-redirect 窗口 + `_NET_WM_STATE_ABOVE`，32 位 ARGB 视觉 |
| 低级键盘钩子 | XRecord 全局键鼠监听（不需要 root，X11 会话） |
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
- **按键没有反应**：程序用 XRecord 监听全局输入，Wayland 会话不支持，请切到 X11 会话；
  日志里会写明启动失败原因。
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
src/global_input.* XRecord 全局键鼠 → 与 Windows 版一致的 VK 码
src/pet.*        按键→手部帧、鼠标跟随、拖拽缩放、热键
src/menu.*       窗口内菜单
src/text.*       freetype + fontconfig 文字
```
