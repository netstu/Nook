#include "../../src/native_hook/plt_hook/elfio_image_parser.h"

#include <vector>

int main() {
    ElfHooker::ElfioImageParser parser;
    if (!parser.LoadFromFile("libs/arm64-v8a/libnook_java_hook_example.so")) {
        return 1;
    }

    uint32_t symbol_index = 0;
    if (!parser.FindDynamicSymbol("malloc", &symbol_index) || symbol_index == 0u) {
        return 1;
    }

    std::vector<ElfHooker::ParsedRelocation> relocations;
    if (!parser.CollectRelocationsForSymbol("malloc", &relocations)) {
        return 1;
    }
    if (relocations.empty()) {
        return 1;
    }

    bool found_non_zero_offset = false;
    bool found_plt_entry = false;
    for (const auto& relocation : relocations) {
        if (relocation.symbol_name != "malloc") {
            return 1;
        }
        if (relocation.offset != 0u) {
            found_non_zero_offset = true;
        }
        if (relocation.section_name.find(".plt") != std::string::npos) {
            found_plt_entry = true;
        }
    }

    if (!found_non_zero_offset || !found_plt_entry) {
        return 1;
    }

    return 0;
}
