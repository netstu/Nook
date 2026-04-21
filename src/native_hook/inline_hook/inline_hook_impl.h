#pragma once

#include <cstddef>
#include <cstdint>

#include "native_hook/inline_hook/inline_hook_record.h"
#include "native_hook/inline_hook/trampoline_allocator.h"

namespace NookInlineHookInternal {

struct InlineHookHandle {
    InlineHookRecord record;
    TrampolineAllocation trampoline;
};

size_t GetArm64InlineHookPatchSize(void);

bool InstallInlineHook(void* target_address,
                       void* replacement,
                       void** original,
                       void** hook_handle);

bool UninstallInlineHook(void* hook_handle);

}  // namespace NookInlineHookInternal
