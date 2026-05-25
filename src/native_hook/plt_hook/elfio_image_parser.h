#ifndef INJECTDEMO_ELFIO_IMAGE_PARSER_H
#define INJECTDEMO_ELFIO_IMAGE_PARSER_H

#include <elfio/elfio.hpp>
#include <elfio/elfio_relocation.hpp>
#include <elfio/elfio_symbols.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ElfHooker {

struct ParsedRelocation {
    uint32_t symbol_index = 0;
    std::string symbol_name;
    uint64_t offset = 0;
    uint32_t type = 0;
    int64_t addend = 0;
    std::string section_name;
};

struct ParsedDynamicSymbol {
    uint32_t index = 0;
    std::string name;
    uint64_t value = 0;
    uint64_t size = 0;
    unsigned char bind = 0;
    unsigned char type = 0;
    uint16_t section_index = 0;
    unsigned char other = 0;
};

struct ParsedImportedSymbol {
    uint32_t symbol_index = 0;
    std::string name;
    uint64_t offset = 0;
    uint32_t relocation_type = 0;
    unsigned char symbol_type = 0;
    std::string section_name;
};

class ElfioImageParser {
public:
    bool LoadFromFile(const char* path);
    bool LoadFromFile(const std::string& path);

    bool FindDynamicSymbol(const char* symbol_name, uint32_t* symbol_index) const;
    bool FindDynamicSymbol(const std::string& symbol_name, uint32_t* symbol_index) const;
    bool FindDynamicSymbolValue(const char* symbol_name, uint64_t* symbol_value) const;
    bool FindDynamicSymbolValue(const std::string& symbol_name, uint64_t* symbol_value) const;
    bool FindDynamicSymbolSize(const char* symbol_name, uint64_t* symbol_size) const;
    bool FindDynamicSymbolSize(const std::string& symbol_name, uint64_t* symbol_size) const;
    bool FindDynamicSymbolType(const char* symbol_name, unsigned char* symbol_type) const;
    bool FindDynamicSymbolType(const std::string& symbol_name, unsigned char* symbol_type) const;
    bool CollectDynamicSymbols(std::vector<ParsedDynamicSymbol>* symbols) const;
    bool CollectImportedSymbols(std::vector<ParsedImportedSymbol>* symbols) const;

    bool CollectRelocationsForSymbol(const char* symbol_name,
                                     std::vector<ParsedRelocation>* relocations) const;
    bool CollectRelocationsForSymbol(const std::string& symbol_name,
                                     std::vector<ParsedRelocation>* relocations) const;
    bool ComputeRuntimeBias(uintptr_t runtime_module_base, uintptr_t* runtime_bias) const;

private:
    bool FindDynamicSymbolInternal(const std::string& symbol_name, uint32_t* symbol_index) const;
    bool FindDynamicSymbolValueInternal(const std::string& symbol_name, uint64_t* symbol_value) const;
    bool FindDynamicSymbolSizeInternal(const std::string& symbol_name, uint64_t* symbol_size) const;
    bool FindDynamicSymbolTypeInternal(const std::string& symbol_name, unsigned char* symbol_type) const;

private:
    ELFIO::elfio elf_file_;
    std::string image_path_;
    bool loaded_ = false;
};

}  // namespace ElfHooker

#endif // INJECTDEMO_ELFIO_IMAGE_PARSER_H
