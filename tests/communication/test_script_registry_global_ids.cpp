#include <cassert>
#include <iostream>
#include <string>

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

int main() {
    JsRuntime::Shutdown();

    ScriptRegistry startup_registry;
    ScriptRegistry attach_registry;
    std::string error_message;

    uint32_t startup_script_id = 0u;
    bool ok = startup_registry.CreateScript("startup.js",
                                            "send({ type: 'send', payload: 'startup' });",
                                            &startup_script_id,
                                            &error_message);
    if (!ok) {
        std::cerr << "startup CreateScript error: " << error_message << "\n";
    }
    assert(ok);
    assert(startup_script_id != 0u);

    uint32_t attach_script_id = 0u;
    ok = attach_registry.CreateScript("attach.js",
                                      "send({ type: 'send', payload: 'attach' });",
                                      &attach_script_id,
                                      &error_message);
    if (!ok) {
        std::cerr << "attach CreateScript error: " << error_message << "\n";
    }
    assert(ok);
    assert(attach_script_id != 0u);
    assert(startup_script_id != attach_script_id);

    startup_registry.Clear();
    attach_registry.Clear();
    JsRuntime::Shutdown();
    return 0;
}
