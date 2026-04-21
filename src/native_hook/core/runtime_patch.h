#ifndef INJECTDEMO_RUNTIME_PATCH_H
#define INJECTDEMO_RUNTIME_PATCH_H

#include <cstddef>
#include <cstdint>

namespace ElfHooker {

struct RelocationCandidate {
    uint32_t symbol_index;
    uint32_t relocation_type;
    uintptr_t relocation_offset;
};

struct PatchPageRange {
    uintptr_t start;
    size_t length;
};

bool FindFirstMatchingRelocationOffset(const RelocationCandidate* relocations,
                                       size_t relocation_count,
                                       uint32_t target_symbol_index,
                                       uint32_t primary_type,
                                       uint32_t secondary_type,
                                       uintptr_t* matched_offset);

PatchPageRange ComputePatchPageRange(uintptr_t target_address,
                                     size_t write_size,
                                     size_t page_size);

bool CaptureAndWritePointer(void** slot, void* replacement, void** original);
bool PatchPointerAtAddress(void* slot_address, void* replacement, void** original);

}  // namespace ElfHooker

#endif // INJECTDEMO_RUNTIME_PATCH_H
