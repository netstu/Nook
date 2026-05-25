#include <cassert>
#include <iostream>
#include <string>

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

int main() {
    JsRuntime::Shutdown();

    ScriptRegistry registry;
    uint32_t script_id = 0;
    std::string error_message;
    const bool ok = registry.CreateScript("lazy.js",
                                          "send({ type: 'send', payload: 'lazy-init' })",
                                          &script_id,
                                          &error_message);
    if (!ok) {
        std::cerr << "CreateScript error: " << error_message << "\n";
    }
    assert(ok);
    assert(script_id != 0u);

    registry.Clear();
    JsRuntime::Shutdown();
    return 0;
}
