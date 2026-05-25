#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

namespace {

void TestDispatchJavaReadyCallbacksDrainsQueuedJavaReadyCallbackAfterReadinessFlip() {
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
        "globalThis.__nookReadyFlag = false;"
        "var events = [];"
        "Java._isClassLoaderReady = function () {"
        "  return globalThis.__nookReadyFlag;"
        "};"
        "Java._isLifecycleReady = function () {"
        "  return false;"
        "};"
        "Java.ready(function () {"
        "  events.push('ready');"
        "  send({ type: 'send', payload: events.join('|') });"
        "});"
        "events.push('after-load');";
    assert(registry.CreateScript("dispatch_java_ready_callbacks_drains_queued_java_ready_callback_after_readiness_flip.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.empty());
    assert(JsRuntime::Evaluate("globalThis.__nookReadyFlag = true;",
                               "dispatch_java_ready_callbacks_ready_flip.js",
                               0,
                               &error_message));
    assert(JsRuntime::DispatchJavaReadyCallbacks(&error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"after-load|ready\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestUnloadScriptDropsQueuedJavaReadyCallback() {
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
        "globalThis.__nookReadyFlag = false;"
        "Java._isClassLoaderReady = function () {"
        "  return globalThis.__nookReadyFlag;"
        "};"
        "Java._isLifecycleReady = function () {"
        "  return false;"
        "};"
        "Java.ready(function () {"
        "  send({ type: 'send', payload: 'stale-ready-callback-ran' });"
        "});";
    assert(registry.CreateScript("unload_script_drops_queued_java_ready_callback.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.empty());

    assert(registry.UnloadScript(script_id, &error_message));
    assert(JsRuntime::Evaluate("globalThis.__nookReadyFlag = true;",
                               "unload_script_drops_queued_java_ready_callback_ready_flip.js",
                               0,
                               &error_message));
    assert(JsRuntime::DispatchJavaReadyCallbacks(&error_message));
    assert(received_json.empty());
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestUnloadScriptKeepsOtherQueuedJavaReadyCallbacks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::vector<std::string> received_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        received_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t first_script_id = 0;
    const char* first_source =
        "globalThis.__nookReadyFlag = false;"
        "Java._isClassLoaderReady = function () {"
        "  return globalThis.__nookReadyFlag;"
        "};"
        "Java._isLifecycleReady = function () {"
        "  return false;"
        "};"
        "Java.ready(function () {"
        "  send({ type: 'send', payload: 'first-ready' });"
        "});";
    assert(registry.CreateScript("unload_script_keeps_other_ready_first.js",
                                 first_source,
                                 &first_script_id,
                                 &error_message));
    assert(registry.LoadScript(first_script_id, &error_message));

    uint32_t second_script_id = 0;
    const char* second_source =
        "Java.ready(function () {"
        "  send({ type: 'send', payload: 'second-ready' });"
        "});";
    assert(registry.CreateScript("unload_script_keeps_other_ready_second.js",
                                 second_source,
                                 &second_script_id,
                                 &error_message));
    assert(registry.LoadScript(second_script_id, &error_message));
    assert(received_messages.empty());

    assert(registry.UnloadScript(first_script_id, &error_message));
    assert(JsRuntime::Evaluate("globalThis.__nookReadyFlag = true;",
                               "unload_script_keeps_other_ready_flip.js",
                               0,
                               &error_message));
    assert(JsRuntime::DispatchJavaReadyCallbacks(&error_message));
    assert(received_messages.size() == 1u);
    assert(received_messages[0] == "{\"type\":\"send\",\"payload\":\"second-ready\"}");

    registry.Clear();
    JsRuntime::Shutdown();
}

}  // namespace

int main() {
    TestDispatchJavaReadyCallbacksDrainsQueuedJavaReadyCallbackAfterReadinessFlip();
    TestUnloadScriptDropsQueuedJavaReadyCallback();
    TestUnloadScriptKeepsOtherQueuedJavaReadyCallbacks();
    return 0;
}
