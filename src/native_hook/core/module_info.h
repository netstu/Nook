#ifndef NOOK_NATIVE_HOOK_MODULE_INFO_H
#define NOOK_NATIVE_HOOK_MODULE_INFO_H

#include <string>
#include <unistd.h>

namespace ElfHooker {

bool get_module_info(pid_t pid, const char* module, void** module_base, std::string* module_path);
void* get_module_base(pid_t pid, const char* module);
bool get_address_protection(void* address, int* protection);
void clear_cache(void* addr, size_t len);

}  // namespace ElfHooker

#endif  // NOOK_NATIVE_HOOK_MODULE_INFO_H
