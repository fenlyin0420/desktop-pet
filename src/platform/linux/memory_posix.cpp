#include "platform/memory.hpp"

#include <cstdlib>

namespace fx {

void* alignedAlloc(size_t size, size_t alignment) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
}

void alignedFree(void* ptr) { std::free(ptr); }

}  // namespace fx
