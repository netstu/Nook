#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

namespace {

void TestEvaluateSendCallsSink() {
    JsRuntime::Initialize();

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    std::string error_message;
    assert(registry.CreateScript("hello.js",
                                 "send({\"type\":\"send\",\"payload\":\"hello-runtime\"});",
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-runtime\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestEvaluateSendWithBinaryPayloadCallsSink() {
    JsRuntime::Initialize();

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    std::string error_message;
    const char* source =
        "var text = Memory.allocUtf8String('ABCD');"
        "var data = text.readByteArray(4);"
        "send({ type: 'dexdump-chunk', token: 'dump-1', index: 0 }, data);";
    assert(registry.CreateScript("binary_send.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"dexdump-chunk\",\"token\":\"dump-1\",\"index\":0}");
    assert(received_data.size() == 4u);
    assert(received_data[0] == static_cast<uint8_t>('A'));
    assert(received_data[1] == static_cast<uint8_t>('B'));
    assert(received_data[2] == static_cast<uint8_t>('C'));
    assert(received_data[3] == static_cast<uint8_t>('D'));

    registry.Clear();
    JsRuntime::Shutdown();
}

}  // namespace

int main() {
    TestEvaluateSendCallsSink();
    TestEvaluateSendWithBinaryPayloadCallsSink();
    return 0;
}
