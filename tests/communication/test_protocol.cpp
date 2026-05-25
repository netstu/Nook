#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/protocol/messages.h"
#include "communication/protocol/tlv.h"

using namespace nook::comm;

namespace {

void TestFrameRoundTrip() {
    std::vector<uint8_t> payload = {0x11, 0x22, 0x33, 0x44};
    Frame frame(MessageType::kScriptLoad, 0x10203040u, payload);

    std::vector<uint8_t> bytes = frame.Serialize();
    assert(bytes.size() == Frame::kHeaderSize + payload.size());

    Frame parsed;
    size_t consumed = 0;
    const bool ok = Frame::Parse(bytes.data(), bytes.size(), &parsed, &consumed);
    assert(ok);
    assert(consumed == bytes.size());
    assert(parsed.GetType() == MessageType::kScriptLoad);
    assert(parsed.GetMsgId() == 0x10203040u);
    assert(parsed.GetPayload() == payload);
}

void TestFrameNeedMoreData() {
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
    Frame frame(MessageType::kPing, 7u, payload);
    std::vector<uint8_t> bytes = frame.Serialize();

    size_t needed = 0;
    const bool waiting_header = Frame::NeedMoreData(bytes.data(), 4, &needed);
    assert(waiting_header);
    assert(needed == Frame::kHeaderSize);

    const bool waiting_payload = Frame::NeedMoreData(bytes.data(), Frame::kHeaderSize + 1, &needed);
    assert(waiting_payload);
    assert(needed == bytes.size());
}

void TestTlvRoundTrip() {
    TlvEncoder nested;
    nested.PutString(1, "child");

    TlvEncoder encoder;
    encoder.PutUint32(1, 123456u);
    encoder.PutString(2, "hello");
    const uint8_t raw_bytes[] = {0x01, 0x02, 0x03};
    encoder.PutBytes(3, raw_bytes, sizeof(raw_bytes));
    encoder.PutNested(4, nested);

    std::vector<uint8_t> bytes = encoder.Build();
    assert(!bytes.empty());

    TlvDecoder decoder(bytes.data(), bytes.size());

    uint32_t value32 = 0;
    std::string text;
    std::vector<uint8_t> blob;
    TlvDecoder child;

    assert(decoder.GetUint32(1, &value32));
    assert(value32 == 123456u);
    assert(decoder.GetString(2, &text));
    assert(text == "hello");
    assert(decoder.GetBytes(3, &blob));
    assert(blob.size() == sizeof(raw_bytes));
    assert(std::memcmp(blob.data(), raw_bytes, sizeof(raw_bytes)) == 0);
    assert(decoder.GetNested(4, &child));
    assert(child.GetString(1, &text));
    assert(text == "child");
}

void TestSpawnRequestRoundTrip() {
    SpawnRequest request = {};
    request.identifier = "com.demo.target";
    request.argv = {"--debug", "--wait"};

    std::vector<uint8_t> bytes = EncodeSpawnRequest(request);
    SpawnRequest parsed = {};
    assert(DecodeSpawnRequest(bytes.data(), bytes.size(), &parsed));
    assert(parsed.identifier == request.identifier);
    assert(parsed.argv == request.argv);
}

void TestSpawnRequestAllowsEmptyIdentifier() {
    SpawnRequest request = {};
    request.argv = {"--only-args"};

    std::vector<uint8_t> bytes = EncodeSpawnRequest(request);
    SpawnRequest parsed = {};
    assert(DecodeSpawnRequest(bytes.data(), bytes.size(), &parsed));
    assert(parsed.identifier.empty());
    assert(parsed.argv == request.argv);
}

void TestAttachRequestRoundTrip() {
    AttachRequest request = {};
    request.pid = 1357u;
    request.identifier = "com.demo.target";

    std::vector<uint8_t> bytes = EncodeAttachRequest(request);
    AttachRequest parsed = {};
    assert(DecodeAttachRequest(bytes.data(), bytes.size(), &parsed));
    assert(parsed.pid == request.pid);
    assert(parsed.identifier == request.identifier);
}

void TestAttachResponseRoundTrip() {
    AttachResponse response = {};
    response.session_id = 7u;
    response.pid = 1357u;
    response.process_name = "com.demo.target";
    response.error.code = -1;
    response.error.message = "inject failed";

    std::vector<uint8_t> bytes = EncodeAttachResponse(response);
    AttachResponse parsed = {};
    assert(DecodeAttachResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.session_id == response.session_id);
    assert(parsed.pid == response.pid);
    assert(parsed.process_name == response.process_name);
    assert(parsed.error.code == response.error.code);
    assert(parsed.error.message == response.error.message);
}

void TestAgentReadyRoundTrip() {
    AgentReady ready = {};
    ready.pid = 13579u;
    ready.process_name = "com.demo.target";
    ready.spawn_token = "spawn-token-123";
    ready.arch = "arm64";
    ready.version = "0.1.0";
    ready.stage = AgentReadyStage::kControl;

    std::vector<uint8_t> bytes = EncodeAgentReady(ready);
    AgentReady parsed = {};
    assert(DecodeAgentReady(bytes.data(), bytes.size(), &parsed));
    assert(parsed.pid == ready.pid);
    assert(parsed.process_name == ready.process_name);
    assert(parsed.spawn_token == ready.spawn_token);
    assert(parsed.arch == ready.arch);
    assert(parsed.version == ready.version);
    assert(parsed.stage == ready.stage);
}

void TestScriptMessagesRoundTrip() {
    ScriptCreate create = {};
    create.session_id = 9u;
    create.name = "hello.js";
    create.source = "console.log('ok');";

    std::vector<uint8_t> create_bytes = EncodeScriptCreate(create);
    ScriptCreate parsed_create = {};
    assert(DecodeScriptCreate(create_bytes.data(), create_bytes.size(), &parsed_create));
    assert(parsed_create.session_id == create.session_id);
    assert(parsed_create.name == create.name);
    assert(parsed_create.source == create.source);

    ScriptLoad load = {};
    load.script_id = 101u;
    std::vector<uint8_t> load_bytes = EncodeScriptLoad(load);
    ScriptLoad parsed_load = {};
    assert(DecodeScriptLoad(load_bytes.data(), load_bytes.size(), &parsed_load));
    assert(parsed_load.script_id == load.script_id);

    ScriptMessage message = {};
    message.script_id = 101u;
    message.message = "{\"type\":\"log\",\"payload\":\"ok\"}";
    message.data = {0x41, 0x42};

    std::vector<uint8_t> msg_bytes = EncodeScriptMessage(message);
    ScriptMessage parsed_message = {};
    assert(DecodeScriptMessage(msg_bytes.data(), msg_bytes.size(), &parsed_message));
    assert(parsed_message.script_id == message.script_id);
    assert(parsed_message.message == message.message);
    assert(parsed_message.data == message.data);
}

void TestScriptUnloadRoundTrip() {
    ScriptUnload unload = {};
    unload.script_id = 202u;

    std::vector<uint8_t> bytes = EncodeScriptUnload(unload);
    ScriptUnload parsed = {};
    assert(DecodeScriptUnload(bytes.data(), bytes.size(), &parsed));
    assert(parsed.script_id == unload.script_id);
}

void TestProcessListRoundTrip() {
    ProcessListRequest request = {};
    std::vector<uint8_t> request_bytes = EncodeProcessListRequest(request);
    ProcessListRequest parsed_request = {};
    assert(DecodeProcessListRequest(request_bytes.data(), request_bytes.size(), &parsed_request));

    ProcessListResponse response = {};
    response.processes.push_back(ProcessEntry{123u, "system_server"});
    response.processes.push_back(ProcessEntry{456u, "com.demo.target"});

    std::vector<uint8_t> response_bytes = EncodeProcessListResponse(response);
    ProcessListResponse parsed_response = {};
    assert(DecodeProcessListResponse(response_bytes.data(), response_bytes.size(), &parsed_response));
    assert(parsed_response.error.code == 0);
    assert(parsed_response.processes.size() == 2u);
    assert(parsed_response.processes[0].pid == 123u);
    assert(parsed_response.processes[0].name == "system_server");
    assert(parsed_response.processes[1].pid == 456u);
    assert(parsed_response.processes[1].name == "com.demo.target");
}

void TestAppListRoundTrip() {
    AppListRequest request = {};
    std::vector<uint8_t> request_bytes = EncodeAppListRequest(request);
    AppListRequest parsed_request = {};
    assert(DecodeAppListRequest(request_bytes.data(), request_bytes.size(), &parsed_request));

    AppListResponse response = {};
    response.apps.push_back(AppEntry{"com.android.systemui"});
    response.apps.push_back(AppEntry{"com.demo.target"});

    std::vector<uint8_t> response_bytes = EncodeAppListResponse(response);
    AppListResponse parsed_response = {};
    assert(DecodeAppListResponse(response_bytes.data(), response_bytes.size(), &parsed_response));
    assert(parsed_response.error.code == 0);
    assert(parsed_response.apps.size() == 2u);
    assert(parsed_response.apps[0].package_name == "com.android.systemui");
    assert(parsed_response.apps[1].package_name == "com.demo.target");
}

void TestSpawnResponseErrorRoundTrip() {
    SpawnResponse response = {};
    response.pid = 42u;
    response.error.code = -7;
    response.error.message = "spawn failed";

    std::vector<uint8_t> bytes = EncodeSpawnResponse(response);
    SpawnResponse parsed = {};
    assert(DecodeSpawnResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.pid == response.pid);
    assert(parsed.error.code == response.error.code);
    assert(parsed.error.message == response.error.message);
}

void TestDetachRequestResponseRoundTrip() {
    DetachRequest request = {};
    request.session_id = 99u;

    std::vector<uint8_t> request_bytes = EncodeDetachRequest(request);
    DetachRequest parsed_request = {};
    assert(DecodeDetachRequest(request_bytes.data(), request_bytes.size(), &parsed_request));
    assert(parsed_request.session_id == request.session_id);

    DetachResponse response = {};
    response.session_id = 99u;
    response.error.code = -3;
    response.error.message = "not attached";

    std::vector<uint8_t> response_bytes = EncodeDetachResponse(response);
    DetachResponse parsed_response = {};
    assert(DecodeDetachResponse(response_bytes.data(), response_bytes.size(), &parsed_response));
    assert(parsed_response.session_id == response.session_id);
    assert(parsed_response.error.code == response.error.code);
    assert(parsed_response.error.message == response.error.message);
}

void TestResumeRequestResponseRoundTrip() {
    ResumeRequest request = {};
    request.pid = 2100u;

    std::vector<uint8_t> request_bytes = EncodeResumeRequest(request);
    ResumeRequest parsed_request = {};
    assert(DecodeResumeRequest(request_bytes.data(), request_bytes.size(), &parsed_request));
    assert(parsed_request.pid == request.pid);

    ResumeResponse response = {};
    response.pid = 2100u;
    response.error.code = -3;
    response.error.message = "not suspended";

    std::vector<uint8_t> response_bytes = EncodeResumeResponse(response);
    ResumeResponse parsed_response = {};
    assert(DecodeResumeResponse(response_bytes.data(), response_bytes.size(), &parsed_response));
    assert(parsed_response.pid == response.pid);
    assert(parsed_response.error.code == response.error.code);
    assert(parsed_response.error.message == response.error.message);
}

void TestScriptCreateResponseErrorRoundTrip() {
    ScriptCreateResponse response = {};
    response.script_id = 13u;
    response.success = false;
    response.error.code = -3;
    response.error.message = "compile error";

    std::vector<uint8_t> bytes = EncodeScriptCreateResponse(response);
    ScriptCreateResponse parsed = {};
    assert(DecodeScriptCreateResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.script_id == response.script_id);
    assert(parsed.success == response.success);
    assert(parsed.error.code == response.error.code);
    assert(parsed.error.message == response.error.message);
}

void TestScriptResponseErrorRoundTrip() {
    ScriptResponse response = {};
    response.script_id = 21u;
    response.success = false;
    response.error.code = -4;
    response.error.message = "load failed";

    std::vector<uint8_t> bytes = EncodeScriptResponse(response);
    ScriptResponse parsed = {};
    assert(DecodeScriptResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.script_id == response.script_id);
    assert(parsed.success == response.success);
    assert(parsed.error.code == response.error.code);
    assert(parsed.error.message == response.error.message);
}

void TestRpcRequestResponseRoundTrip() {
    RpcRequest request = {};
    request.script_id = 7u;
    request.method = "ping";
    request.args_json = "[\"hello\",123]";

    std::vector<uint8_t> request_bytes = EncodeRpcRequest(request);
    RpcRequest parsed_request = {};
    assert(DecodeRpcRequest(request_bytes.data(), request_bytes.size(), &parsed_request));
    assert(parsed_request.script_id == request.script_id);
    assert(parsed_request.method == request.method);
    assert(parsed_request.args_json == request.args_json);

    RpcResponse response = {};
    response.script_id = 7u;
    response.success = true;
    response.result_json = "{\"value\":\"pong\"}";

    std::vector<uint8_t> response_bytes = EncodeRpcResponse(response);
    RpcResponse parsed_response = {};
    assert(DecodeRpcResponse(response_bytes.data(), response_bytes.size(), &parsed_response));
    assert(parsed_response.script_id == response.script_id);
    assert(parsed_response.success == response.success);
    assert(parsed_response.result_json == response.result_json);
    assert(parsed_response.error.code == 0);
}

void TestSpawnInstallRequestRoundTrip() {
    SpawnInstallRequest request = {};
    request.target_package = "com.demo.target";
    request.spawn_token = "spawn-token-42";
    request.mode = "stable";

    std::vector<uint8_t> bytes = EncodeSpawnInstallRequest(request);
    SpawnInstallRequest parsed = {};
    assert(DecodeSpawnInstallRequest(bytes.data(), bytes.size(), &parsed));
    assert(parsed.target_package == request.target_package);
    assert(parsed.spawn_token == request.spawn_token);
    assert(parsed.mode == request.mode);
}

void TestSpawnInstallResponseRoundTrip() {
    SpawnInstallResponse response = {};
    response.success = true;

    std::vector<uint8_t> bytes = EncodeSpawnInstallResponse(response);
    SpawnInstallResponse parsed = {};
    assert(DecodeSpawnInstallResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.success == response.success);
    assert(parsed.error.code == 0);
    assert(parsed.error.message.empty());
}

void TestSpawnInstallResponseErrorRoundTrip() {
    SpawnInstallResponse response = {};
    response.success = false;
    response.error.code = -12;
    response.error.message = "zygote already armed";

    std::vector<uint8_t> bytes = EncodeSpawnInstallResponse(response);
    SpawnInstallResponse parsed = {};
    assert(DecodeSpawnInstallResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.success == response.success);
    assert(parsed.error.code == response.error.code);
    assert(parsed.error.message == response.error.message);
}

void TestSpawnUninstallRequestRoundTrip() {
    SpawnUninstallRequest request = {};
    request.spawn_token = "spawn-token-42";

    std::vector<uint8_t> bytes = EncodeSpawnUninstallRequest(request);
    SpawnUninstallRequest parsed = {};
    assert(DecodeSpawnUninstallRequest(bytes.data(), bytes.size(), &parsed));
    assert(parsed.spawn_token == request.spawn_token);
}

void TestSpawnUninstallResponseRoundTrip() {
    SpawnUninstallResponse response = {};
    response.success = true;

    std::vector<uint8_t> bytes = EncodeSpawnUninstallResponse(response);
    SpawnUninstallResponse parsed = {};
    assert(DecodeSpawnUninstallResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.success == response.success);
    assert(parsed.error.code == 0);
    assert(parsed.error.message.empty());
}

void TestSpawnUninstallResponseErrorRoundTrip() {
    SpawnUninstallResponse response = {};
    response.success = false;
    response.error.code = -9;
    response.error.message = "not armed";

    std::vector<uint8_t> bytes = EncodeSpawnUninstallResponse(response);
    SpawnUninstallResponse parsed = {};
    assert(DecodeSpawnUninstallResponse(bytes.data(), bytes.size(), &parsed));
    assert(parsed.success == response.success);
    assert(parsed.error.code == response.error.code);
    assert(parsed.error.message == response.error.message);
}

void TestNeedMoreDataHandlesNullptr() {
    size_t needed = 0;
    assert(Frame::NeedMoreData(nullptr, Frame::kHeaderSize, &needed));
    assert(needed == Frame::kHeaderSize);
}

void TestFrameRejectsOversizedPayload() {
    constexpr uint32_t kTooLargePayload = Frame::kMaxPayloadSize + 1u;
    std::vector<uint8_t> header(Frame::kHeaderSize, 0);
    header[0] = static_cast<uint8_t>((kTooLargePayload >> 24) & 0xFF);
    header[1] = static_cast<uint8_t>((kTooLargePayload >> 16) & 0xFF);
    header[2] = static_cast<uint8_t>((kTooLargePayload >> 8) & 0xFF);
    header[3] = static_cast<uint8_t>(kTooLargePayload & 0xFF);
    header[4] = static_cast<uint8_t>((static_cast<uint16_t>(MessageType::kScriptMessage) >> 8) & 0xFF);
    header[5] = static_cast<uint8_t>(static_cast<uint16_t>(MessageType::kScriptMessage) & 0xFF);

    Frame parsed;
    size_t consumed = 123u;
    assert(!Frame::Parse(header.data(), header.size(), &parsed, &consumed));
    assert(consumed == 0u);

    size_t needed = 123u;
    assert(!Frame::NeedMoreData(header.data(), header.size(), &needed));
    assert(needed == 0u);
}

}  // namespace

