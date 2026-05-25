#ifndef NINJECTOR_SYMBI_INJECTOR_H
#define NINJECTOR_SYMBI_INJECTOR_H

#include <sys/types.h>
#include <string>
#include <vector>

struct SpawnSymbiResult {
    pid_t child_pid = -1;
    std::string package_name;
};

bool inject_spawn_symbi_by_package(pid_t zygote_pid,
                                   const char* package_name,
                                   SpawnSymbiResult* result);
bool inject_spawn_symbi_by_pids(const std::vector<pid_t>& pids,
                                const char* package_name,
                                SpawnSymbiResult* result);
const char* get_last_spawn_symbi_error();

#endif // NINJECTOR_SYMBI_INJECTOR_H
