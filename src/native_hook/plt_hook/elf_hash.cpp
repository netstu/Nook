#include "elf_hash.h"

namespace ElfHooker {

uint32_t elf_hash(const char* symbol) {
    const uint8_t* name = reinterpret_cast<const uint8_t*>(symbol);
    uint32_t h = 0;
    uint32_t g = 0;

    while (*name) {
        h = (h << 4) + *name++;
        g = h & 0xf0000000;
        h ^= g;
        h ^= g >> 24;
    }

    return h;
}

uint32_t gnu_hash(const char* symbol) {
    uint32_t h = 5381;
    const uint8_t* name = reinterpret_cast<const uint8_t*>(symbol);
    while (*name != 0) {
        h += (h << 5) + *name++;
    }
    return h;
}

}  // namespace ElfHooker
