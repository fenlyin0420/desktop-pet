#include "l2d_core.hpp"

#include <dlfcn.h>
#include <glob.h>

#include <cstdlib>
#include <vector>

#include "config.hpp"

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
static std::string envOr(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}
#endif

#ifndef FXPET_CUBISM_STATIC
static std::vector<std::string> candidateLibs() {
    std::vector<std::string> out;
    const std::string explicitPath = envOr("FXPET_CUBISM_CORE");
    if (!explicitPath.empty()) out.push_back(explicitPath);

    std::vector<std::string> bases;
    const std::string envBase = envOr("FXPET_HOME");
    if (!envBase.empty()) bases.push_back(envBase);
    bases.push_back(exeDir());
    bases.push_back(".");
    for (const std::string& base : bases) {
        out.push_back(base + "/lib/libLive2DCubismCore.so");
        out.push_back(base + "/lib/live2d/_live2d.so");
        out.push_back(base + "/lib/live2d_pkg/live2d/_live2d.so");
    }
    out.push_back("libLive2DCubismCore.so");

    // 已安装的 live2d-py 轮子里带着官方 Core（仅在本机复用，不随仓库分发）
    const char* home = std::getenv("HOME");
    std::vector<std::string> patterns;
    if (home && *home) {
        patterns.push_back(std::string(home) + "/.local/lib/python3*/site-packages/live2d/_live2d.so");
        patterns.push_back(std::string(home) + "/.local/lib/python3*/dist-packages/live2d/_live2d.so");
        patterns.push_back(std::string(home) + "/.venv/lib/python3*/site-packages/live2d/_live2d.so");
    }
    patterns.push_back("/usr/lib/python3*/site-packages/live2d/_live2d.so");
    patterns.push_back("/usr/lib/python3/dist-packages/live2d/_live2d.so");
    patterns.push_back("/usr/local/lib/python3*/dist-packages/live2d/_live2d.so");
    patterns.push_back("/usr/local/lib/python3*/site-packages/live2d/_live2d.so");

    for (const std::string& pat : patterns) {
        glob_t g{};
        if (glob(pat.c_str(), 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) out.push_back(g.gl_pathv[i]);
        }
        globfree(&g);
    }
    out.push_back("libLive2DCubismCore.so");
    return out;
}
#endif

bool CubismCore::load(std::string* error) {
#ifdef FXPET_CUBISM_STATIC
    handle = reinterpret_cast<void*>(1);  // 标记为已就绪
    path = "静态链接 (libLive2DCubismCore.a)";
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
        // RTLD_LAZY：live2d-py 轮子里的 Core 带着未解析的 CPython 符号，NOW 会直接失败
        void* h = dlopen(cand.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!h) continue;
        handle = h;
        path = cand;
        break;
    }
    if (!handle) {
        if (error) {
            *error = "找不到 Live2D Cubism Core 库。请下载官方 CubismSdkForNative 后把 libLive2DCubismCore.so 放到 lib/，"
                     "或设置环境变量 FXPET_CUBISM_CORE 指向它。";
        }
        return false;
    }

    const char* names[] = {"csmReviveMocInPlace",
                           "csmGetSizeofModel",
                           "csmInitializeModelInPlace",
                           "csmUpdateModel",
                           "csmReadCanvasInfo",
                           "csmResetDrawableDynamicFlags",
                           "csmGetParameterCount",
                           "csmGetParameterIds",
                           "csmGetParameterMinimumValues",
                           "csmGetParameterMaximumValues",
                           "csmGetParameterDefaultValues",
                           "csmGetParameterValues",
                           "csmGetPartCount",
                           "csmGetPartIds",
                           "csmGetPartOpacities",
                           "csmGetDrawableCount",
                           "csmGetDrawableIds",
                           "csmGetDrawableTextureIndices",
                           "csmGetDrawableConstantFlags",
                           "csmGetDrawableDynamicFlags",
                           "csmGetDrawableOpacities",
                           "csmGetDrawableMaskCounts",
                           "csmGetDrawableMasks",
                           "csmGetDrawableVertexCounts",
                           "csmGetDrawableVertexPositions",
                           "csmGetDrawableVertexUvs",
                           "csmGetDrawableIndexCounts",
                           "csmGetDrawableIndices",
                           "csmGetDrawableMultiplyColors",
                           "csmGetDrawableScreenColors",
                           "csmGetDrawableRenderOrders",
                           "csmGetDrawableParentPartIndices",
                           "csmGetPartParentPartIndices",
                           nullptr};

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
    (void)names;

    for (const Slot& s : slots) {
        void* sym = dlsym(handle, s.name);
        if (!sym) {
            if (error) *error = std::string("Core 缺少符号: ") + s.name + "（" + path + "）";
            dlclose(handle);
            handle = nullptr;
            return false;
        }
        *s.target = sym;
    }
    return true;
#endif
}

}  // namespace fx
