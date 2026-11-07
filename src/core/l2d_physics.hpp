// Cubism 物理演算（physics3.json）的独立实现：算法逐行对应官方 CubismNativeFramework
// 的 CubismPhysics，只依赖 Core 的参数读写，不引入整个 Framework。
//
// 求值顺序很重要：必须在 csmUpdateModel() 之前调用 evaluate()，物理输出写进参数后
// 才会参与本帧的形变计算。
#pragma once

#include <map>
#include <string>
#include <vector>

#include "l2d_core.hpp"

namespace fx {

class Physics {
public:
    void destroy();
    // paramIndex 为模型参数 ID → 索引，用于把 physics3.json 里的参数名解析成 Core 索引
    bool load(const std::string& physics3Path, const std::map<std::string, int>& paramIndex, std::string* error);
    bool valid() const { return !subRigs_.empty(); }

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    // 回到初始状态（重载模型时调用 load() 内部也会做）
    void reset();
    // 按固定步长推进物理并插值写回参数；dt 必须 > 0
    void evaluate(CubismCore& core, CsmModel* model, float dt);

    int subRigCount() const { return int(subRigs_.size()); }
    float fps() const { return fps_; }

    // 内部数据结构（public 仅为便于 .cpp 内的自由函数访问，不属于对外接口）
    struct Vec2 {
        float x = 0.0f, y = 0.0f;
        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}
    };

    struct Particle {
        Vec2 initialPosition;
        Vec2 position;
        Vec2 lastPosition;
        Vec2 lastGravity{0.0f, 1.0f};
        Vec2 force;
        Vec2 velocity;
        float mobility = 1.0f;
        float delay = 1.0f;
        float acceleration = 1.0f;
        float radius = 0.0f;
    };

private:

    struct Normalization {
        float minimum = -10.0f, maximum = 10.0f, defaultValue = 0.0f;
    };

    // 0 = X, 1 = Y, 2 = Angle
    enum Type { kTypeX = 0, kTypeY = 1, kTypeAngle = 2 };

    struct Input {
        std::string sourceId;
        int sourceIndex = -1;
        float weight = 100.0f;
        int type = kTypeAngle;
        bool reflect = false;
    };

    struct Output {
        std::string destinationId;
        int destinationIndex = -1;
        int vertexIndex = 0;
        float scale = 0.0f;
        float weight = 100.0f;
        int type = kTypeAngle;
        bool reflect = false;
        float valueBelowMinimum = 0.0f;    // 仅用于诊断
        float valueExceededMaximum = 0.0f;  // 仅用于诊断
    };

    struct SubRig {
        Normalization normalizationPosition;
        Normalization normalizationAngle;
        std::vector<Input> inputs;
        std::vector<Output> outputs;
        std::vector<Particle> particles;
        std::vector<float> currentOutputs;
        std::vector<float> previousOutputs;
    };

    void initialize();
    void updateRig(SubRig& rig, const float* parameterValues, const float* parameterMinimumValues,
                   const float* parameterMaximumValues, const float* parameterDefaultValues, float* parameterCaches,
                   float physicsDeltaTime);
    void interpolate(float* parameterValues, const float* parameterMinimumValues, const float* parameterMaximumValues,
                     float weight);

    std::vector<SubRig> subRigs_;
    float fps_ = 0.0f;
    Vec2 gravity_{0.0f, -1.0f};
    Vec2 wind_;
    float remainTime_ = 0.0f;
    std::vector<float> parameterCaches_;
    std::vector<float> parameterInputCaches_;
    bool enabled_ = true;
};

}  // namespace fx
