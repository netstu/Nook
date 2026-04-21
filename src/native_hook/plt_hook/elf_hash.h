#ifndef NOOK_NATIVE_HOOK_ELF_HASH_H
#define NOOK_NATIVE_HOOK_ELF_HASH_H

#include <cstdint>

namespace ElfHooker {

uint32_t elf_hash(const char* symbol);
uint32_t gnu_hash(const char* symbol);

}  // namespace ElfHooker

#endif  // NOOK_NATIVE_HOOK_ELF_HASH_H
