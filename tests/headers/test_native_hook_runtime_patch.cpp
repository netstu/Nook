#include "../../src/native_hook/core/runtime_patch.h"

#include <cstdint>

int main() {
    using ElfHooker::PatchPageRange;
    using ElfHooker::RelocationCandidate;

    const RelocationCandidate relocations[] = {
        {1u, 100u, 0x1000u},
        {7u, 200u, 0x2000u},
        {7u, 201u, 0x3000u},
    };

    uintptr_t matched_offset = 0;
    if (!ElfHooker::FindFirstMatchingRelocationOffset(
            relocations, 3u, 7u, 200u, 201u, &matched_offset)) {
        return 1;
    }
    if (matched_offset != 0x2000u) {
        return 1;
    }

    matched_offset = 0;
    if (ElfHooker::FindFirstMatchingRelocationOffset(
            relocations, 3u, 9u, 200u, 201u, &matched_offset)) {
        return 1;
    }

    const PatchPageRange single_page =
            ElfHooker::ComputePatchPageRange(0x1234u, sizeof(void*), 0x1000u);
    if (single_page.start != 0x1000u || single_page.length != 0x1000u) {
        return 1;
    }

    const PatchPageRange cross_page =
            ElfHooker::ComputePatchPageRange(0x1ffcu, sizeof(void*), 0x1000u);
    if (cross_page.start != 0x1000u || cross_page.length != 0x2000u) {
        return 1;
    }

    void* slot = reinterpret_cast<void*>(0x1111u);
    void* original = nullptr;
    if (!ElfHooker::CaptureAndWritePointer(&slot,
                                           reinterpret_cast<void*>(0x2222u),
                                           &original)) {
        return 1;
    }
    if (slot != reinterpret_cast<void*>(0x2222u) ||
        original != reinterpret_cast<void*>(0x1111u)) {
        return 1;
    }

    return 0;
}
