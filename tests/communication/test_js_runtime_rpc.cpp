#include <cassert>
#include <cstdint>
#include <string>

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

namespace {

void TestCallRpcInvokesRpcExports() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "rpc.exports = {"
        "  ping: function(name, value) {"
        "    return { reply: 'hello ' + name, number: value + 1 };"
        "  }"
        "};";
    assert(registry.CreateScript("rpc.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id,
                              "ping",
                              "[\"world\",41]",
                              &result_json,
                              &error_message));
    assert(result_json == "{\"reply\":\"hello world\",\"number\":42}");

    registry.Clear();
    JsRuntime::Shutdown();
}

}  // namespace

int main() {
    TestCallRpcInvokesRpcExports();
    return 0;
}
