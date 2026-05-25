#include <cassert>
#include <string>

#include "agent_runtime/js_runtime.h"

using namespace nook::agent_runtime;

int main() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    assert(JsRuntime::ValidateScript("send({\"type\":\"send\",\"payload\":\"hello-runtime\"});",
                                     "quoted_keys.js",
                                     &error_message));

    assert(JsRuntime::ValidateScript("send({ type: 'send', payload: 'script-loaded' })",
                                     "unquoted_keys.js",
                                     &error_message));

    JsRuntime::Shutdown();
    return 0;
}
