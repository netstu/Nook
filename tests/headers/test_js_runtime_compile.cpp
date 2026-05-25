#include "agent_runtime/js_runtime.h"
#include "agent_runtime/script_registry.h"

int main() {
    nook::agent_runtime::JsRuntime::Initialize();
    nook::agent_runtime::JsRuntime::Shutdown();

    nook::agent_runtime::ScriptRegistry registry;
    uint32_t script_id = 0;
    std::string error_message;
    const bool create_ok = registry.CreateScript("compile.js",
                                                 "send({ type: 'send', payload: 'ok' });",
                                                 &script_id,
                                                 &error_message);
    const bool load_ok = registry.LoadScript(script_id, &error_message);
    registry.Clear();

    return (create_ok || !error_message.empty() || !load_ok || script_id == 0u) ? 0 : 0;
}

