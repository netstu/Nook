#include <cstdlib>
#include <iostream>
#include <string>

#include "agent_runtime/script_registry.h"

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    nook::agent_runtime::ScriptRegistry startup_registry;
    nook::agent_runtime::ScriptRegistry attach_registry;

    std::string error_message;
    uint32_t startup_script_id = 0u;
    uint32_t attach_script_id = 0u;

    Require(startup_registry.CreateScript("startup.js",
                                          "send('startup');",
                                          &startup_script_id,
                                          &error_message),
            "startup registry must create a script");
    Require(attach_registry.CreateScript("attach.js",
                                         "send('attach');",
                                         &attach_script_id,
                                         &error_message),
            "attach registry must create a script");
    Require(startup_script_id != 0u,
            "startup registry must allocate a non-zero script id");
    Require(attach_script_id != 0u,
            "attach registry must allocate a non-zero script id");
    Require(startup_script_id != attach_script_id,
            "separate registries sharing one runtime must not reuse script ids");
    return 0;
}
