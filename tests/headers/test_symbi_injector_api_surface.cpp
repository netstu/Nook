#include "server/symbi/symbi_injector.h"

#include <vector>

int main() {
    SpawnSymbiResult result{};
    std::vector<pid_t> pids{123};

    // The symbi gate API should only describe zygote gate + child callback handoff.
    // Child runtime delivery belongs to the caller after the child has stopped.
    (void) inject_spawn_symbi_by_package(123, "com.demo.target", &result);
    (void) inject_spawn_symbi_by_pids(pids, "com.demo.target", &result);
    return 0;
}
