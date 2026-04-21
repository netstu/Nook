#include "native_hook/inline_hook/inline_hook_record.h"

namespace NookInlineHookInternal {

void ResetInlineHookRecord(InlineHookRecord* record) {
    if (record == nullptr) {
        return;
    }

    record->target_address = nullptr;
    record->replacement_address = nullptr;
    record->trampoline_address = nullptr;
    record->patched_length = 0u;
    record->active = false;
    record->original_code.clear();
}

void ActivateInlineHookRecord(InlineHookRecord* record,
                              void* target_address,
                              void* replacement_address,
                              void* trampoline_address,
                              size_t patched_length,
                              const uint8_t* original_code,
                              size_t original_code_size) {
    if (record == nullptr) {
        return;
    }

    record->target_address = target_address;
    record->replacement_address = replacement_address;
    record->trampoline_address = trampoline_address;
    record->patched_length = patched_length;
    record->active = true;
    record->original_code.assign(original_code, original_code + original_code_size);
}

}  // namespace NookInlineHookInternal
