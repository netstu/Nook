#pragma once

#include <cstddef>

namespace NookInlineHookInternal {

struct TrampolineAllocation {
    void* address = nullptr;
    size_t size = 0u;
};

bool AllocateExecutableTrampoline(size_t size, TrampolineAllocation* allocation);
void FreeExecutableTrampoline(TrampolineAllocation* allocation);

}  // namespace NookInlineHookInternal
