#include "l2d_physics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config.hpp"
#include "json.hpp"

namespace fx {

namespace {

constexpr float kAirResistance = 5.0f;
constexpr float kMaximumWeight = 100.0f;
constexpr float kMovementThreshold = 0.001f;
constexpr float kMaxDeltaTime = 5.0f;
constexpr float kPi = 3.14159265358979323846f;

float absF(float v) { return v < 0.0f ? -v : v; }

float rangeValue(float min, float max) {
    const float maxValue = std::max(min, max);
    const float minValue = std::min(min, max);
    return absF(maxValue - minValue);
}

int sign(float value) { return value > 0.0f ? 1 : (value < 0.0f ? -1 : 0); }

float defaultValue(float min, float max) {
    const float minValue = std::min(min, max);
    return minValue + rangeValue(min, max) * 0.5f;
}

// 官方实现里 normalized 值与参数值都是“中值向两侧线性映射”，isInverted 决定结果取反
float normalizeParameterValue(float value, float parameterMinimum, float parameterMaximum, float parameterDefault,
                              float normalizedMinimum, float normalizedMaximum, float normalizedDefault, bool inverted) {
    const float maxValue = std::max(parameterMaximum, parameterMinimum);
    const float minValue = std::min(parameterMaximum, parameterMinimum);
    value = std::min(std::max(value, minValue), maxValue);

    const float minNormValue = std::min(normalizedMinimum, normalizedMaximum);
    const float maxNormValue = std::max(normalizedMinimum, normalizedMaximum);
    const float middleNormValue = normalizedDefault;

    const float middleValue = defaultValue(minValue, maxValue);
    const float paramValue = value - middleValue;
    float result = 0.0f;
    switch (sign(paramValue)) {
        case 1: {
            const float nLength = maxNormValue - middleNormValue;
            const float pLength = maxValue - middleValue;
            if (pLength != 0.0f) result = paramValue * (nLength / pLength) + middleNormValue;
            break;
        }
        case -1: {
            const float nLength = minNormValue - middleNormValue;
            const float pLength = minValue - middleValue;
            if (pLength != 0.0f) result = paramValue * (nLength / pLength) + middleNormValue;
            break;
        }
        case 0:
            result = middleNormValue;
            break;
        default:
            break;
    }
    return inverted ? result : (result * -1.0f);
}

// ---- 向量运算（与 CubismVector2 语义一致）

struct V : Physics::Vec2 {
    V() = default;
    V(float x_, float y_) : Physics::Vec2(x_, y_) {}
    V(Physics::Vec2 v) : Physics::Vec2(v.x, v.y) {}
};

V makeV(float x, float y) { return V{x, y}; }
V add(V a, V b) { return V{a.x + b.x, a.y + b.y}; }
V sub(V a, V b) { return V{a.x - b.x, a.y - b.y}; }
V mul(V a, float s) { return V{a.x * s, a.y * s}; }
V divV(V a, float s) { return V{a.x / s, a.y / s}; }
// 官方 Normalize 直接除以长度；长度为 0 会得到 NaN，这里退回零向量以免污染整条链
V normalize(V a) {
    const float len = std::sqrt(a.x * a.x + a.y * a.y);
    if (len < 1e-6f) return V{0.0f, 0.0f};
    return V{a.x / len, a.y / len};
}

float directionToRadian(V from, V to) {
    const float q1 = std::atan2(to.y, to.x);
    const float q2 = std::atan2(from.y, from.x);
    float ret = q1 - q2;
    while (ret < -kPi) ret += kPi * 2.0f;
    while (ret > kPi) ret -= kPi * 2.0f;
    return ret;
}

// CubismMath::RadianToDirection：注意 X=sin、Y=cos（角度从 +Y 轴顺时针度量）
V radianToDirection(float radian) { return V{std::sin(radian), std::cos(radian)}; }

float degreesToRadian(float degrees) { return degrees * kPi / 180.0f; }

// ---- 输入：把参数归一化后累加到位移 / 角度

void inputNormalizedValue(V& targetTranslation, float& targetAngle, int type, float value, float parameterMinimum,
                          float parameterMaximum, float parameterDefault, float normPositionMinimum,
                          float normPositionMaximum, float normPositionDefault, float normAngleMinimum,
                          float normAngleMaximum, float normAngleDefault, bool inverted, float weight) {
    if (type == 2) {  // Angle
        targetAngle += normalizeParameterValue(value, parameterMinimum, parameterMaximum, parameterDefault,
                                               normAngleMinimum, normAngleMaximum, normAngleDefault, inverted) *
                       weight;
    } else if (type == 1) {  // Y
        targetTranslation.y +=
            normalizeParameterValue(value, parameterMinimum, parameterMaximum, parameterDefault, normPositionMinimum,
                                    normPositionMaximum, normPositionDefault, inverted) *
            weight;
    } else {  // X
        targetTranslation.x +=
            normalizeParameterValue(value, parameterMinimum, parameterMaximum, parameterDefault, normPositionMinimum,
                                    normPositionMaximum, normPositionDefault, inverted) *
            weight;
    }
}

// ---- 输出：从粒子链取位移或角度

float outputValue(int type, V translation, const std::vector<Physics::Particle>& particles, int particleIndex,
                  bool inverted, V parentGravity) {
    float output = 0.0f;
    if (type == 2) {  // Angle：与上一段粒子方向（或反向重力）的夹角
        V reference = parentGravity;
        if (particleIndex >= 2) {
            reference = sub(particles[size_t(particleIndex - 1)].position, particles[size_t(particleIndex - 2)].position);
        } else {
            reference = mul(parentGravity, -1.0f);
        }
        output = directionToRadian(reference, translation);
    } else if (type == 1) {
        output = translation.y;
    } else {
        output = translation.x;
    }
    return inverted ? -output : output;
}

float outputScale(int type, float scale) {
    // physics3.json 只有一个 Scale；官方 5.x 的 X/Y 输出用未赋值的 TranslationScale（恒为 0），
    // 这里统一按 Scale 处理，X/Y 输出才有意义（本模型只用到 Angle）。
    (void)type;
    return scale;
}

// ---- 粒子推进

void updateParticles(std::vector<Physics::Particle>& strand, V totalTranslation, float totalAngle, V windDirection,
                     float thresholdValue, float deltaTimeSeconds, float airResistance) {
    if (strand.empty()) return;
    const int count = int(strand.size());
    strand[0].position = totalTranslation;

    const float totalRadian = degreesToRadian(totalAngle);
    V currentGravity = normalize(radianToDirection(totalRadian));

    for (int i = 1; i < count; ++i) {
        Physics::Particle& p = strand[size_t(i)];
        p.force = add(mul(currentGravity, p.acceleration), windDirection);
        p.lastPosition = p.position;

        const float delay = p.delay * deltaTimeSeconds * 30.0f;

        V direction = sub(p.position, strand[size_t(i - 1)].position);
        const float radian = directionToRadian(p.lastGravity, currentGravity) / airResistance;
        // 与官方一致：先算 X 再用已更新的 X 算 Y
        const float dx = (std::cos(radian) * direction.x) - (direction.y * std::sin(radian));
        const float dy = (std::sin(radian) * direction.x) + (direction.y * std::cos(radian));
        direction = makeV(dx, dy);

        p.position = add(strand[size_t(i - 1)].position, direction);

        const V velocity = mul(p.velocity, delay);
        const V force = mul(p.force, delay * delay);
        p.position = add(add(p.position, velocity), force);

        V newDirection = normalize(sub(p.position, strand[size_t(i - 1)].position));
        p.position = add(strand[size_t(i - 1)].position, mul(newDirection, p.radius));

        if (absF(p.position.x) < thresholdValue) p.position.x = 0.0f;

        if (delay != 0.0f) {
            V vel = sub(p.position, p.lastPosition);
            vel = divV(vel, delay);
            p.velocity = mul(vel, p.mobility);
        }

        p.force = V{0.0f, 0.0f};
        p.lastGravity = currentGravity;
    }
}

// 把粒子输出写回参数：先按输出 Scale 缩放、夹到参数范围，再按 Weight 与当前值混合
void updateOutputParameterValue(float* parameterValue, float parameterValueMinimum, float parameterValueMaximum,
                                float translation, float scale, float weight, float* valueBelowMinimum,
                                float* valueExceededMaximum) {
    float value = translation * scale;

    if (value < parameterValueMinimum) {
        if (value < *valueBelowMinimum) *valueBelowMinimum = value;
        value = parameterValueMinimum;
    } else if (value > parameterValueMaximum) {
        if (value > *valueExceededMaximum) *valueExceededMaximum = value;
        value = parameterValueMaximum;
    }

    const float w = weight / kMaximumWeight;
    if (w >= 1.0f) {
        *parameterValue = value;
    } else {
        *parameterValue = (*parameterValue * (1.0f - w)) + (value * w);
    }
}

}  // namespace

void Physics::destroy() {
    subRigs_.clear();
    parameterCaches_.clear();
    parameterInputCaches_.clear();
    remainTime_ = 0.0f;
    fps_ = 0.0f;
}

void Physics::reset() {
    for (SubRig& rig : subRigs_) {
        for (size_t i = 0; i < rig.particles.size(); ++i) {
            Particle& p = rig.particles[i];
            p.position = p.initialPosition;
            p.lastPosition = p.initialPosition;
            p.lastGravity = V{0.0f, 1.0f};
            p.velocity = V{0.0f, 0.0f};
            p.force = V{0.0f, 0.0f};
        }
        std::fill(rig.currentOutputs.begin(), rig.currentOutputs.end(), 0.0f);
        std::fill(rig.previousOutputs.begin(), rig.previousOutputs.end(), 0.0f);
    }
    parameterCaches_.clear();
    parameterInputCaches_.clear();
    remainTime_ = 0.0f;
}

bool Physics::load(const std::string& physics3Path, const std::map<std::string, int>& paramIndex, std::string* error) {
    destroy();

    json::Value root;
    if (!json::loadFile(physics3Path, root, error)) return false;
    const json::Value& settings = root["PhysicsSettings"];
    if (!settings.isArray() || settings.size() == 0) {
        if (error) *error = "physics3.json 没有 PhysicsSettings: " + physics3Path;
        return false;
    }
    const json::Value& meta = root["Meta"];
    fps_ = float(meta["Fps"].num(0.0));
    gravity_ = V{float(meta["EffectiveForces"]["Gravity"]["X"].num(0.0)),
                 float(meta["EffectiveForces"]["Gravity"]["Y"].num(-1.0))};
    wind_ = V{float(meta["EffectiveForces"]["Wind"]["X"].num(0.0)),
              float(meta["EffectiveForces"]["Wind"]["Y"].num(0.0))};

    int missing = 0;
    for (size_t s = 0; s < settings.size(); ++s) {
        const json::Value& src = settings.at(s);
        SubRig rig;

        const json::Value& norm = src["Normalization"];
        rig.normalizationPosition.minimum = float(norm["Position"]["Minimum"].num(-10.0));
        rig.normalizationPosition.maximum = float(norm["Position"]["Maximum"].num(10.0));
        rig.normalizationPosition.defaultValue = float(norm["Position"]["Default"].num(0.0));
        rig.normalizationAngle.minimum = float(norm["Angle"]["Minimum"].num(-10.0));
        rig.normalizationAngle.maximum = float(norm["Angle"]["Maximum"].num(10.0));
        rig.normalizationAngle.defaultValue = float(norm["Angle"]["Default"].num(0.0));

        const json::Value& inputs = src["Input"];
        for (size_t i = 0; i < inputs.size(); ++i) {
            const json::Value& in = inputs.at(i);
            Input input;
            input.sourceId = in["Source"]["Id"].str();
            input.weight = float(in["Weight"].num(100.0));
            input.reflect = in["Reflect"].boolean(false);
            const std::string type = in["Type"].str();
            input.type = type == "X" ? kTypeX : (type == "Y" ? kTypeY : kTypeAngle);
            auto it = paramIndex.find(input.sourceId);
            if (it != paramIndex.end()) {
                input.sourceIndex = it->second;
            } else {
                ++missing;
            }
            rig.inputs.push_back(std::move(input));
        }

        const json::Value& outputs = src["Output"];
        for (size_t i = 0; i < outputs.size(); ++i) {
            const json::Value& out = outputs.at(i);
            Output output;
            output.destinationId = out["Destination"]["Id"].str();
            output.vertexIndex = out["VertexIndex"].integer(0);
            output.scale = float(out["Scale"].num(0.0));
            output.weight = float(out["Weight"].num(100.0));
            output.reflect = out["Reflect"].boolean(false);
            const std::string type = out["Type"].str();
            output.type = type == "X" ? kTypeX : (type == "Y" ? kTypeY : kTypeAngle);
            auto it = paramIndex.find(output.destinationId);
            if (it != paramIndex.end()) {
                output.destinationIndex = it->second;
            } else {
                ++missing;
            }
            rig.outputs.push_back(std::move(output));
        }

        // 输出用的插值缓存，长度与输出数一致
        rig.currentOutputs.assign(rig.outputs.size(), 0.0f);
        rig.previousOutputs.assign(rig.outputs.size(), 0.0f);

        const json::Value& vertices = src["Vertices"];
        for (size_t i = 0; i < vertices.size(); ++i) {
            const json::Value& v = vertices.at(i);
            Particle p;
            p.mobility = float(v["Mobility"].num(1.0));
            p.delay = float(v["Delay"].num(1.0));
            p.acceleration = float(v["Acceleration"].num(1.0));
            p.radius = float(v["Radius"].num(0.0));
            p.initialPosition = V{float(v["Position"]["X"].num(0.0)), float(v["Position"]["Y"].num(0.0))};
            rig.particles.push_back(p);
        }

        subRigs_.push_back(std::move(rig));
    }

    if (missing > 0) logLine("物理：physics3.json 有 %d 个参数在模型里不存在，已忽略对应输入/输出", missing);
    initialize();
    return true;
}

void Physics::initialize() {
    for (SubRig& rig : subRigs_) {
        if (rig.particles.empty()) continue;
        Particle& top = rig.particles[0];
        top.initialPosition = V{0.0f, 0.0f};
        top.position = top.initialPosition;
        top.lastPosition = top.initialPosition;
        top.lastGravity = V{0.0f, 1.0f};
        top.velocity = V{0.0f, 0.0f};
        top.force = V{0.0f, 0.0f};

        for (size_t i = 1; i < rig.particles.size(); ++i) {
            Particle& p = rig.particles[i];
            p.initialPosition = add(rig.particles[i - 1].initialPosition, V{0.0f, p.radius});
            p.position = p.initialPosition;
            p.lastPosition = p.initialPosition;
            p.lastGravity = V{0.0f, 1.0f};
            p.velocity = V{0.0f, 0.0f};
            p.force = V{0.0f, 0.0f};
        }
        std::fill(rig.currentOutputs.begin(), rig.currentOutputs.end(), 0.0f);
        std::fill(rig.previousOutputs.begin(), rig.previousOutputs.end(), 0.0f);
    }
    remainTime_ = 0.0f;
}

void Physics::updateRig(SubRig& rig, const float* parameterValues, const float* parameterMinimumValues,
                        const float* parameterMaximumValues, const float* parameterDefaultValues, float* parameterCaches,
                        float physicsDeltaTime) {
    V totalTranslation{0.0f, 0.0f};
    float totalAngle = 0.0f;

    // 读取输入（用 cache 而不是模型原值，保证组与组之间的传播顺序）
    for (const Input& input : rig.inputs) {
        if (input.sourceIndex < 0) continue;
        const float weight = input.weight / kMaximumWeight;
        inputNormalizedValue(totalTranslation, totalAngle, input.type, parameterCaches[input.sourceIndex],
                             parameterMinimumValues[input.sourceIndex], parameterMaximumValues[input.sourceIndex],
                             parameterDefaultValues[input.sourceIndex], rig.normalizationPosition.minimum,
                             rig.normalizationPosition.maximum, rig.normalizationPosition.defaultValue,
                             rig.normalizationAngle.minimum, rig.normalizationAngle.maximum,
                             rig.normalizationAngle.defaultValue, input.reflect, weight);
    }

    // 与官方一致：旋转角为 -totalAngle，且 Y 用已旋转的 X 计算
    const float radAngle = degreesToRadian(-totalAngle);
    const float tx = (totalTranslation.x * std::cos(radAngle) - totalTranslation.y * std::sin(radAngle));
    const float ty = (totalTranslation.x * std::sin(radAngle) + totalTranslation.y * std::cos(radAngle));
    totalTranslation = V{tx, ty};

    updateParticles(rig.particles, totalTranslation, totalAngle, V{wind_.x, wind_.y},
                    kMovementThreshold * rig.normalizationPosition.maximum, physicsDeltaTime, kAirResistance);

    for (size_t i = 0; i < rig.outputs.size(); ++i) {
        Output& output = rig.outputs[i];
        if (output.destinationIndex < 0) continue;
        const int particleIndex = output.vertexIndex;
        if (particleIndex < 1 || particleIndex >= int(rig.particles.size())) continue;

        const V translation = sub(rig.particles[size_t(particleIndex)].position,
                                  rig.particles[size_t(particleIndex - 1)].position);
        const float value = outputValue(output.type, translation, rig.particles, particleIndex, output.reflect,
                                        V{gravity_.x, gravity_.y});
        rig.currentOutputs[i] = value;

        updateOutputParameterValue(&parameterCaches[output.destinationIndex],
                                   parameterMinimumValues[output.destinationIndex],
                                   parameterMaximumValues[output.destinationIndex], value,
                                   outputScale(output.type, output.scale), output.weight, &output.valueBelowMinimum,
                                   &output.valueExceededMaximum);
    }
}

void Physics::interpolate(float* parameterValues, const float* parameterMinimumValues,
                          const float* parameterMaximumValues, float weight) {
    for (SubRig& rig : subRigs_) {
        for (size_t i = 0; i < rig.outputs.size(); ++i) {
            Output& output = rig.outputs[i];
            if (output.destinationIndex < 0) continue;
            const float value = rig.previousOutputs[i] * (1.0f - weight) + rig.currentOutputs[i] * weight;
            updateOutputParameterValue(&parameterValues[output.destinationIndex],
                                       parameterMinimumValues[output.destinationIndex],
                                       parameterMaximumValues[output.destinationIndex], value,
                                       outputScale(output.type, output.scale), output.weight,
                                       &output.valueBelowMinimum, &output.valueExceededMaximum);
        }
    }
}

void Physics::evaluate(CubismCore& core, CsmModel* model, float dt) {
    if (!enabled_ || !valid() || !model || dt <= 0.0f) return;
    if (!core.GetParameterValues || !core.GetParameterCount) return;

    float* parameterValues = core.GetParameterValues(model);
    const int count = core.GetParameterCount(model);
    if (!parameterValues || count <= 0) return;
    const float* parameterMaximumValues = core.GetParameterMaximumValues(model);
    const float* parameterMinimumValues = core.GetParameterMinimumValues(model);
    const float* parameterDefaultValues = core.GetParameterDefaultValues(model);
    if (!parameterMaximumValues || !parameterMinimumValues || !parameterDefaultValues) return;

    if (int(parameterCaches_.size()) < count) {
        parameterCaches_.resize(size_t(count));
        parameterInputCaches_.resize(size_t(count));
        for (int i = 0; i < count; ++i) parameterInputCaches_[size_t(i)] = parameterValues[i];
    }

    remainTime_ += dt;
    // 官方行为：单次暂停超过 5 秒就丢弃这段时间，避免恢复时粒子爆炸
    if (remainTime_ > kMaxDeltaTime) remainTime_ = 0.0f;

    const float physicsDeltaTime = fps_ > 0.0f ? 1.0f / fps_ : dt;
    if (physicsDeltaTime <= 0.0f) return;

    // 按 physics3.json 的 Fps 固定步长推进；剩下不足一步的时间用于插值
    while (remainTime_ >= physicsDeltaTime) {
        for (SubRig& rig : subRigs_) rig.previousOutputs = rig.currentOutputs;

        // 输入在“上一次采样”和当前值之间按时间线性插值
        const float inputWeight = physicsDeltaTime / remainTime_;
        for (int i = 0; i < count; ++i) {
            parameterCaches_[size_t(i)] = parameterInputCaches_[size_t(i)] * (1.0f - inputWeight) +
                                          parameterValues[i] * inputWeight;
            parameterInputCaches_[size_t(i)] = parameterCaches_[size_t(i)];
        }

        for (SubRig& rig : subRigs_)
            updateRig(rig, parameterValues, parameterMinimumValues, parameterMaximumValues, parameterDefaultValues,
                      parameterCaches_.data(), physicsDeltaTime);

        remainTime_ -= physicsDeltaTime;
    }

    interpolate(parameterValues, parameterMinimumValues, parameterMaximumValues, remainTime_ / physicsDeltaTime);
}

}  // namespace fx
