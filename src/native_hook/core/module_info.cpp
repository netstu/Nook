#include "module_info.h"

#include "module_match.h"

#include <cstdio>
#include <sys/mman.h>

namespace {

constexpr size_t kSoNameLen = 128;

int maps_perms_to_prot(const char* perms) {
    if (perms == nullptr) {
        return 0;
    }

    int prot = 0;
    if (perms[0] == 'r') {
        prot |= PROT_READ;
    }
    if (perms[1] == 'w') {
        prot |= PROT_WRITE;
    }
    if (perms[2] == 'x') {
        prot |= PROT_EXEC;
    }
    return prot;
}

}  // namespace

bool ElfHooker::get_module_info(pid_t pid,
                                const char* module,
                                void** module_base,
                                std::string* module_path) {
    if (module == nullptr || module[0] == '\0') {
        return false;
    }

    if (module_base != nullptr) {
        *module_base = nullptr;
    }
    if (module_path != nullptr) {
        module_path->clear();
    }

    char buffer[1024];
    if (pid <= 0) {
        std::snprintf(buffer, sizeof(buffer), "/proc/self/maps");
    } else {
        std::snprintf(buffer, sizeof(buffer), "/proc/%d/maps", pid);
    }

    unsigned long map_start = 0;
    unsigned long map_end = 0;
    char perms[5] = {0};
    char so_name[kSoNameLen] = {0};
    FILE* maps_file = std::fopen(buffer, "r");
    if (maps_file == nullptr) {
        return false;
    }

    while (std::fgets(buffer, sizeof(buffer), maps_file)) {
        if (std::sscanf(buffer,
                        "%lx-%lx %4s %*x %*x:%*x %*d %127s",
                        &map_start,
                        &map_end,
                        perms,
                        so_name) != 4) {
            continue;
        }

        if (!module_path_matches(so_name, module)) {
            continue;
        }

        std::fclose(maps_file);
        if (module_base != nullptr) {
            *module_base = reinterpret_cast<void*>(map_start);
        }
        if (module_path != nullptr) {
            *module_path = so_name;
        }
        return true;
    }

    std::fclose(maps_file);
    return false;
}

void* ElfHooker::get_module_base(pid_t pid, const char* module) {
    void* module_base = nullptr;
    if (get_module_info(pid, module, &module_base, nullptr)) {
        return module_base;
    }

    return nullptr;
}

bool ElfHooker::get_address_protection(void* address, int* protection) {
    if (address == nullptr || protection == nullptr) {
        return false;
    }

    *protection = 0;

    FILE* maps_file = std::fopen("/proc/self/maps", "r");
    if (maps_file == nullptr) {
        return false;
    }

    char buffer[1024];
    unsigned long map_start = 0;
    unsigned long map_end = 0;
    char perms[5] = {0};
    const uintptr_t target = reinterpret_cast<uintptr_t>(address);

    while (std::fgets(buffer, sizeof(buffer), maps_file)) {
        if (std::sscanf(buffer, "%lx-%lx %4s", &map_start, &map_end, perms) != 3) {
            continue;
        }

        if (target < map_start || target >= map_end) {
            continue;
        }

        std::fclose(maps_file);
        *protection = maps_perms_to_prot(perms);
        return true;
    }

    std::fclose(maps_file);
    return false;
}

void ElfHooker::clear_cache(void* addr, size_t len) {
    (void)len;
#if defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache(reinterpret_cast<char*>(addr),
                            reinterpret_cast<char*>(addr) + len);
#else
    (void)addr;
#endif
}