int main() {
    TestFrameRoundTrip();
    TestFrameNeedMoreData();
    TestTlvRoundTrip();
    TestSpawnRequestRoundTrip();
    TestSpawnRequestAllowsEmptyIdentifier();
    TestAttachRequestRoundTrip();
    TestAttachResponseRoundTrip();
    TestAgentReadyRoundTrip();
    TestScriptMessagesRoundTrip();
    TestScriptUnloadRoundTrip();
    TestProcessListRoundTrip();
    TestAppListRoundTrip();
    TestSpawnResponseErrorRoundTrip();
    TestDetachRequestResponseRoundTrip();
    TestResumeRequestResponseRoundTrip();
    TestScriptCreateResponseErrorRoundTrip();
    TestScriptResponseErrorRoundTrip();
    TestRpcRequestResponseRoundTrip();
    TestSpawnInstallRequestRoundTrip();
    TestSpawnInstallResponseRoundTrip();
    TestSpawnInstallResponseErrorRoundTrip();
    TestSpawnUninstallRequestRoundTrip();
    TestSpawnUninstallResponseRoundTrip();
    TestSpawnUninstallResponseErrorRoundTrip();
    TestNeedMoreDataHandlesNullptr();
    TestFrameRejectsOversizedPayload();

    std::cout << "Protocol tests passed!" << std::endl;
    return 0;
}
