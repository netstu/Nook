#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NookInlineHookInternal {

struct InlineHookRecord {
    void* target_address = nullptr;
    void* replacement_address = nullptr;
    void* trampoline_address = nullptr;
    size_t patched_length = 0u;
    bool active = false;
    std::vector<uint8_t> original_code;
};

void ResetInlineHookRecord(InlineHookRecord* record);

void ActivateInlineHookRecord(InlineHookRecord* record,
                              void* target_address,
                              void* replacement_address,
                              void* trampoline_address,
                              size_t patched_length,
                              const uint8_t* original_code,
                              size_t original_code_size);

}  // namespace NookInlineHookInternal
