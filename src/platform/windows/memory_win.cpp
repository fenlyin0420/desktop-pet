#include "platform/memory.hpp"

#include <malloc.h>

namespace fx {

void* alignedAlloc(size_t size, size_t alignment) { return _aligned_malloc(size, alignment); }
void alignedFree(void* ptr) { _aligned_free(ptr); }

}  // namespace fx
