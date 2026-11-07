#include "l2d_core.hpp"

#include <cstdlib>
#include <vector>

#include "config.hpp"
#include "platform/paths.hpp"

namespace fx {

// 官方 SDK 提供的是静态库；此时直接链接 C API，无需 dlopen
#ifdef FXPET_CUBISM_STATIC
extern "C" {
CsmMoc* csmReviveMocInPlace(void*, unsigned int);
unsigned int csmGetSizeofModel(const CsmMoc*);
CsmModel* csmInitializeModelInPlace(const CsmMoc*, void*, unsigned int);
void csmUpdateModel(CsmModel*);
void csmReadCanvasInfo(const CsmModel*, CsmVector2*, CsmVector2*, float*);
void csmResetDrawableDynamicFlags(CsmModel*);
int csmGetParameterCount(const CsmModel*);
const char** csmGetParameterIds(const CsmModel*);
const float* csmGetParameterMinimumValues(const CsmModel*);
const float* csmGetParameterMaximumValues(const CsmModel*);
const float* csmGetParameterDefaultValues(const CsmModel*);
float* csmGetParameterValues(CsmModel*);
int csmGetPartCount(const CsmModel*);
const char** csmGetPartIds(const CsmModel*);
float* csmGetPartOpacities(CsmModel*);
int csmGetDrawableCount(const CsmModel*);
const char** csmGetDrawableIds(const CsmModel*);
const int* csmGetDrawableTextureIndices(const CsmModel*);
const uint8_t* csmGetDrawableConstantFlags(const CsmModel*);
const uint8_t* csmGetDrawableDynamicFlags(const CsmModel*);
const float* csmGetDrawableOpacities(const CsmModel*);
const int* csmGetDrawableMaskCounts(const CsmModel*);
const int** csmGetDrawableMasks(const CsmModel*);
const int* csmGetDrawableVertexCounts(const CsmModel*);
const CsmVector2** csmGetDrawableVertexPositions(const CsmModel*);
const CsmVector2** csmGetDrawableVertexUvs(const CsmModel*);
const int* csmGetDrawableIndexCounts(const CsmModel*);
const unsigned short** csmGetDrawableIndices(const CsmModel*);
const CsmVector4* csmGetDrawableMultiplyColors(const CsmModel*);
const CsmVector4* csmGetDrawableScreenColors(const CsmModel*);
const int* csmGetDrawableRenderOrders(const CsmModel*);
const int* csmGetDrawableParentPartIndices(const CsmModel*);
const int* csmGetPartParentPartIndices(const CsmModel*);
}
#endif

#ifndef FXPET_CUBISM_STATIC
static std::vector<std::string> candidateLibs() { return platform::cubismLibraryCandidates(); }
#endif

bool CubismCore::load(std::string* error) {
#ifdef FXPET_CUBISM_STATIC
    staticLinked = true;  // 标记为已就绪
    path = "静态链接 (libLive2DCubismCore)";
    ReviveMocInPlace = &csmReviveMocInPlace;
    GetSizeofModel = &csmGetSizeofModel;
    InitializeModelInPlace = &csmInitializeModelInPlace;
    UpdateModel = &csmUpdateModel;
    ReadCanvasInfo = &csmReadCanvasInfo;
    ResetDrawableDynamicFlags = &csmResetDrawableDynamicFlags;
    GetParameterCount = &csmGetParameterCount;
    GetParameterIds = &csmGetParameterIds;
    GetParameterMinimumValues = &csmGetParameterMinimumValues;
    GetParameterMaximumValues = &csmGetParameterMaximumValues;
    GetParameterDefaultValues = &csmGetParameterDefaultValues;
    GetParameterValues = &csmGetParameterValues;
    GetPartCount = &csmGetPartCount;
    GetPartIds = &csmGetPartIds;
    GetPartOpacities = &csmGetPartOpacities;
    GetDrawableCount = &csmGetDrawableCount;
    GetDrawableIds = &csmGetDrawableIds;
    GetDrawableTextureIndices = &csmGetDrawableTextureIndices;
    GetDrawableConstantFlags = &csmGetDrawableConstantFlags;
    GetDrawableDynamicFlags = &csmGetDrawableDynamicFlags;
    GetDrawableOpacities = &csmGetDrawableOpacities;
    GetDrawableMaskCounts = &csmGetDrawableMaskCounts;
    GetDrawableMasks = &csmGetDrawableMasks;
    GetDrawableVertexCounts = &csmGetDrawableVertexCounts;
    GetDrawableVertexPositions = &csmGetDrawableVertexPositions;
    GetDrawableVertexUvs = &csmGetDrawableVertexUvs;
    GetDrawableIndexCounts = &csmGetDrawableIndexCounts;
    GetDrawableIndices = &csmGetDrawableIndices;
    GetDrawableMultiplyColors = &csmGetDrawableMultiplyColors;
    GetDrawableScreenColors = &csmGetDrawableScreenColors;
    GetDrawableRenderOrders = &csmGetDrawableRenderOrders;
    GetDrawableParentPartIndices = &csmGetDrawableParentPartIndices;
    GetPartParentPartIndices = &csmGetPartParentPartIndices;
    (void)error;
    return true;
#else
    const std::vector<std::string> candidates = candidateLibs();
    for (const std::string& cand : candidates) {
        if (lib.open(cand, nullptr)) {
            path = cand;
            break;
        }
    }
    if (!lib.valid()) {
        if (error) {
            *error = "找不到 Live2D Cubism Core 库。请下载官方 CubismSdkForNative 后把 "
                     "Live2DCubismCore.dll（Windows）或 libLive2DCubismCore.so（Linux）放到 lib/，"
                     "或设置环境变量 FXPET_CUBISM_CORE 指向它。";
        }
        return false;
    }

    struct Slot {
        const char* name;
        void** target;
    };
    const Slot slots[] = {
        {"csmReviveMocInPlace", reinterpret_cast<void**>(&ReviveMocInPlace)},
        {"csmGetSizeofModel", reinterpret_cast<void**>(&GetSizeofModel)},
        {"csmInitializeModelInPlace", reinterpret_cast<void**>(&InitializeModelInPlace)},
        {"csmUpdateModel", reinterpret_cast<void**>(&UpdateModel)},
        {"csmReadCanvasInfo", reinterpret_cast<void**>(&ReadCanvasInfo)},
        {"csmResetDrawableDynamicFlags", reinterpret_cast<void**>(&ResetDrawableDynamicFlags)},
        {"csmGetParameterCount", reinterpret_cast<void**>(&GetParameterCount)},
        {"csmGetParameterIds", reinterpret_cast<void**>(&GetParameterIds)},
        {"csmGetParameterMinimumValues", reinterpret_cast<void**>(&GetParameterMinimumValues)},
        {"csmGetParameterMaximumValues", reinterpret_cast<void**>(&GetParameterMaximumValues)},
        {"csmGetParameterDefaultValues", reinterpret_cast<void**>(&GetParameterDefaultValues)},
        {"csmGetParameterValues", reinterpret_cast<void**>(&GetParameterValues)},
        {"csmGetPartCount", reinterpret_cast<void**>(&GetPartCount)},
        {"csmGetPartIds", reinterpret_cast<void**>(&GetPartIds)},
        {"csmGetPartOpacities", reinterpret_cast<void**>(&GetPartOpacities)},
        {"csmGetDrawableCount", reinterpret_cast<void**>(&GetDrawableCount)},
        {"csmGetDrawableIds", reinterpret_cast<void**>(&GetDrawableIds)},
        {"csmGetDrawableTextureIndices", reinterpret_cast<void**>(&GetDrawableTextureIndices)},
        {"csmGetDrawableConstantFlags", reinterpret_cast<void**>(&GetDrawableConstantFlags)},
        {"csmGetDrawableDynamicFlags", reinterpret_cast<void**>(&GetDrawableDynamicFlags)},
        {"csmGetDrawableOpacities", reinterpret_cast<void**>(&GetDrawableOpacities)},
        {"csmGetDrawableMaskCounts", reinterpret_cast<void**>(&GetDrawableMaskCounts)},
        {"csmGetDrawableMasks", reinterpret_cast<void**>(&GetDrawableMasks)},
        {"csmGetDrawableVertexCounts", reinterpret_cast<void**>(&GetDrawableVertexCounts)},
        {"csmGetDrawableVertexPositions", reinterpret_cast<void**>(&GetDrawableVertexPositions)},
        {"csmGetDrawableVertexUvs", reinterpret_cast<void**>(&GetDrawableVertexUvs)},
        {"csmGetDrawableIndexCounts", reinterpret_cast<void**>(&GetDrawableIndexCounts)},
        {"csmGetDrawableIndices", reinterpret_cast<void**>(&GetDrawableIndices)},
        {"csmGetDrawableMultiplyColors", reinterpret_cast<void**>(&GetDrawableMultiplyColors)},
        {"csmGetDrawableScreenColors", reinterpret_cast<void**>(&GetDrawableScreenColors)},
        {"csmGetDrawableRenderOrders", reinterpret_cast<void**>(&GetDrawableRenderOrders)},
        {"csmGetDrawableParentPartIndices", reinterpret_cast<void**>(&GetDrawableParentPartIndices)},
        {"csmGetPartParentPartIndices", reinterpret_cast<void**>(&GetPartParentPartIndices)},
    };
    for (const Slot& s : slots) {
        void* sym = lib.sym(s.name);
        if (!sym) {
            if (error) *error = std::string("Core 缺少符号: ") + s.name + "（" + path + "）";
            lib.close();
            return false;
        }
        *s.target = sym;
    }
    return true;
#endif
}

}  // namespace fx
