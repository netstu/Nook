#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

namespace {

void TestDispatchMessageCallsRecvHandler() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "recv(function(message) {"
        "  send({ type: 'send', payload: message.payload });"
        "});";
    assert(registry.CreateScript("recv_echo.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    assert(JsRuntime::DispatchMessage(script_id,
                                      "{\"type\":\"post\",\"payload\":\"hello-from-host\"}",
                                      {},
                                      &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-from-host\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

}  // namespace

int main() {
    TestDispatchMessageCallsRecvHandler();
    return 0;
}
