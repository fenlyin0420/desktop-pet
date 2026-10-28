// 用 Cubism Core 直接驱动 moc3 模型（不经 CubismNativeFramework）：
// 参数/部件读写 + 逐 drawable 绘制，支持遮罩与混合模式。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gl.hpp"
#include "l2d_core.hpp"
#include "l2d_physics.hpp"

namespace fx {

class Live2DModel {
public:
    ~Live2DModel() { freeBuffers(); }
    bool load(CubismCore& core, GLContext& gl, const std::string& model3Path, std::string* error);
    void destroy(GLContext& gl);

    bool valid() const { return model_ != nullptr; }
    int drawableCount() const { return drawableCount_; }

    // 每帧调用一次：写入参数 → csmUpdateModel
    void update(float dt);

    bool hasParam(const std::string& id) const;
    // 钉住参数：在本帧 csmUpdateModel 之前写入（形变是 update 时计算的，之后写没用）
    void pinParam(const std::string& id, float value) { pinnedParams_[id] = value; }
    void unpinParam(const std::string& id) { pinnedParams_.erase(id); }
    void setParam(const std::string& id, float value);
    float getParam(const std::string& id) const;
    void setPartOpacity(const std::string& id, float opacity);

    // 交互表现
    void setLook(float x, float y);  // -1..1
    void setMouthOpen(float v);      // 0..1
    void setEyeSmile(float v);       // 0..1
    void setBodyAngle(float x, float y);
    void setMousePosition(float x, float y);  // -1..1，y 正向下
    void setMouseButtons(bool left, bool right);
    void setBreathEnabled(bool on) { breath_ = on; }
    void setAutoBlink(bool on) { autoBlink_ = on; }
    void setPhysicsEnabled(bool on) { physics_.setEnabled(on); }
    bool hasPhysics() const { return physics_.valid(); }

    // 布局：correct 为整体缩放系数，offset 为相对画布尺寸的归一化偏移（y 正向下）
    struct Layout {
        float correct = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };
    void setLayout(const Layout& layout) { layout_ = layout; }
    // 调试：只画/跳过指定 drawable
    void setOnlyDrawables(const std::vector<int>& list) { onlyDrawables_ = list; }
    void setHiddenDrawables(const std::vector<int>& list) { hiddenDrawables_ = list; }
    void setPartOverrides(const std::map<std::string, float>& map) { partOverrides_ = map; }
    void setRespectVisible(bool on) { respectVisible_ = on; }
    void setForceFull(bool on) { forceFull_ = on; }
    void setForceOpaque(bool on) { forceOpaque_ = on; }
    void setDisableMasks(bool on) { disableMasks_ = on; }
    void setForceInvertedMask(bool on) { forceInvertedMask_ = on; }
    // 按键时把模型自带的静止手隐藏掉，避免和手部贴图重手
    void setPartOpacityOverride(const std::string& id, float value) { partOverrides_[id] = value; }
    // 运行期隐藏指定 drawable（按键时藏掉模型自带的静止手）
    void setRuntimeHidden(const std::vector<int>& list) { runtimeHidden_ = list; }
    void setFlipV(bool flip) { flipV_ = flip; }
    struct Placement {
        float x = 0, y = 0, w = 0, h = 0;
    };
    // 模型在窗口内的实际绘制矩形（手部贴图与之对齐）
    Placement placement(float winW, float winH) const;

    void render(GLContext& gl, float x, float y, float w, float h, bool flipX, float opacity);
    // 诊断：打印参数默认值、部件透明度、drawable 可见性
    void dump() const;

    float canvasWidth() const { return canvasW_; }
    float canvasHeight() const { return canvasH_; }

private:
    struct Drawable {
        int constantFlags = 0;
        int vertexCount = 0;
        int indexCount = 0;
        int maskCount = 0;
        int renderOrder = 0;
        int textureIndex = 0;
        std::vector<int> masks;
        std::vector<uint16_t> indices;
        std::vector<float> tmpPos;  // 每帧变换后的窗口坐标
    };

    CubismCore* core_ = nullptr;
    CsmModel* model_ = nullptr;
    Physics physics_;
    // Core 要求 64 字节对齐的 moc/model 缓冲区
    void* mocBuf_ = nullptr;
    void* modelBuf_ = nullptr;
    void freeBuffers();
    std::vector<Texture> textures_;

    float canvasW_ = 1, canvasH_ = 1;
    float originX_ = 0, originY_ = 0;
    float pixelsPerUnit_ = 1;
    bool coordCentered_ = false;
    // Cubism 的 UV 原点在纹理左下，需要翻转 V
    bool flipV_ = true;
    Layout layout_;

    std::vector<Drawable> drawables_;
    std::vector<int> drawableParentPart_;   // drawable → 父部件
    std::vector<int> partParentPart_;       // 部件 → 父部件
    std::vector<int> drawOrder_;
    std::map<std::string, int> paramIndex_;
    std::vector<int> onlyDrawables_;
    std::vector<int> hiddenDrawables_;
    std::vector<int> runtimeHidden_;
    std::map<std::string, float> partOverrides_;
    std::map<std::string, float> pinnedParams_;
    bool respectVisible_ = true;
    bool disableMasks_ = false;
    bool forceInvertedMask_ = true;  // 按 Core 的反向遮罩标志处理
    bool forceFull_ = false;
    bool forceOpaque_ = false;
    std::map<std::string, int> partIndex_;
    int drawableCount_ = 0;

    // 表现状态
    float lookX_ = 0, lookY_ = 0;
    float mouseX_ = 0, mouseY_ = 0;
    bool mouseLeftDown_ = false, mouseRightDown_ = false;
    float mouthOpen_ = 0;
    float eyeSmile_ = 0;
    float bodyX_ = 0, bodyY_ = 0;
    float time_ = 0;
    float blinkTimer_ = 2.0f;
    float blinkPhase_ = -1.0f;
    bool breath_ = true;
    bool autoBlink_ = true;
    float blink_ = 1.0f;  // 眼睛开合（1 = 睁开）

    std::vector<float> vertexScratch_;
    std::vector<float> uvScratch_;

    void applyDefaults();
    // Cubism 要求把父部件的不透明度链乘进 drawable 的最终不透明度
    float effectiveOpacity(int drawableIndex, const float* drawableOpacities) const;
};

}  // namespace fx
