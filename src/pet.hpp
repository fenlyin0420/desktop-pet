// 桌宠本体：资源加载、按键→手部帧映射、Live2D 表现、拖拽/缩放/菜单。
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "gl.hpp"
#include "l2d_core.hpp"
#include "l2d_model.hpp"
#include "menu.hpp"
#include "text.hpp"
#include "window.hpp"

namespace fx {

struct InputEvent;
class GlobalInput;

class Pet {
public:
    bool init(Config& cfg, State& st, GLContext& gl, TextRenderer& text, Window& win, std::string* error);
    void shutdown(GLContext& gl);

    void handleWindowEvents(const std::vector<WinEvent>& events);
    void handleGlobalInput(std::vector<InputEvent>& events, const GlobalInput& input);
    void frame(float dt, int winW, int winH, GLContext& gl);

    State& state() { return *state_; }
    bool quitRequested() const { return quit_; }
    void debugPress(int vk);
    void debugSetParam(const std::string& id, float value);
    void debugSetRespectVisible(bool on);
    void debugSetForceFull(bool on);
    void debugSetDisableMasks(bool on);
    void debugSetForceInvertedMask(bool on);
    void debugSetForceOpaque(bool on);
    void debugSetEyeOpen(float v);
    void debugSetMouthOpen(float v);
    void debugSetOnly(const std::vector<int>& v);
    void debugSetHidden(const std::vector<int>& v);
    void debugSetParts(const std::map<std::string, float>& m);
    void dumpModel();

private:
    void loadAssets(GLContext& gl);
    void reloadModel(GLContext& gl);
    void pressKey(int vk);
    void releaseKey(int vk);
    void toggleLock();
    void toggleVisible();
    void resetSize();
    void renderFallbackCat(GLContext& gl, int winW, int winH);
    void updateLook(int winW, int winH);
    bool isTypingKey(int vk) const;
    void startDrag(int button, int mouseX, int mouseY);
    void updateDrag(int mouseX, int mouseY);
    void openMenu(int mouseX, int mouseY);
    bool runMenuAction(int action);

    enum class Drag { Idle, Move, Scale };

    Config* cfg_ = nullptr;
    State* state_ = nullptr;
    GLContext* gl_ = nullptr;
    TextRenderer* text_ = nullptr;
    Window* win_ = nullptr;
    CubismCore core_;

    std::unique_ptr<Live2DModel> model_;
    std::string modelError_;
    bool modelFallback_ = true;

    std::vector<Texture> handFrames_;
    Texture mouseIdle_, mouseLeft_, mouseRight_, mouseSide_;
    Texture catFallback_;
    std::map<int, int> vkToFrame_;
    std::vector<int> heldKeys_;
    int currentFrame_ = -1;

    Vec2f cursor_{0, 0};
    bool cursorFresh_ = false;   // 本帧是否真的取到了指针位置
    bool cursorTrusted_ = false; // 该位置是否可信（Wayland 下可能只有过期种子）
    Vec2f mouseSprite_{0, 0};
    bool mouseSpriteInit_ = false;
    float mouth_ = 0.0f;
    float mouthTarget_ = 0.0f;
    float smile_ = 0.0f;
    float smileTarget_ = 0.0f;
    float time_ = 0.0f;
    int mouseButtonPulse_ = 0;
    float mousePulseTimer_ = 0.0f;
    bool mouseLeftDown_ = false;
    bool mouseRightDown_ = false;

    // 修饰键状态（全局监听维护）
    bool ctrl_ = false;
    bool shift_ = false;
    bool alt_ = false;

    Drag drag_ = Drag::Idle;
    bool rightPressed_ = false;
    int rightPressX_ = 0, rightPressY_ = 0;
    int dragStartGlobalX_ = 0, dragStartGlobalY_ = 0;
    int dragStartWinX_ = 0, dragStartWinY_ = 0;
    int dragStartW_ = 0, dragStartH_ = 0;

    Menu menu_;
    bool quit_ = false;
    bool needsReload_ = false;
    float eyeOpenOverride_ = -1.0f;
    float mouthOpenOverride_ = -1.0f;
    std::map<std::string, float> pinnedParams_;
};

}  // namespace fx
