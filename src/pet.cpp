#include "pet.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <X11/keysym.h>

#include "global_input.hpp"

namespace fx {

static const int kVkControl = 0x11;
static const int kVkShift = 0x10;
static const int kVkAlt = 0x12;
static const int kVkT = 0x54;
static const int kVkH = 0x48;
static const int kVkR = 0x52;
static const int kVkQ = 0x51;

static std::string imagePath(const Config& cfg, const std::string& rel) { return cfg.assetDir + "/img/" + rel; }

bool Pet::init(Config& cfg, State& st, GLContext& gl, TextRenderer& text, Window& win, std::string* error) {
    cfg_ = &cfg;
    state_ = &st;
    gl_ = &gl;
    text_ = &text;
    win_ = &win;

    if (cfg.standardL2d) {
        std::string coreError;
        if (core_.load(&coreError)) {
            logLine("Live2D Core: %s", core_.path.c_str());
        } else {
            logLine("Live2D Core 不可用：%s", coreError.c_str());
        }
    }
    reloadModel(gl);
    loadAssets(gl);

    if (modelFallback_ && error) *error = modelError_;
    return true;
}

void Pet::reloadModel(GLContext& gl) {
    model_.reset();
    modelFallback_ = true;
    modelError_.clear();
    if (!cfg_->standardL2d) {
        modelError_ = "配置关闭了 Live2D（standard.l2d = false）";
        return;
    }
    if (!core_.ok()) {
        modelError_ = "缺少 Live2D Cubism Core（运行 tools/fetch_live2d_core.sh 可自动获取）";
        return;
    }
    auto fresh = std::make_unique<Live2DModel>();
    std::string err;
    const std::string modelPath = imagePath(*cfg_, "standard/cat_model/cat.model3.json");
    if (!fresh->load(core_, gl, modelPath, &err)) {
        modelError_ = err + "，已退回静态图";
        logLine("模型加载失败: %s", err.c_str());
        return;
    }
    Live2DModel::Layout layout;
    layout.correct = cfg_->l2dCorrect;
    layout.offsetX = cfg_->l2dOffset.x;
    layout.offsetY = cfg_->l2dOffset.y;
    fresh->setLayout(layout);
    model_ = std::move(fresh);
    modelFallback_ = false;
    logLine("Live2D 模型已加载：%s", modelPath.c_str());
}

void Pet::loadAssets(GLContext& gl) {
    handFrames_.clear();
    vkToFrame_.clear();
    for (size_t i = 0; i < cfg_->standardHand.size(); ++i) {
        char rel[64];
        std::snprintf(rel, sizeof(rel), "standard/hand/%zu.png", i);
        std::string err;
        Texture t = gl.loadTexture(imagePath(*cfg_, rel), &err);
        if (t.valid()) {
            for (int vk : cfg_->standardHand[i]) vkToFrame_[vk] = int(handFrames_.size());
            handFrames_.push_back(t);
        } else {
            logLine("%s", err.c_str());
        }
    }

    std::string err;
    mouseIdle_ = gl.loadTexture(imagePath(*cfg_, "standard/mouse.png"), &err);
    mouseLeft_ = gl.loadTexture(imagePath(*cfg_, "standard/mouse_left.png"), &err);
    mouseRight_ = gl.loadTexture(imagePath(*cfg_, "standard/mouse_right.png"), &err);
    mouseSide_ = gl.loadTexture(imagePath(*cfg_, "standard/mouse_side.png"), &err);
    catFallback_ = gl.loadTexture(imagePath(*cfg_, "standard/cat.png"), &err);

    logLine("资源加载完成：手部帧 %zu，键位映射 %zu", handFrames_.size(), vkToFrame_.size());
}

void Pet::debugPress(int vk) { pressKey(vk); }

void Pet::debugSetRespectVisible(bool on) {
    if (model_) model_->setRespectVisible(on);
}

void Pet::debugSetEyeOpen(float v) {
    if (!model_) return;
    eyeOpenOverride_ = v;
    model_->pinParam("ParamEyeLOpen", v);
    model_->pinParam("ParamEyeROpen", v);
}

void Pet::debugSetMouthOpen(float v) {
    if (!model_) return;
    mouthOpenOverride_ = v;
    model_->pinParam("ParamMouthOpenY", v);
}

void Pet::debugSetForceOpaque(bool on) {
    if (model_) model_->setForceOpaque(on);
}

void Pet::debugSetDisableMasks(bool on) {
    if (model_) model_->setDisableMasks(on);
}

void Pet::debugSetForceInvertedMask(bool on) {
    if (model_) model_->setForceInvertedMask(on);
}

void Pet::debugSetForceFull(bool on) {
    if (model_) model_->setForceFull(on);
}

void Pet::debugSetOnly(const std::vector<int>& v) {
    if (model_) model_->setOnlyDrawables(v);
}

void Pet::debugSetHidden(const std::vector<int>& v) {
    if (model_) model_->setHiddenDrawables(v);
}

void Pet::debugSetParts(const std::map<std::string, float>& m) {
    if (model_) model_->setPartOverrides(m);
}

void Pet::debugSetParam(const std::string& id, float value) {
    if (model_) model_->pinParam(id, value);
}

void Pet::dumpModel() {
    if (model_) model_->dump();
    else std::printf("没有加载 Live2D 模型: %s\n", modelError_.c_str());
}

void Pet::shutdown(GLContext& gl) {
    for (Texture& t : handFrames_) gl.destroyTexture(t);
    handFrames_.clear();
    gl.destroyTexture(mouseIdle_);
    gl.destroyTexture(mouseLeft_);
    gl.destroyTexture(mouseRight_);
    gl.destroyTexture(mouseSide_);
    gl.destroyTexture(catFallback_);
    if (model_) model_->destroy(gl);
    model_.reset();
    if (text_) text_->shutdown(gl);
}

void Pet::toggleLock() {
    state_->locked = !state_->locked;
    saveState(*cfg_, *state_);
    logLine("锁定状态：%s", state_->locked ? "已锁定（鼠标穿透）" : "未锁定");
}

void Pet::toggleVisible() {
    state_->visible = !state_->visible;
    if (state_->visible) {
        win_->show();
        win_->raise();
    } else {
        win_->hide();
    }
    saveState(*cfg_, *state_);
}

void Pet::resetSize() {
    const int w = std::max(160, int(float(cfg_->windowW) * cfg_->scalarX * 0.55f));
    const int h = std::max(120, int(float(cfg_->windowH) * cfg_->scalarY * 0.55f));
    win_->move(win_->x() + (win_->width() - w), win_->y() + (win_->height() - h));
    win_->resize(w, h);
    state_->window = Rect{win_->x(), win_->y(), w, h};
    saveState(*cfg_, *state_);
}

bool Pet::isTypingKey(int vk) const { return vk != kVkShift && vk != kVkControl && vk != kVkAlt && vk != 0x5B && vk != 0x14 && vk != 0x90; }

void Pet::pressKey(int vk) {
    if (std::find(heldKeys_.begin(), heldKeys_.end(), vk) != heldKeys_.end()) return;
    heldKeys_.push_back(vk);
    auto it = vkToFrame_.find(vk);
    if (it != vkToFrame_.end()) currentFrame_ = it->second;
    if (isTypingKey(vk)) {
        mouthTarget_ = 1.0f;
        smileTarget_ = 0.35f;
    }
}

void Pet::releaseKey(int vk) {
    auto it = std::find(heldKeys_.begin(), heldKeys_.end(), vk);
    if (it == heldKeys_.end()) return;
    heldKeys_.erase(it);
    currentFrame_ = -1;
    for (auto rit = heldKeys_.rbegin(); rit != heldKeys_.rend(); ++rit) {
        auto fit = vkToFrame_.find(*rit);
        if (fit != vkToFrame_.end()) {
            currentFrame_ = fit->second;
            break;
        }
    }
}

void Pet::handleGlobalInput(std::vector<InputEvent>& events, const GlobalInput& input) {
    (void)input;
    for (const InputEvent& ev : events) {
        const bool isKey = ev.type == InputEvent::Type::KeyDown || ev.type == InputEvent::Type::KeyUp;
        if (isKey) {
            if (ev.code == kVkControl) ctrl_ = ev.type == InputEvent::Type::KeyDown;
            if (ev.code == kVkShift) shift_ = ev.type == InputEvent::Type::KeyDown;
            if (ev.code == kVkAlt) alt_ = ev.type == InputEvent::Type::KeyDown;
        }
        if (ev.type == InputEvent::Type::KeyDown) {
            if (ctrl_ && shift_) {
                if (ev.code == kVkT) {
                    toggleLock();
                    continue;
                }
                if (ev.code == kVkH) {
                    toggleVisible();
                    continue;
                }
                if (ev.code == kVkR) {
                    resetSize();
                    continue;
                }
                if (ev.code == kVkQ) {
                    quit_ = true;
                    continue;
                }
            }
            pressKey(ev.code);
        } else if (ev.type == InputEvent::Type::KeyUp) {
            releaseKey(ev.code);
        } else if (ev.type == InputEvent::Type::MouseDown || ev.type == InputEvent::Type::MouseUp) {
            // 锁定时窗口收不到鼠标事件，用全局事件驱动反应
            if (!state_->locked) continue;
            if (ev.type == InputEvent::Type::MouseDown) {
                mouseButtonPulse_ = ev.code == 1 ? 1 : (ev.code == 3 ? 2 : (ev.code == 5 || ev.code == 6 ? 3 : 0));
                mouthTarget_ = 0.6f;
            }
        }
    }
    watermarkHidden_ = ctrl_;
}

bool Pet::runMenuAction(int action) {
    switch (action) {
        case MenuAction::ToggleLock: toggleLock(); return true;
        case MenuAction::Hide: toggleVisible(); return true;
        case MenuAction::ResetSize: resetSize(); return true;
        case MenuAction::Reload: needsReload_ = true; return true;
        case MenuAction::Quit: quit_ = true; return true;
        default: return false;
    }
}

void Pet::openMenu(int mouseX, int mouseY) { menu_.open(float(mouseX), float(mouseY), state_->locked); }

void Pet::startDrag(int button, int mouseX, int mouseY) {
    int gx = 0, gy = 0;
    win_->globalPointer(gx, gy);
    dragStartGlobalX_ = gx;
    dragStartGlobalY_ = gy;
    dragStartWinX_ = win_->x();
    dragStartWinY_ = win_->y();
    dragStartW_ = win_->width();
    dragStartH_ = win_->height();
    if (button == 1) {
        drag_ = Drag::Move;
        win_->grabPointer(true);
    } else if (button == 3) {
        rightPressed_ = true;
        rightPressX_ = mouseX;
        rightPressY_ = mouseY;
        drag_ = Drag::Scale;
        win_->grabPointer(true);
    }
}

void Pet::updateDrag(int mouseX, int mouseY) {
    if (drag_ == Drag::Idle) return;
    int gx = 0, gy = 0;
    win_->globalPointer(gx, gy);
    const int dx = gx - dragStartGlobalX_;
    const int dy = gy - dragStartGlobalY_;
    if (drag_ == Drag::Move) {
        win_->move(dragStartWinX_ + dx, dragStartWinY_ + dy);
        state_->window = Rect{win_->x(), win_->y(), win_->width(), win_->height()};
    } else if (drag_ == Drag::Scale) {
        const int newW = std::max(160, dragStartW_ + dx);
        const int newH = std::max(120, dragStartH_ + dy);
        const int nx = dragStartWinX_ + (dragStartW_ - newW);
        const int ny = dragStartWinY_ + (dragStartH_ - newH);
        win_->resize(newW, newH);
        win_->move(nx, ny);
        state_->window = Rect{nx, ny, newW, newH};
    }
    (void)mouseX;
    (void)mouseY;
}

void Pet::handleWindowEvents(const std::vector<WinEvent>& events) {
    for (const WinEvent& ev : events) {
        switch (ev.type) {
            case WinEventType::MousePress: {
                if (state_->locked) break;
                if (ev.button == 4 || ev.button == 5) {
                    const float factor = ev.button == 4 ? 1.06f : 0.94f;
                    const int w = win_->width();
                    const int h = win_->height();
                    const int nw = std::max(160, int(float(w) * factor));
                    const int nh = std::max(120, int(float(h) * factor));
                    const int nx = win_->x() + (w - nw);
                    const int ny = win_->y() + (h - nh);
                    win_->resize(nw, nh);
                    win_->move(nx, ny);
                    state_->window = Rect{nx, ny, nw, nh};
                    break;
                }
                if (menu_.visible()) {
                    const int idx = menu_.hitTest(float(ev.x), float(ev.y));
                    if (idx >= 0) {
                        runMenuAction(menu_.actionAt(idx));
                        menu_.close();
                        break;
                    }
                    if (menu_.contains(float(ev.x), float(ev.y))) break;
                    menu_.close();
                }
                if (ev.button == 2) {
                    openMenu(ev.x, ev.y);
                } else {
                    startDrag(ev.button, ev.x, ev.y);
                }
                break;
            }
            case WinEventType::MouseRelease: {
                win_->grabPointer(false);
                if (ev.button == 3 && rightPressed_) {
                    rightPressed_ = false;
                    if (std::abs(ev.x - rightPressX_) <= 4 && std::abs(ev.y - rightPressY_) <= 4) {
                        openMenu(ev.x, ev.y);
                    } else {
                        saveState(*cfg_, *state_);
                    }
                } else if (ev.button == 1) {
                    saveState(*cfg_, *state_);
                }
                drag_ = Drag::Idle;
                break;
            }
            case WinEventType::MouseMove:
                if (menu_.visible()) menu_.setHover(float(ev.x), float(ev.y));
                if (drag_ != Drag::Idle) updateDrag(ev.x, ev.y);
                break;
            case WinEventType::KeyDown: {
                if ((ev.state & ControlMask) && (ev.state & ShiftMask) && ev.key == XK_t) toggleLock();
                if (ev.key == XK_Escape) menu_.close();
                break;
            }
            case WinEventType::FocusLost:
                menu_.close();
                break;
            case WinEventType::Moved:
                state_->window.x = win_->x();
                state_->window.y = win_->y();
                break;
            case WinEventType::Resize:
                state_->window.w = win_->width();
                state_->window.h = win_->height();
                break;
            case WinEventType::Close:
                quit_ = true;
                break;
            default: break;
        }
    }
}

void Pet::updateLook(int winW, int winH) {
    int sx = 0, sy = 0;
    if (!win_->globalPointer(sx, sy)) return;
    cursor_.x = float(sx - win_->x());
    cursor_.y = float(sy - win_->y());
    const float nx = (cursor_.x - float(winW) * 0.5f) / (float(winW) * 0.5f + 1.0f);
    const float ny = (cursor_.y - float(winH) * 0.55f) / (float(winH) * 0.55f + 1.0f);
    if (model_) {
        const float x = std::max(-1.0f, std::min(1.0f, nx));
        const float y = std::max(-1.0f, std::min(1.0f, ny));
        model_->setLook(x, y);
        model_->setMousePosition(x, y);
    }
}

void Pet::frame(float dt, int winW, int winH, GLContext& gl) {
    if (needsReload_) {
        needsReload_ = false;
        reloadModel(gl);
    }
    time_ += dt;

    mouthTarget_ = std::max(0.0f, mouthTarget_ - dt * 4.0f);
    smileTarget_ = std::max(0.0f, smileTarget_ - dt * 2.5f);
    mouth_ += (mouthTarget_ - mouth_) * std::min(1.0f, dt * 22.0f);
    smile_ += (smileTarget_ - smile_) * std::min(1.0f, dt * 12.0f);
    mousePulseTimer_ += dt;
    if (mousePulseTimer_ > 0.12f) mouseButtonPulse_ = 0;

    updateLook(winW, winH);

    if (model_) {
        model_->setMouthOpen(mouthOpenOverride_ >= 0.0f ? mouthOpenOverride_ : mouth_ * 0.85f);
        model_->setEyeSmile(smile_);
        model_->setMouseButtons(mouseButtonPulse_ == 1, mouseButtonPulse_ == 2);
        // 参数必须在 csmUpdateModel 之前写入，否则本帧不生效
        if (model_->hasParam(cfg_->watermarkParam)) {
            const bool hidden = cfg_->hideWatermark || watermarkHidden_;
            model_->setParam(cfg_->watermarkParam, hidden ? cfg_->watermarkOff : cfg_->watermarkOn);
        }
        model_->update(dt);
    }

    if (model_) {
        // 手部贴图接管按键动作时，隐藏模型自带的静止手（光照/贴图仍由 PNG 提供）
        model_->setRuntimeHidden(currentFrame_ >= 0 ? cfg_->handDrawables : std::vector<int>{});
        if (!cfg_->handPart.empty()) model_->setPartOpacityOverride(cfg_->handPart, currentFrame_ >= 0 ? 0.0f : 1.0f);
    }

    gl.beginFrame(winW, winH, Color{0, 0, 0, 0});

    if (model_) {
        model_->render(gl, 0.0f, 0.0f, float(winW), float(winH), cfg_->l2dFlipX, 1.0f);
    } else {
        renderFallbackCat(gl, winW, winH);
    }

    if (currentFrame_ >= 0 && currentFrame_ < int(handFrames_.size()) && handFrames_[size_t(currentFrame_)].valid()) {
        // 手部贴图与模型共用同一块画布，用同一目标矩形才能对齐
        float hx = 0.0f, hy = 0.0f, hw = float(winW), hh = float(winH);
        if (model_) {
            const Live2DModel::Placement p = model_->placement(float(winW), float(winH));
            hx = p.x;
            hy = p.y;
            hw = p.w;
            hh = p.h;
        }
        hx += cfg_->handOffset.x * hw;
        hy += cfg_->handOffset.y * hh;
        gl.setColorParams(Color{1, 1, 1, 1}, Color{1, 1, 1, 1}, Color{0, 0, 0, 0});
        gl.quad(handFrames_[size_t(currentFrame_)], hx, hy, hw, hh);
    }

    if (cfg_->standardMouse && mouseIdle_.valid()) {
        const Texture* tex = &mouseIdle_;
        if (mouseButtonPulse_ == 1 && mouseLeft_.valid())
            tex = &mouseLeft_;
        else if (mouseButtonPulse_ == 2 && mouseRight_.valid())
            tex = &mouseRight_;
        else if (mouseButtonPulse_ == 3 && mouseSide_.valid())
            tex = &mouseSide_;
        const float scale = float(winW) / float(cfg_->windowW > 0 ? cfg_->windowW : winW);
        const float mw = float(tex->w) * scale;
        const float mh = float(tex->h) * scale;
        if (!mouseSpriteInit_) {
            mouseSprite_ = Vec2f{cursor_.x, cursor_.y};
            mouseSpriteInit_ = true;
        }
        const float k = std::min(1.0f, dt * 18.0f * std::max(0.2f, cfg_->mouseSpeed));
        mouseSprite_.x += (cursor_.x - mouseSprite_.x) * k;
        mouseSprite_.y += (cursor_.y - mouseSprite_.y) * k;
        gl.setColorParams(Color{1, 1, 1, 0.96f}, Color{1, 1, 1, 1}, Color{0, 0, 0, 0});
        gl.quad(*tex, mouseSprite_.x - mw * 0.5f, mouseSprite_.y - mh * 0.5f, mw, mh);
        gl.resetColorParams();
    }

    menu_.render(gl, *text_);
}

void Pet::renderFallbackCat(GLContext& gl, int winW, int winH) {
    if (!catFallback_.valid()) return;
    const float scale = std::min(float(winW) / float(catFallback_.w), float(winH) / float(catFallback_.h));
    const float w = float(catFallback_.w) * scale;
    const float h = float(catFallback_.h) * scale;
    gl.setColorParams(Color{1, 1, 1, 1}, Color{1, 1, 1, 1}, Color{0, 0, 0, 0});
    gl.quad(catFallback_, (float(winW) - w) * 0.5f, float(winH) - h, w, h);
    gl.resetColorParams();
}

}  // namespace fx
