// Live2D Cubism Core（官方专有二进制）的运行时加载与 C API 声明。
// 不随本仓库分发 Core：优先用 $FXPET_CUBISM_CORE，其次 ./lib/，最后系统库。
#pragma once

#include <cstdint>
#include <string>

namespace fx {

struct CsmVector2 {
    float x;
    float y;
};
struct CsmVector4 {
    float x;
    float y;
    float z;
    float w;
};

struct CsmMoc;
struct CsmModel;

// drawable 的 constant flags
enum : int {
    kBlendAdditive = 1 << 0,
    kBlendMultiplicative = 1 << 1,
    kIsDoubleSided = 1 << 2,
    kIsInvertedMask = 1 << 3,
};
// drawable 的 dynamic flags
enum : int {
    kIsVisible = 1 << 0,
    kVisibilityDidChange = 1 << 1,
    kOpacityDidChange = 1 << 2,
    kDrawOrderDidChange = 1 << 3,
    kRenderOrderDidChange = 1 << 4,
    kVertexPositionsDidChange = 1 << 5,
    kBlendColorDidChange = 1 << 6,
};

struct CubismCore {
    void* handle = nullptr;
    std::string path;

    bool load(std::string* error);
    bool ok() const { return handle != nullptr; }

    // ---- moc / model
    CsmMoc* (*ReviveMocInPlace)(void*, unsigned int) = nullptr;
    unsigned int (*GetSizeofModel)(const CsmMoc*) = nullptr;
    CsmModel* (*InitializeModelInPlace)(const CsmMoc*, void*, unsigned int) = nullptr;
    void (*UpdateModel)(CsmModel*) = nullptr;
    void (*ReadCanvasInfo)(const CsmModel*, CsmVector2*, CsmVector2*, float*) = nullptr;
    void (*ResetDrawableDynamicFlags)(CsmModel*) = nullptr;
    // ---- parameter
    int (*GetParameterCount)(const CsmModel*) = nullptr;
    const char** (*GetParameterIds)(const CsmModel*) = nullptr;
    const float* (*GetParameterMinimumValues)(const CsmModel*) = nullptr;
    const float* (*GetParameterMaximumValues)(const CsmModel*) = nullptr;
    const float* (*GetParameterDefaultValues)(const CsmModel*) = nullptr;
    float* (*GetParameterValues)(CsmModel*) = nullptr;
    // ---- part
    int (*GetPartCount)(const CsmModel*) = nullptr;
    const char** (*GetPartIds)(const CsmModel*) = nullptr;
    float* (*GetPartOpacities)(CsmModel*) = nullptr;
    // ---- drawable
    int (*GetDrawableCount)(const CsmModel*) = nullptr;
    const char** (*GetDrawableIds)(const CsmModel*) = nullptr;
    const int* (*GetDrawableTextureIndices)(const CsmModel*) = nullptr;
    // Core 的 csmFlags 是 uint8_t；声明成 int* 会错位并越界读取。
    const uint8_t* (*GetDrawableConstantFlags)(const CsmModel*) = nullptr;
    const uint8_t* (*GetDrawableDynamicFlags)(const CsmModel*) = nullptr;
    const float* (*GetDrawableOpacities)(const CsmModel*) = nullptr;
    const int* (*GetDrawableMaskCounts)(const CsmModel*) = nullptr;
    const int** (*GetDrawableMasks)(const CsmModel*) = nullptr;
    const int* (*GetDrawableVertexCounts)(const CsmModel*) = nullptr;
    const CsmVector2** (*GetDrawableVertexPositions)(const CsmModel*) = nullptr;
    const CsmVector2** (*GetDrawableVertexUvs)(const CsmModel*) = nullptr;
    const int* (*GetDrawableIndexCounts)(const CsmModel*) = nullptr;
    const unsigned short** (*GetDrawableIndices)(const CsmModel*) = nullptr;
    const CsmVector4* (*GetDrawableMultiplyColors)(const CsmModel*) = nullptr;
    const CsmVector4* (*GetDrawableScreenColors)(const CsmModel*) = nullptr;
    const int* (*GetDrawableRenderOrders)(const CsmModel*) = nullptr;
    const int* (*GetDrawableParentPartIndices)(const CsmModel*) = nullptr;
    const int* (*GetPartParentPartIndices)(const CsmModel*) = nullptr;
};

}  // namespace fx
