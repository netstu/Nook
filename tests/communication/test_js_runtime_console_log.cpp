#include <cassert>
#include <string>
#include <vector>

#include "agent_runtime/js_runtime.h"

using namespace nook::agent_runtime;

int main() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    const bool ok = JsRuntime::Evaluate("console.log('hello-console')",
                                        "console_log.js",
                                        &error_message);
    if (!ok) {
        JsRuntime::Shutdown();
        return 1;
    }

    assert(received_json == "{\"type\":\"log\",\"level\":\"info\",\"payload\":\"hello-console\"}");
    assert(received_data.empty());

    JsRuntime::Shutdown();
    return 0;
}
