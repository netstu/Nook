#include "runtime_patch.h"

#if defined(__linux__) || defined(__ANDROID__)
#include "module_info.h"
#include <sys/mman.h>
#endif

namespace ElfHooker {

bool FindFirstMatchingRelocationOffset(const RelocationCandidate* relocations,
                                       size_t relocation_count,
                                       uint32_t target_symbol_index,
                                       uint32_t primary_type,
                                       uint32_t secondary_type,
                                       uintptr_t* matched_offset) {
    if (relocations == nullptr || matched_offset == nullptr) {
        return false;
    }

    for (size_t index = 0; index < relocation_count; ++index) {
        const RelocationCandidate& relocation = relocations[index];
        if (relocation.symbol_index != target_symbol_index) {
            continue;
        }
        if (relocation.relocation_type != primary_type &&
            relocation.relocation_type != secondary_type) {
            continue;
        }

        *matched_offset = relocation.relocation_offset;
        return true;
    }

    return false;
}

PatchPageRange ComputePatchPageRange(uintptr_t target_address,
                                     size_t write_size,
                                     size_t page_size) {
    PatchPageRange range = {0u, 0u};
    if (target_address == 0u || write_size == 0u || page_size == 0u) {
        return range;
    }

    const uintptr_t page_mask = ~(static_cast<uintptr_t>(page_size) - 1u);
    const uintptr_t start = target_address & page_mask;
    const uintptr_t end = (target_address + write_size - 1u) & page_mask;

    range.start = start;
    range.length = (end - start) + page_size;
    return range;
}

bool CaptureAndWritePointer(void** slot, void* replacement, void** original) {
    if (slot == nullptr || original == nullptr || replacement == nullptr) {
        return false;
    }

    *original = *slot;
    *slot = replacement;
    return true;
}

bool PatchPointerAtAddress(void* slot_address, void* replacement, void** original) {
#if defined(__linux__) || defined(__ANDROID__)
    if (slot_address == nullptr || replacement == nullptr || original == nullptr) {
        return false;
    }

    int original_protection = 0;
    if (!get_address_protection(slot_address, &original_protection)) {
        return false;
    }

    int writable_protection = original_protection & ~PROT_EXEC;
    writable_protection |= PROT_WRITE;

    const PatchPageRange patch_range =
            ComputePatchPageRange(reinterpret_cast<uintptr_t>(slot_address),
                                  sizeof(void*),
                                  PAGE_SIZE);
    if (patch_range.start == 0u || patch_range.length == 0u) {
        return false;
    }

    void* page_start = reinterpret_cast<void*>(patch_range.start);
    if (mprotect(page_start, patch_range.length, writable_protection) != 0) {
        return false;
    }

    const bool wrote_pointer =
            CaptureAndWritePointer(reinterpret_cast<void**>(slot_address), replacement, original);
    clear_cache(page_start, patch_range.length);
    const int restore_result = mprotect(page_start, patch_range.length, original_protection);
    return wrote_pointer && restore_result == 0;
#else
    (void)slot_address;
    (void)replacement;
    (void)original;
    return false;
#endif
}

}  // namespace ElfHooker
