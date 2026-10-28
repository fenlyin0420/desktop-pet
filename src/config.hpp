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
    bool physics = true;  // linux.physics：是否应用 physics3.json 物理演算
    Vec2f l2dOffset;
    Vec2f handOffset;
    // ---- 模式
    int mode = 0;  // 0 = 标准（Live2D）
    bool standardL2d = true;
    bool standardMouse = true;
    bool mouseForceMove = true;
    float mouseSpeed = 1.0f;
    std::vector<KeyGroup> standardHand;     // 键位 → hand/<i>.png
    std::vector<KeyGroup> standardFace;
    std::vector<KeyGroup> mouseLeft;
    std::vector<KeyGroup> mouseRight;
    std::vector<KeyGroup> mouseSide;
    // ---- 按键时隐藏的模型部件（静止键盘手），留空则不隐藏
    std::string handPart;
    std::vector<int> handDrawables = {37};  // 37 = 静态键盘手；42 = 鼠标手，必须保留

    // ---- 运行时路径
    std::string baseDir;    // 可执行文件所在目录
    std::string assetDir;   // 资源目录（模型与图片直接在其下）
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
// 在资源目录里找 model3.json（扁平布局：<dir>/<模型名>/<模型名>.model3.json）
std::string findModel3(const std::string& assetDir);

}  // namespace fx
