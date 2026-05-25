#include "../../src/native_hook/inline_hook/inline_hook_record.h"
#include "../../src/native_hook/inline_hook/trampoline_allocator.h"

#include <cstdint>

int main() {
    using NookInlineHookInternal::ActivateInlineHookRecord;
    using NookInlineHookInternal::AllocateExecutableTrampoline;
    using NookInlineHookInternal::FreeExecutableTrampoline;
    using NookInlineHookInternal::InlineHookRecord;
    using NookInlineHookInternal::ResetInlineHookRecord;
    using NookInlineHookInternal::TrampolineAllocation;

    InlineHookRecord record;
    if (record.target_address != nullptr ||
        record.replacement_address != nullptr ||
        record.trampoline_address != nullptr ||
        record.patched_length != 0u ||
        record.active ||
        !record.original_code.empty()) {
        return 1;
    }

    const uint8_t original_code[] = {0xaa, 0xbb, 0xcc, 0xdd};
    ActivateInlineHookRecord(&record,
                             reinterpret_cast<void*>(0x1000u),
                             reinterpret_cast<void*>(0x2000u),
                             reinterpret_cast<void*>(0x3000u),
                             16u,
                             original_code,
                             sizeof(original_code));

    if (record.target_address != reinterpret_cast<void*>(0x1000u) ||
        record.replacement_address != reinterpret_cast<void*>(0x2000u) ||
        record.trampoline_address != reinterpret_cast<void*>(0x3000u) ||
        record.patched_length != 16u ||
        !record.active ||
        record.original_code.size() != sizeof(original_code) ||
        record.original_code[0] != 0xaau ||
        record.original_code[3] != 0xddu) {
        return 1;
    }

    ResetInlineHookRecord(&record);
    if (record.target_address != nullptr ||
        record.replacement_address != nullptr ||
        record.trampoline_address != nullptr ||
        record.patched_length != 0u ||
        record.active ||
        !record.original_code.empty()) {
        return 1;
    }

    TrampolineAllocation allocation = {};
    if (!AllocateExecutableTrampoline(128u, &allocation)) {
        return 1;
    }
    if (allocation.address == nullptr || allocation.size < 128u) {
        return 1;
    }
    FreeExecutableTrampoline(&allocation);
    if (allocation.address != nullptr || allocation.size != 0u) {
        return 1;
    }

    return 0;
}
