// 配置解析、路径解析、状态持久化。
#pragma once

#include <string>
#include <vector>

namespace fx {

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

using KeyGroup = std::vector<int>;  // 一组虚拟键码（任一命中即触发）

struct Config {
    // ---- 窗口 / 装饰（对应 config.json 的 decoration 段）
    float scalarX = 1.0f;
    float scalarY = 1.0f;
    int windowW = 612;
    int windowH = 354;
    int framerate = 60;
    bool topWindow = true;
    bool leftHanded = true;
    // Linux 版语义：l2dCorrect 是相对“铺满窗口”的缩放系数（1.0 = 正好铺满）
    float l2dCorrect = 1.0f;
    bool l2dFlipX = false;
    Vec2f l2dOffset;
    Vec2f handOffset;
    // ---- 模式
    int mode = 0;  // 0 = 标准（Live2D）
    bool standardL2d = true;
    bool standardMouse = true;
    bool mouseForceMove = true;
    float mouseSpeed = 1.0f;
    std::vector<KeyGroup> standardHand;     // 键位 → img/standard/hand/<i>.png
    std::vector<KeyGroup> standardFace;
    std::vector<KeyGroup> mouseLeft;
    std::vector<KeyGroup> mouseRight;
    std::vector<KeyGroup> mouseSide;
    // ---- 按键时隐藏的模型部件（静止键盘手），留空则不隐藏
    std::string handPart;
    std::vector<int> handDrawables = {37};  // 37 = 静态键盘手；42 = 鼠标手，必须保留
    // ---- 水印（免费版按住 Ctrl 隐藏）
    // 始终隐藏水印（不再需要按住 Ctrl）
    bool hideWatermark = true;
    // 这个模型里 Param24 = 1 时水印隐藏、= 0 时显示
    std::string watermarkParam = "Param24";
    float watermarkOn = 0.0f;
    float watermarkOff = 1.0f;

    // ---- 运行时路径
    std::string baseDir;    // 可执行文件所在目录
    std::string assetDir;   // 含 img/ 的目录
    std::string configPath;
    std::string statePath;
    std::string logPath;
};

struct State {
    Rect window{0, 0, 0, 0};  // w/h 为 0 表示用配置里的默认值
    bool locked = false;
    bool visible = true;
};

// 解析 baseDir/assetDir/configPath/statePath（遵循 FXPET_* 环境变量）
void resolvePaths(Config& cfg);
// 读取 config.json，失败时保留默认值并返回错误信息
bool loadConfig(Config& cfg, std::string* error);
bool loadState(Config& cfg, State& st);
bool saveState(const Config& cfg, const State& st);
// 写日志到 ~/.local/state/fxpet/fxpet.log（无控制台时的排错手段）
void logLine(const char* fmt, ...);
std::string exeDir();

}  // namespace fx
