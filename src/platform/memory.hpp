// 64 字节对齐分配（Live2D Core 要求）。
#pragma once

#include <cstddef>

namespace fx {

void* alignedAlloc(size_t size, size_t alignment);
void alignedFree(void* ptr);

}  // namespace fx
