#include "l2d_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.hpp"
#include "json.hpp"

namespace fx {

static std::string dirName(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

static bool readWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return false;
    }
    out.resize(size_t(size));
    const size_t got = std::fread(out.data(), 1, size_t(size), f);
    std::fclose(f);
    return got == size_t(size);
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool Live2DModel::load(CubismCore& core, GLContext& gl, const std::string& model3Path, std::string* error) {
    destroy(gl);
    core_ = &core;
    const std::string base = dirName(model3Path);

    json::Value root;
    if (!json::loadFile(model3Path, root, error)) return false;
    const json::Value& refs = root["FileReferences"];
    if (!refs.isObject()) {
        if (error) *error = "model3.json 缺少 FileReferences: " + model3Path;
        return false;
    }

    const std::string mocPath = base + "/" + refs["Moc"].str();
    std::vector<uint8_t> mocBytes;
    if (!readWholeFile(mocPath, mocBytes)) {
        if (error) *error = "读取 moc3 失败: " + mocPath;
        return false;
    }
    if (posix_memalign(&mocBuf_, 64, (mocBytes.size() + 63) / 64 * 64) != 0) {
        if (error) *error = "内存分配失败";
        destroy(gl);
        return false;
    }
    std::memcpy(mocBuf_, mocBytes.data(), mocBytes.size());

    CsmMoc* moc = core.ReviveMocInPlace(mocBuf_, unsigned(mocBytes.size()));
    if (!moc) {
        if (error) *error = "moc3 解析失败（Core 版本不支持）: " + mocPath;
        destroy(gl);
        return false;
    }
    const unsigned modelSize = core.GetSizeofModel(moc);
    if (posix_memalign(&modelBuf_, 64, (modelSize + 63) / 64 * 64) != 0) {
        if (error) *error = "内存分配失败";
        destroy(gl);
        return false;
    }
    model_ = core.InitializeModelInPlace(moc, modelBuf_, modelSize);
    if (!model_) {
        if (error) *error = "模型初始化失败: " + mocPath;
        destroy(gl);
        return false;
    }

    CsmVector2 size{}, origin{};
    core.ReadCanvasInfo(model_, &size, &origin, &pixelsPerUnit_);
    canvasW_ = size.x > 0 ? size.x : 1.0f;
    canvasH_ = size.y > 0 ? size.y : 1.0f;
    originX_ = origin.x;
    originY_ = origin.y;

    // 纹理
    const json::Value& texArr = refs["Textures"];
    for (size_t i = 0; i < texArr.size(); ++i) {
        std::string texPath = base + "/" + texArr.at(i).str();
        std::string err;
        Texture t = gl.loadTexture(texPath, &err);
        if (!t.valid()) {
            if (error) *error = err;
            destroy(gl);
            return false;
        }
        textures_.push_back(t);
    }

    // 参数 / 部件索引表
    const int paramCount = core.GetParameterCount(model_);
    const char** paramIds = core.GetParameterIds(model_);
    for (int i = 0; i < paramCount; ++i) paramIndex_[paramIds[i]] = i;

    const int partCount = core.GetPartCount(model_);
    const char** partIds = core.GetPartIds(model_);
    for (int i = 0; i < partCount; ++i) partIndex_[partIds[i]] = i;

    // drawable
    drawableCount_ = core.GetDrawableCount(model_);
    const int* texIndices = core.GetDrawableTextureIndices(model_);
    const uint8_t* flags = core.GetDrawableConstantFlags(model_);
    const int* maskCounts = core.GetDrawableMaskCounts(model_);
    const int** masks = core.GetDrawableMasks(model_);
    const int* vertexCounts = core.GetDrawableVertexCounts(model_);
    const int* indexCounts = core.GetDrawableIndexCounts(model_);
    const unsigned short** indices = core.GetDrawableIndices(model_);
    const int* renderOrders = core.GetDrawableRenderOrders(model_);

    drawables_.resize(size_t(drawableCount_));
    for (int i = 0; i < drawableCount_; ++i) {
        Drawable& d = drawables_[size_t(i)];
        d.constantFlags = flags[i];
        d.vertexCount = vertexCounts[i];
        d.indexCount = indexCounts[i];
        d.maskCount = maskCounts[i];
        d.renderOrder = renderOrders[i];
        d.textureIndex = texIndices[i];
        for (int m = 0; m < d.maskCount; ++m) d.masks.push_back(masks[i][m]);
        d.indices.assign(indices[i], indices[i] + d.indexCount);
        d.tmpPos.resize(size_t(d.vertexCount) * 2);
        drawOrder_.push_back(i);
    }
    {
        const int* dpp = core.GetDrawableParentPartIndices(model_);
        const int* ppp = core.GetPartParentPartIndices(model_);
        drawableParentPart_.assign(dpp, dpp + drawableCount_);
        partParentPart_.assign(ppp, ppp + partCount);
    }
    std::stable_sort(drawOrder_.begin(), drawOrder_.end(),
                     [this](int a, int b) { return drawables_[size_t(a)].renderOrder < drawables_[size_t(b)].renderOrder; });

    applyDefaults();
    core.UpdateModel(model_);

    // 判断顶点坐标空间（画布坐标 0..size 还是以中心为原点）
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    const CsmVector2** verts = core.GetDrawableVertexPositions(model_);
    for (int i = 0; i < drawableCount_; ++i) {
        for (int v = 0; v < vertexCounts[i]; ++v) {
            minX = std::min(minX, verts[i][v].x);
            maxX = std::max(maxX, verts[i][v].x);
            minY = std::min(minY, verts[i][v].y);
            maxY = std::max(maxY, verts[i][v].y);
        }
    }
    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;
    coordCentered_ = (std::fabs(cx) < canvasW_ * 0.3f && std::fabs(cy) < canvasH_ * 0.3f);
    logLine("模型画布 %.1fx%.1f origin=(%.1f,%.1f) ppu=%.3f 顶点范围 x[%.1f,%.1f] y[%.1f,%.1f] %s", canvasW_,
            canvasH_, originX_, originY_, pixelsPerUnit_, minX, maxX, minY, maxY,
            coordCentered_ ? "(中心原点)" : "(画布原点)");
    return true;
}

void Live2DModel::freeBuffers() {
    std::free(mocBuf_);
    std::free(modelBuf_);
    mocBuf_ = nullptr;
    modelBuf_ = nullptr;
    model_ = nullptr;
}

void Live2DModel::destroy(GLContext& gl) {
    for (Texture& t : textures_) gl.destroyTexture(t);
    textures_.clear();
    freeBuffers();
    drawables_.clear();
    drawableParentPart_.clear();
    partParentPart_.clear();
    drawOrder_.clear();
    paramIndex_.clear();
    partIndex_.clear();
    drawableCount_ = 0;
}

float Live2DModel::effectiveOpacity(int drawableIndex, const float* drawableOpacities) const {
    float op = drawableOpacities[drawableIndex];
    if (partParentPart_.empty()) return op;
    const float* partOpacities = core_->GetPartOpacities(model_);
    int part = drawableParentPart_[size_t(drawableIndex)];
    int guard = 0;
    while (part >= 0 && part < int(partParentPart_.size()) && guard++ < 64) {
        op *= partOpacities[part];
        part = partParentPart_[size_t(part)];
    }
    return op;
}

void Live2DModel::applyDefaults() {
    // 由我们驱动的参数先回到中立值，避免残留上一个状态
    const char* neutral[] = {"ParamAngleX", "ParamAngleY", "ParamAngleZ", "ParamEyeBallX", "ParamEyeBallY", "ParamBodyAngleX",
                             "ParamBodyAngleY", "ParamBodyAngleZ", "ParamMouthOpenY", "ParamEyeLSmile", "ParamEyeRSmile",
                             "ParamBrowLY", "ParamBrowRY", "ParamBrowLAngle", "ParamBrowRAngle", "ParamBrowLForm",
                             "ParamBrowRForm", "ParamHairFront", "ParamHairSide", "ParamHairBack", nullptr};
    for (int i = 0; neutral[i]; ++i) setParam(neutral[i], 0.0f);
}

bool Live2DModel::hasParam(const std::string& id) const { return paramIndex_.count(id) != 0; }

void Live2DModel::setParam(const std::string& id, float value) {
    if (!model_) return;
    auto it = paramIndex_.find(id);
    if (it == paramIndex_.end()) return;
    float* values = core_->GetParameterValues(model_);
    const float* mins = core_->GetParameterMinimumValues(model_);
    const float* maxs = core_->GetParameterMaximumValues(model_);
    const int idx = it->second;
    values[idx] = clampf(value, mins[idx], maxs[idx]);
}

float Live2DModel::getParam(const std::string& id) const {
    if (!model_) return 0.0f;
    auto it = paramIndex_.find(id);
    if (it == paramIndex_.end()) return 0.0f;
    return core_->GetParameterValues(model_)[it->second];
}

void Live2DModel::setPartOpacity(const std::string& id, float opacity) {
    if (!model_) return;
    auto it = partIndex_.find(id);
    if (it == partIndex_.end()) return;
    core_->GetPartOpacities(model_)[it->second] = clampf(opacity, 0.0f, 1.0f);
}

void Live2DModel::setLook(float x, float y) {
    lookX_ = clampf(x, -1.0f, 1.0f);
    lookY_ = clampf(y, -1.0f, 1.0f);
}

void Live2DModel::setMouthOpen(float v) { mouthOpen_ = clampf(v, 0.0f, 1.0f); }

void Live2DModel::setEyeSmile(float v) { eyeSmile_ = clampf(v, 0.0f, 1.0f); }

void Live2DModel::setBodyAngle(float x, float y) {
    bodyX_ = clampf(x, -1.0f, 1.0f);
    bodyY_ = clampf(y, -1.0f, 1.0f);
}

void Live2DModel::setMousePosition(float x, float y) {
    mouseX_ = clampf(x, -1.0f, 1.0f);
    mouseY_ = clampf(y, -1.0f, 1.0f);
}

void Live2DModel::setMouseButtons(bool left, bool right) {
    mouseLeftDown_ = left;
    mouseRightDown_ = right;
}

void Live2DModel::update(float dt) {
    if (!model_) return;
    time_ += dt;

    // 自动眨眼
    if (autoBlink_) {
        blinkTimer_ -= dt;
        if (blinkTimer_ <= 0.0f) {
            blinkPhase_ = 0.0f;
            blinkTimer_ = 2.2f + float(std::rand() % 300) / 100.0f;
        }
        if (blinkPhase_ >= 0.0f) {
            blinkPhase_ += dt;
            const float down = 0.07f, hold = 0.04f, up = 0.10f;
            if (blinkPhase_ < down) {
                blink_ = 1.0f - blinkPhase_ / down;
            } else if (blinkPhase_ < down + hold) {
                blink_ = 0.0f;
            } else if (blinkPhase_ < down + hold + up) {
                blink_ = (blinkPhase_ - down - hold) / up;
            } else {
                blink_ = 1.0f;
                blinkPhase_ = -1.0f;
            }
        }
    } else {
        blink_ = 1.0f;
    }

    const float breath = breath_ ? (0.5f + 0.5f * std::sin(time_ * 1.6f)) : 0.0f;

    setParam("ParamAngleX", lookX_ * 30.0f);
    setParam("ParamAngleY", -lookY_ * 30.0f);
    setParam("ParamAngleZ", -lookX_ * 8.0f);
    setParam("ParamEyeBallX", lookX_);
    setParam("ParamEyeBallY", -lookY_);
    setParam("ParamBodyAngleX", bodyX_ * 10.0f);
    setParam("ParamBodyAngleY", bodyY_ * 10.0f);
    setParam("ParamBodyAngleZ", lookX_ * 5.0f);
    setParam("ParamMouseX", mouseX_ * 30.0f);
    setParam("ParamMouseY", mouseY_ * 30.0f);
    setParam("ParamMouseLeftDown", mouseLeftDown_ ? 1.0f : 0.0f);
    setParam("ParamMouseRightDown", mouseRightDown_ ? 1.0f : 0.0f);
    setParam("ParamBreath", breath);
    // 常规语义：1 = 睁开，0 = 闭合
    setParam("ParamEyeLOpen", blink_);
    setParam("ParamEyeROpen", blink_);
    setParam("ParamEyeLSmile", eyeSmile_);
    setParam("ParamEyeRSmile", eyeSmile_);
    setParam("ParamMouthOpenY", mouthOpen_);
    setParam("ParamHairFront", std::sin(time_ * 1.1f) * 0.35f);
    setParam("ParamHairSide", std::sin(time_ * 0.9f + 1.0f) * 0.3f);
    setParam("ParamHairBack", std::sin(time_ * 0.8f + 2.0f) * 0.25f);

    // 外部钉住的参数必须在 UpdateModel 之前写，否则形变不会生效
    for (const auto& kv : pinnedParams_) setParam(kv.first, kv.second);

    core_->UpdateModel(model_);
}

void Live2DModel::dump() const {
    if (!model_) return;
    const int pc = core_->GetParameterCount(model_);
    const char** pids = core_->GetParameterIds(model_);
    const float* pdef = core_->GetParameterDefaultValues(model_);
    const float* pmin = core_->GetParameterMinimumValues(model_);
    const float* pmax = core_->GetParameterMaximumValues(model_);
    std::printf("---- parameters (%d)\n", pc);
    for (int i = 0; i < pc; ++i) std::printf("  %-32s def=%7.3f min=%7.3f max=%7.3f\n", pids[i], pdef[i], pmin[i], pmax[i]);
    const int partc = core_->GetPartCount(model_);
    const char** partIds = core_->GetPartIds(model_);
    const float* po = core_->GetPartOpacities(model_);
    std::printf("---- parts (%d)\n", partc);
    for (int i = 0; i < partc; ++i) std::printf("  %-16s opacity=%.3f\n", partIds[i], po[i]);
    const char** dids = core_->GetDrawableIds(model_);
    const uint8_t* dyn = core_->GetDrawableDynamicFlags(model_);
    const float* dop = core_->GetDrawableOpacities(model_);
    const int* orders = core_->GetDrawableRenderOrders(model_);
    std::printf("---- drawables (%d)\n", drawableCount_);
    for (int i = 0; i < drawableCount_; ++i) if (drawables_[size_t(i)].maskCount > 0) {
        std::printf("  mask: %d %s inverted=%d masks=[", i, dids[i], (drawables_[size_t(i)].constantFlags & kIsInvertedMask) ? 1 : 0);
        for (int m : drawables_[size_t(i)].masks) std::printf("%d ", m);
        std::printf("]\n");
    }
    for (int i = 0; i < drawableCount_; ++i) {
        const int ppi = drawableParentPart_.empty() ? -1 : drawableParentPart_[size_t(i)];
        std::printf("  [%3d] %-20s visible=%d opacity=%.3f eff=%.3f order=%d masks=%d part=%s\n", i, dids[i],
                    (dyn[i] & kIsVisible) ? 1 : 0, dop[i], effectiveOpacity(i, dop), orders[i],
                    drawables_[size_t(i)].maskCount,
                    ppi < 0 || ppi >= partc ? "-" : partIds[ppi]);
        if (getenv("FX_GEOM") && (i == 72 || i == 74)) {
            const CsmVector2** vv = core_->GetDrawableVertexPositions(model_);
            const CsmVector2** uu = core_->GetDrawableVertexUvs(model_);
            const int* vc = core_->GetDrawableVertexCounts(model_);
            const int* ic = core_->GetDrawableIndexCounts(model_);
            const int* ti = core_->GetDrawableTextureIndices(model_);
            std::printf("GEOM [%d] tex=%d vtx=%d idx=%d\n", i, ti[i], vc[i], ic[i]);
            for (int v = 0; v < (vc[i] < 4 ? vc[i] : 4); ++v)
                std::printf("   v%d pos=(%.2f,%.2f) uv=(%.4f,%.4f)\n", v, vv[i][v].x, vv[i][v].y, uu[i][v].x, uu[i][v].y);
        }
    }
}

Live2DModel::Placement Live2DModel::placement(float winW, float winH) const {
    Placement p;
    if (canvasW_ <= 0.0f || canvasH_ <= 0.0f || winW <= 0.0f || winH <= 0.0f) {
        p.w = winW;
        p.h = winH;
        return p;
    }
    // 等比缩放到窗口内，再乘整体缩放系数；偏移按窗口尺寸归一化
    const float sx = winW / canvasW_;
    const float sy = winH / canvasH_;
    const float s = std::min(sx, sy) * (layout_.correct > 0.0f ? layout_.correct : 1.0f);
    p.w = canvasW_ * s;
    p.h = canvasH_ * s;
    p.x = (winW - p.w) * 0.5f + layout_.offsetX * winW;
    p.y = (winH - p.h) * 0.5f + layout_.offsetY * winH;
    if (getenv("FX_LAYOUT"))
        std::fprintf(stderr, "[layout] canvas=%.1fx%.1f win=%.1fx%.1f dest=%.2fx%.2f at (%.2f,%.2f) correct=%.3f off=(%.3f,%.3f)\n",
                     canvasW_, canvasH_, winW, winH, p.w, p.h, p.x, p.y, layout_.correct, layout_.offsetX, layout_.offsetY);
    return p;
}

void Live2DModel::render(GLContext& gl, float x, float y, float w, float h, bool flipX, float opacity) {
    if (!model_) return;

    // 画布坐标 y 向上、原点左下
    const Placement p = placement(w, h);
    const float destW = p.w;
    const float destH = p.h;
    const float dx = x + p.x;
    const float dy = y + p.y;

    const CsmVector2** verts = core_->GetDrawableVertexPositions(model_);
    const CsmVector2** uvs = core_->GetDrawableVertexUvs(model_);
    const float* opacities = core_->GetDrawableOpacities(model_);
    const uint8_t* dynFlags = core_->GetDrawableDynamicFlags(model_);
    const CsmVector4* multiply = core_->GetDrawableMultiplyColors(model_);
    const CsmVector4* screenColors = core_->GetDrawableScreenColors(model_);
    const int* renderOrders = core_->GetDrawableRenderOrders(model_);

    // renderOrder 会随参数在 csmUpdateModel() 后变化，每帧都要按最新值重排。
    for (int i = 0; i < drawableCount_; ++i) {
        const int order = renderOrders[i];
        if (order >= 0 && order < drawableCount_) drawOrder_[size_t(order)] = i;
    }
    // Core 给出的顶点是模型单位（乘 pixelsPerUnit 后才是画布像素）
    const float coordOffX = coordCentered_ ? canvasW_ * 0.5f : 0.0f;
    const float coordOffY = coordCentered_ ? canvasH_ * 0.5f : 0.0f;
    const float unitScale = pixelsPerUnit_ > 0.0f ? pixelsPerUnit_ : 1.0f;
    auto toWindowX = [&](float vx) {
        const float canvasX = vx * unitScale + coordOffX;
        const float t = canvasX / canvasW_;
        return dx + (flipX ? (1.0f - t) : t) * destW;
    };
    auto toWindowY = [&](float vy) {
        const float canvasY = vy * unitScale + coordOffY;
        return dy + (1.0f - canvasY / canvasH_) * destH;
    };

    // 调试用：部件透明度覆盖
    if (!partOverrides_.empty()) {
        float* po = core_->GetPartOpacities(model_);
        const char** partIds = core_->GetPartIds(model_);
        for (int i = 0; i < core_->GetPartCount(model_); ++i) {
            auto it = partOverrides_.find(partIds[i]);
            if (it != partOverrides_.end()) po[i] = it->second;
        }
    }

    for (int di : drawOrder_) {
        Drawable& d = drawables_[size_t(di)];
        if (!onlyDrawables_.empty() && std::find(onlyDrawables_.begin(), onlyDrawables_.end(), di) == onlyDrawables_.end()) continue;
        if (std::find(hiddenDrawables_.begin(), hiddenDrawables_.end(), di) != hiddenDrawables_.end()) continue;
        if (std::find(runtimeHidden_.begin(), runtimeHidden_.end(), di) != runtimeHidden_.end()) continue;
        if (respectVisible_ && !(dynFlags[di] & kIsVisible)) continue;
        // drawable 不透明度 × 父部件不透明度链（Cubism 的部件显隐就靠这一层）
        float baseAlpha = effectiveOpacity(di, opacities);
        if (forceFull_) baseAlpha = std::max(1.0f, baseAlpha);
        else if (forceOpaque_) baseAlpha = 1.0f;
        const float alpha = baseAlpha * opacity;
        if (alpha <= 0.001f) continue;
        if (d.vertexCount <= 0 || d.indexCount <= 0) continue;
        const int tex = d.textureIndex;
        if (tex < 0 || tex >= int(textures_.size())) continue;

        // 顶点 → 窗口像素
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (int v = 0; v < d.vertexCount; ++v) {
            const float px = toWindowX(verts[di][v].x);
            const float py = toWindowY(verts[di][v].y);
            d.tmpPos[size_t(v) * 2 + 0] = px;
            d.tmpPos[size_t(v) * 2 + 1] = py;
            minX = std::min(minX, px);
            maxX = std::max(maxX, px);
            minY = std::min(minY, py);
            maxY = std::max(maxY, py);
        }

        // UV 直接拷贝（一次性转换到 float 缓冲）
        uvScratch_.resize(size_t(d.vertexCount) * 2);
        for (int v = 0; v < d.vertexCount; ++v) {
            uvScratch_[size_t(v) * 2 + 0] = uvs[di][v].x;
            uvScratch_[size_t(v) * 2 + 1] = flipV_ ? 1.0f - uvs[di][v].y : uvs[di][v].y;
        }

        const bool inverted = forceInvertedMask_ && (d.constantFlags & kIsInvertedMask) != 0;
        bool maskActive = false;
        if (d.maskCount > 0 && !disableMasks_) {
            // 计算 drawable + 遮罩的并集包围盒（窗口坐标）
            float ux0 = minX, uy0 = minY, ux1 = maxX, uy1 = maxY;
            std::vector<std::vector<float>> maskPos(size_t(d.maskCount));
            std::vector<std::vector<float>> maskUv(size_t(d.maskCount));
            std::vector<int> maskIndexCount(size_t(d.maskCount), 0);
            std::vector<int> maskTexIndex(size_t(d.maskCount), -1);
            std::vector<const uint16_t*> maskIndices(size_t(d.maskCount), nullptr);
            for (size_t k = 0; k < d.masks.size(); ++k) {
                const int mi = d.masks[k];
                if (mi < 0 || mi >= drawableCount_) continue;
                Drawable& md = drawables_[size_t(mi)];
                maskPos[k].resize(size_t(md.vertexCount) * 2);
                maskUv[k].resize(size_t(md.vertexCount) * 2);
                for (int v = 0; v < md.vertexCount; ++v) {
                    const float px = toWindowX(verts[mi][v].x);
                    const float py = toWindowY(verts[mi][v].y);
                    maskPos[k][size_t(v) * 2 + 0] = px;
                    maskPos[k][size_t(v) * 2 + 1] = py;
                    maskUv[k][size_t(v) * 2 + 0] = uvs[mi][v].x;
                    maskUv[k][size_t(v) * 2 + 1] = flipV_ ? 1.0f - uvs[mi][v].y : uvs[mi][v].y;
                    ux0 = std::min(ux0, px);
                    uy0 = std::min(uy0, py);
                    ux1 = std::max(ux1, px);
                    uy1 = std::max(uy1, py);
                }
                maskIndexCount[k] = md.indexCount;
                maskTexIndex[k] = md.textureIndex;
                maskIndices[k] = md.indices.data();
            }
            const int bx = int(std::floor(ux0)) - 2;
            const int by = int(std::floor(uy0)) - 2;
            const int bw = int(std::ceil(ux1 - ux0)) + 4;
            const int bh = int(std::ceil(uy1 - uy0)) + 4;
            const bool okBegin = (bw > 0 && bh > 0 && bw < 4096 && bh < 4096) ? gl.beginMask(bw, bh) : false;
            if (okBegin) {
                gl.setBlend(Blend::Normal);
                for (size_t k = 0; k < d.masks.size(); ++k) {
                    const int mi = d.masks[k];
                    if (mi < 0 || mi >= drawableCount_) continue;
                    if (respectVisible_ && !(dynFlags[mi] & kIsVisible)) continue;
                    if (maskTexIndex[k] < 0 || maskTexIndex[k] >= int(textures_.size())) continue;
                    float maskAlpha = effectiveOpacity(mi, opacities);
                    if (forceFull_) maskAlpha = std::max(1.0f, maskAlpha);
                    else if (forceOpaque_) maskAlpha = 1.0f;
                    if (maskAlpha <= 0.001f) continue;
                    gl.setColorParams(Color{1, 1, 1, maskAlpha}, Color{1, 1, 1, 1}, Color{0, 0, 0, 0});
                    // 顶点平移到遮罩缓冲的局部坐标
                    for (size_t vi = 0; vi < maskPos[k].size() / 2; ++vi) {
                        maskPos[k][vi * 2 + 0] -= float(bx);
                        maskPos[k][vi * 2 + 1] -= float(by);
                    }
                    gl.mesh(textures_[size_t(maskTexIndex[k])], maskPos[k].data(), maskUv[k].data(), maskIndices[k],
                            maskIndexCount[k], true);
                }
                gl.endMask();
                gl.enableMask(bx, by, bw, bh, inverted);
                maskActive = true;
            }
        }

        int blend = d.constantFlags & (kBlendAdditive | kBlendMultiplicative);
        if (blend == kBlendAdditive) {
            gl.setBlend(Blend::Additive);
        } else if (blend == kBlendMultiplicative) {
            gl.setBlend(Blend::Multiplicative);
        } else {
            gl.setBlend(Blend::Normal);
        }

        Color base{1.0f, 1.0f, 1.0f, alpha};
        Color mul{1.0f, 1.0f, 1.0f, 1.0f};
        Color scr{0.0f, 0.0f, 0.0f, 0.0f};
        if (multiply) {
            mul.r = multiply[di].x;
            mul.g = multiply[di].y;
            mul.b = multiply[di].z;
            mul.a = multiply[di].w;
        }
        if (screenColors) {
            scr.r = screenColors[di].x;
            scr.g = screenColors[di].y;
            scr.b = screenColors[di].z;
            scr.a = screenColors[di].w;
        }
        gl.setColorParams(base, mul, scr);
        gl.mesh(textures_[size_t(tex)], d.tmpPos.data(), uvScratch_.data(), d.indices.data(), d.indexCount,
                (d.constantFlags & kIsDoubleSided) != 0);

        if (maskActive) gl.disableMask();
    }
    gl.setBlend(Blend::Normal);
    gl.resetColorParams();
}

}  // namespace fx
