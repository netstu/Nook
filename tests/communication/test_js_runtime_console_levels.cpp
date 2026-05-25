#include <cassert>
#include <string>
#include <vector>

#include "agent_runtime/js_runtime.h"

using namespace nook::agent_runtime;

int main() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::vector<std::string> received_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        assert(data.empty());
        received_messages.push_back(json);
        return true;
    });

    assert(JsRuntime::Evaluate("console.info('hello-info')", "console_info.js", &error_message));
    assert(JsRuntime::Evaluate("console.warn('hello-warn')", "console_warn.js", &error_message));
    assert(JsRuntime::Evaluate("console.error('hello-error')", "console_error.js", &error_message));

    assert(received_messages.size() == 3u);
    assert(received_messages[0] ==
           "{\"type\":\"log\",\"level\":\"info\",\"payload\":\"hello-info\"}");
    assert(received_messages[1] ==
           "{\"type\":\"log\",\"level\":\"warn\",\"payload\":\"hello-warn\"}");
    assert(received_messages[2] ==
           "{\"type\":\"log\",\"level\":\"error\",\"payload\":\"hello-error\"}");

    JsRuntime::Shutdown();
    return 0;
}
