#include <cassert>
#include <string>

#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

int main() {
    ScriptRegistry registry;
    uint32_t script_id = 0;
    std::string error_message;

    assert(registry.CreateScript("unload.js",
                                 "send({ type: 'send', payload: 'before-unload' })",
                                 &script_id,
                                 &error_message));
    assert(script_id != 0u);
    assert(registry.UnloadScript(script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message == "script not found");

    return 0;
}
