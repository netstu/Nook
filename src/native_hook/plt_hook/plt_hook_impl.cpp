#include "plt_hook_impl.h"

#include "elfio_image_parser.h"
#include "elf_reader.h"
#include "native_hook/core/runtime_patch.h"

#include <vector>

namespace NookNativeInternal {

bool TryPltHookWithElfio(const ResolvedHookTarget& target, void*) {
    ElfHooker::ElfioImageParser parser;
    if (!parser.LoadFromFile(target.module_path)) {
        return false;
    }

    uintptr_t runtime_bias = 0;
    if (!parser.ComputeRuntimeBias(reinterpret_cast<uintptr_t>(target.module_base), &runtime_bias)) {
        return false;
    }

    std::vector<ElfHooker::ParsedRelocation> relocations;
    if (!parser.CollectRelocationsForSymbol(target.symbol_name, &relocations)) {
        return false;
    }

    for (const auto& relocation : relocations) {
        void* slot_address = reinterpret_cast<void*>(runtime_bias + relocation.offset);
        if (ElfHooker::PatchPointerAtAddress(slot_address, target.replacement, target.original)) {
            return true;
        }
    }

    return false;
}

bool TryPltHookWithElfReader(const ResolvedHookTarget& target, void*) {
    ElfReader reader(target.module_name, target.module_base);
    if (reader.parse() != 0) {
        return false;
    }
    return reader.hook(target.symbol_name, target.replacement, target.original) == 0;
}

}  // namespace NookNativeInternal
