#include "elfio_image_parser.h"

namespace ElfHooker {

bool ElfioImageParser::LoadFromFile(const char* path) {
    if (path == nullptr) {
        loaded_ = false;
        image_path_.clear();
        return false;
    }

    return LoadFromFile(std::string(path));
}

bool ElfioImageParser::LoadFromFile(const std::string& path) {
    if (path.empty()) {
        loaded_ = false;
        image_path_.clear();
        return false;
    }

    elf_file_ = ELFIO::elfio();
    loaded_ = elf_file_.load(path);
    if (!loaded_) {
        image_path_.clear();
        return false;
    }

    image_path_ = path;
    return true;
}

bool ElfioImageParser::FindDynamicSymbol(const char* symbol_name, uint32_t* symbol_index) const {
    if (symbol_name == nullptr) {
        return false;
    }
    return FindDynamicSymbolInternal(symbol_name, symbol_index);
}

bool ElfioImageParser::FindDynamicSymbol(const std::string& symbol_name, uint32_t* symbol_index) const {
    return FindDynamicSymbolInternal(symbol_name, symbol_index);
}

bool ElfioImageParser::FindDynamicSymbolValue(const char* symbol_name, uint64_t* symbol_value) const {
    if (symbol_name == nullptr) {
        return false;
    }
    return FindDynamicSymbolValueInternal(symbol_name, symbol_value);
}

bool ElfioImageParser::FindDynamicSymbolValue(const std::string& symbol_name, uint64_t* symbol_value) const {
    return FindDynamicSymbolValueInternal(symbol_name, symbol_value);
}

bool ElfioImageParser::FindDynamicSymbolSize(const char* symbol_name, uint64_t* symbol_size) const {
    if (symbol_name == nullptr) {
        return false;
    }
    return FindDynamicSymbolSizeInternal(symbol_name, symbol_size);
}

bool ElfioImageParser::FindDynamicSymbolSize(const std::string& symbol_name, uint64_t* symbol_size) const {
    return FindDynamicSymbolSizeInternal(symbol_name, symbol_size);
}

bool ElfioImageParser::FindDynamicSymbolType(const char* symbol_name, unsigned char* symbol_type) const {
    if (symbol_name == nullptr) {
        return false;
    }
    return FindDynamicSymbolTypeInternal(symbol_name, symbol_type);
}

bool ElfioImageParser::FindDynamicSymbolType(const std::string& symbol_name,
                                             unsigned char* symbol_type) const {
    return FindDynamicSymbolTypeInternal(symbol_name, symbol_type);
}

bool ElfioImageParser::CollectDynamicSymbols(std::vector<ParsedDynamicSymbol>* symbols) const {
    if (!loaded_ || symbols == nullptr) {
        return false;
    }

    symbols->clear();

    ELFIO::section* dynsym = elf_file_.sections[".dynsym"];
    if (dynsym == nullptr) {
        return false;
    }

    ELFIO::symbol_section_accessor accessor(elf_file_, dynsym);
    std::string current_name;
    ELFIO::Elf64_Addr value = 0;
    ELFIO::Elf_Xword size = 0;
    unsigned char bind = 0;
    unsigned char type = 0;
    ELFIO::Elf_Half section_index = 0;
    unsigned char other = 0;

    for (ELFIO::Elf_Xword index = 0; index < accessor.get_symbols_num(); ++index) {
        if (!accessor.get_symbol(index,
                                 current_name,
                                 value,
                                 size,
                                 bind,
                                 type,
                                 section_index,
                                 other)) {
            continue;
        }

        ParsedDynamicSymbol symbol = {};
        symbol.index = static_cast<uint32_t>(index);
        symbol.name = current_name;
        symbol.value = static_cast<uint64_t>(value);
        symbol.size = static_cast<uint64_t>(size);
        symbol.bind = bind;
        symbol.type = type;
        symbol.section_index = static_cast<uint16_t>(section_index);
        symbol.other = other;
        symbols->push_back(std::move(symbol));
    }

    return true;
}

bool ElfioImageParser::CollectImportedSymbols(std::vector<ParsedImportedSymbol>* symbols) const {
    if (!loaded_ || symbols == nullptr) {
        return false;
    }

    symbols->clear();

    for (const auto& section : elf_file_.sections) {
        if (!section) {
            continue;
        }

        ELFIO::section* current_section = section.get();
        const ELFIO::Elf_Word section_type = current_section->get_type();
        if (section_type != ELFIO::SHT_REL && section_type != ELFIO::SHT_RELA) {
            continue;
        }

        const ELFIO::Elf_Half linked_index = current_section->get_link();
        ELFIO::section* linked_symbols =
            linked_index < elf_file_.sections.size() ? elf_file_.sections[linked_index] : nullptr;
        if (linked_symbols == nullptr) {
            continue;
        }

        ELFIO::symbol_section_accessor symbol_accessor(elf_file_, linked_symbols);
        ELFIO::relocation_section_accessor reloc_accessor(elf_file_, current_section);
        for (ELFIO::Elf_Xword index = 0; index < reloc_accessor.get_entries_num(); ++index) {
            ELFIO::Elf64_Addr offset = 0;
            ELFIO::Elf_Word relocation_symbol = 0;
            unsigned int relocation_type = 0;
            ELFIO::Elf_Sxword addend = 0;
            if (!reloc_accessor.get_entry(index,
                                          offset,
                                          relocation_symbol,
                                          relocation_type,
                                          addend)) {
                continue;
            }
            (void)addend;

            if (relocation_symbol == 0) {
                continue;
            }

            std::string current_name;
            ELFIO::Elf64_Addr value = 0;
            ELFIO::Elf_Xword size = 0;
            unsigned char bind = 0;
            unsigned char type = 0;
            ELFIO::Elf_Half section_index = 0;
            unsigned char other = 0;
            if (!symbol_accessor.get_symbol(relocation_symbol,
                                            current_name,
                                            value,
                                            size,
                                            bind,
                                            type,
                                            section_index,
                                            other)) {
                continue;
            }
            (void)value;
            (void)size;
            (void)bind;
            (void)section_index;
            (void)other;

            if (current_name.empty()) {
                continue;
            }

            ParsedImportedSymbol symbol = {};
            symbol.symbol_index = relocation_symbol;
            symbol.name = current_name;
            symbol.offset = static_cast<uint64_t>(offset);
            symbol.relocation_type = static_cast<uint32_t>(relocation_type);
            symbol.symbol_type = type;
            symbol.section_name = current_section->get_name();
            symbols->push_back(std::move(symbol));
        }
    }

    return true;
}

bool ElfioImageParser::FindDynamicSymbolInternal(const std::string& symbol_name,
                                                 uint32_t* symbol_index) const {
    if (!loaded_ || symbol_name.empty() || symbol_index == nullptr) {
        return false;
    }

    ELFIO::section* dynsym = elf_file_.sections[".dynsym"];
    if (dynsym == nullptr) {
        return false;
    }

    ELFIO::symbol_section_accessor symbols(elf_file_, dynsym);
    std::string current_name;
    ELFIO::Elf64_Addr value = 0;
    ELFIO::Elf_Xword size = 0;
    unsigned char bind = 0;
    unsigned char type = 0;
    ELFIO::Elf_Half section_index = 0;
    unsigned char other = 0;

    for (ELFIO::Elf_Xword index = 0; index < symbols.get_symbols_num(); ++index) {
        if (!symbols.get_symbol(index,
                                current_name,
                                value,
                                size,
                                bind,
                                type,
                                section_index,
                                other)) {
            continue;
        }
        if (current_name == symbol_name) {
            *symbol_index = static_cast<uint32_t>(index);
            return true;
        }
    }

    return false;
}

bool ElfioImageParser::FindDynamicSymbolTypeInternal(const std::string& symbol_name,
                                                     unsigned char* symbol_type) const {
    if (!loaded_ || symbol_name.empty() || symbol_type == nullptr) {
        return false;
    }

    ELFIO::section* dynsym = elf_file_.sections[".dynsym"];
    if (dynsym == nullptr) {
        return false;
    }

    ELFIO::symbol_section_accessor symbols(elf_file_, dynsym);
    std::string current_name;
    ELFIO::Elf64_Addr value = 0;
    ELFIO::Elf_Xword size = 0;
    unsigned char bind = 0;
    unsigned char type = 0;
    ELFIO::Elf_Half section_index = 0;
    unsigned char other = 0;

    for (ELFIO::Elf_Xword index = 0; index < symbols.get_symbols_num(); ++index) {
        if (!symbols.get_symbol(index,
                                current_name,
                                value,
                                size,
                                bind,
                                type,
                                section_index,
                                other)) {
            continue;
        }
        if (current_name == symbol_name) {
            *symbol_type = type;
            return true;
        }
    }

    return false;
}

bool ElfioImageParser::FindDynamicSymbolValueInternal(const std::string& symbol_name,
                                                      uint64_t* symbol_value) const {
    if (!loaded_ || symbol_name.empty() || symbol_value == nullptr) {
        return false;
    }

    ELFIO::section* dynsym = elf_file_.sections[".dynsym"];
    if (dynsym == nullptr) {
        return false;
    }

    ELFIO::symbol_section_accessor symbols(elf_file_, dynsym);
    std::string current_name;
    ELFIO::Elf64_Addr value = 0;
    ELFIO::Elf_Xword size = 0;
    unsigned char bind = 0;
    unsigned char type = 0;
    ELFIO::Elf_Half section_index = 0;
    unsigned char other = 0;

    for (ELFIO::Elf_Xword index = 0; index < symbols.get_symbols_num(); ++index) {
        if (!symbols.get_symbol(index,
                                current_name,
                                value,
                                size,
                                bind,
                                type,
                                section_index,
                                other)) {
            continue;
        }
        if (current_name == symbol_name) {
            *symbol_value = static_cast<uint64_t>(value);
            return true;
        }
    }

    return false;
}

bool ElfioImageParser::FindDynamicSymbolSizeInternal(const std::string& symbol_name,
                                                     uint64_t* symbol_size) const {
    if (!loaded_ || symbol_name.empty() || symbol_size == nullptr) {
        return false;
    }

    ELFIO::section* dynsym = elf_file_.sections[".dynsym"];
    if (dynsym == nullptr) {
        return false;
    }

    ELFIO::symbol_section_accessor symbols(elf_file_, dynsym);
    std::string current_name;
    ELFIO::Elf64_Addr value = 0;
    ELFIO::Elf_Xword size = 0;
    unsigned char bind = 0;
    unsigned char type = 0;
    ELFIO::Elf_Half section_index = 0;
    unsigned char other = 0;

    for (ELFIO::Elf_Xword index = 0; index < symbols.get_symbols_num(); ++index) {
        if (!symbols.get_symbol(index,
                                current_name,
                                value,
                                size,
                                bind,
                                type,
                                section_index,
                                other)) {
            continue;
        }
        if (current_name == symbol_name) {
            *symbol_size = static_cast<uint64_t>(size);
            return true;
        }
    }

    return false;
}

bool ElfioImageParser::CollectRelocationsForSymbol(
        const char* symbol_name,
        std::vector<ParsedRelocation>* relocations) const {
    if (symbol_name == nullptr) {
        return false;
    }
    return CollectRelocationsForSymbol(std::string(symbol_name), relocations);
}

bool ElfioImageParser::CollectRelocationsForSymbol(
        const std::string& symbol_name,
        std::vector<ParsedRelocation>* relocations) const {
    if (!loaded_ || symbol_name.empty() || relocations == nullptr) {
        return false;
    }

    relocations->clear();

    uint32_t symbol_index = 0;
    if (!FindDynamicSymbolInternal(symbol_name, &symbol_index)) {
        return false;
    }

    for (const auto& section : elf_file_.sections) {
        if (!section) {
            continue;
        }
        const ELFIO::section* current_section = section.get();
        const ELFIO::Elf_Word section_type = current_section->get_type();
        if (section_type != ELFIO::SHT_REL && section_type != ELFIO::SHT_RELA) {
            continue;
        }

        ELFIO::relocation_section_accessor reloc_accessor(elf_file_,
                                                          const_cast<ELFIO::section*>(current_section));
        for (ELFIO::Elf_Xword index = 0; index < reloc_accessor.get_entries_num(); ++index) {
            ELFIO::Elf64_Addr offset = 0;
            ELFIO::Elf_Word relocation_symbol = 0;
            unsigned int relocation_type = 0;
            ELFIO::Elf_Sxword addend = 0;
            if (!reloc_accessor.get_entry(index,
                                          offset,
                                          relocation_symbol,
                                          relocation_type,
                                          addend)) {
                continue;
            }
            if (relocation_symbol != symbol_index) {
                continue;
            }

            ParsedRelocation relocation = {};
            relocation.symbol_index = relocation_symbol;
            relocation.symbol_name = symbol_name;
            relocation.offset = static_cast<uint64_t>(offset);
            relocation.type = static_cast<uint32_t>(relocation_type);
            relocation.addend = static_cast<int64_t>(addend);
            relocation.section_name = current_section->get_name();
            relocations->push_back(relocation);
        }
    }

    return !relocations->empty();
}

bool ElfioImageParser::ComputeRuntimeBias(uintptr_t runtime_module_base, uintptr_t* runtime_bias) const {
    if (!loaded_ || runtime_module_base == 0u || runtime_bias == nullptr) {
        return false;
    }

    for (const auto& segment : elf_file_.segments) {
        if (!segment || segment->get_type() != ELFIO::PT_LOAD) {
            continue;
        }

        *runtime_bias = runtime_module_base +
                        static_cast<uintptr_t>(segment->get_offset()) -
                        static_cast<uintptr_t>(segment->get_virtual_address());
        return true;
    }

    return false;
}

}  // namespace ElfHooker
