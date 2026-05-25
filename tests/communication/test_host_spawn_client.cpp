#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "communication/host/host_spawn_client.h"
#include "communication/protocol/frame.h"
#include "communication/protocol/message_types.h"
#include "communication/protocol/messages.h"
#include "communication/transport/transport.h"

using namespace nook::comm;

namespace {

class FakeTransport final : public Transport {
public:
    FakeTransport() {
        state_ = TransportState::kConnected;
        connected_.store(true);
    }

    bool Connect() override {
        connected_.store(true);
        SetState(TransportState::kConnected);
        return true;
    }

    void Disconnect() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connected_.store(false);
        }
        cv_.notify_all();
        SetState(TransportState::kDisconnected);
    }

    bool IsConnected() const override {
        return connected_.load();
    }

    TransportState GetState() const override {
        return connected_.load() ? TransportState::kConnected : TransportState::kDisconnected;
    }

    ssize_t Send(const uint8_t* data, size_t len) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sent_.insert(sent_.end(), data, data + len);
        }
        cv_.notify_all();
        return static_cast<ssize_t>(len);
    }

    ssize_t Recv(uint8_t* buf, size_t len, int timeout_ms = -1) override {
        std::unique_lock<std::mutex> lock(mutex_);
        auto ready = [this]() {
            return !incoming_.empty() || !connected_.load();
        };

        if (timeout_ms >= 0) {
            cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready);
        } else {
            cv_.wait(lock, ready);
        }

        if (incoming_.empty()) {
            return connected_.load() ? 0 : -1;
        }

        size_t count = 0;
        while (count < len && !incoming_.empty()) {
            buf[count++] = incoming_.front();
            incoming_.pop_front();
        }
        return static_cast<ssize_t>(count);
    }

    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "Fake"; }

    void PushIncoming(const std::vector<uint8_t>& bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            incoming_.insert(incoming_.end(), bytes.begin(), bytes.end());
        }
        cv_.notify_all();
    }

    std::vector<uint8_t> WaitSentBytes(size_t min_size, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
            return sent_.size() >= min_size;
        });
        return sent_;
    }

    std::vector<uint8_t> TakeSentBytes() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint8_t> out = sent_;
        sent_.clear();
        return out;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<uint8_t> incoming_;
    std::vector<uint8_t> sent_;
    std::atomic<bool> connected_{false};
};

std::vector<uint8_t> SerializeFrame(MessageType type, uint32_t msg_id, std::vector<uint8_t> payload) {
    return Frame(type, msg_id, std::move(payload)).Serialize();
}

Frame ParseSingleFrame(const std::vector<uint8_t>& bytes) {
    Frame frame;
    size_t consumed = 0;
    assert(Frame::Parse(bytes.data(), bytes.size(), &frame, &consumed));
    assert(consumed == bytes.size());
    return frame;
}

void TestSpawnAndWaitSuccess() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kSpawnRequest);

        SpawnResponse response;
        response.pid = 12345u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 12345u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-12345";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         99u,
                                         EncodeAgentReady(ready)));

        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         100u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(result.spawn_response_received);
    assert(result.spawn_response.pid == 12345u);
    assert(result.agent_ready_received);
    assert(result.agent_ready.pid == 12345u);
    assert(result.agent_ready.process_name == "com.demo.target");
    assert(result.error_message.empty());

    responder.join();
}

void TestSpawnAndWaitPropagatesSpawnFailure() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.error.code = -3;
        response.error.message = "spawn injector failed";
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{1000, 100});
    assert(result.spawn_response_received);
    assert(!result.agent_ready_received);
    assert(result.error_message == "spawn injector failed");

    responder.join();
}

void TestSpawnAndWaitReportsSpawnResponseTimeoutStage() {
    auto transport = std::make_unique<FakeTransport>();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{20, 20});
    assert(!result.spawn_response_received);
    assert(!result.agent_ready_received);
    assert(result.error_message == "wait spawn response timed out");
}

void TestSpawnAndWaitReportsAgentReadyTimeoutStage() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kSpawnRequest);

        SpawnResponse response;
        response.pid = 12346u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 12346u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-12346";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         101u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{1000, 20});
    assert(result.spawn_response_received);
    assert(result.spawn_response.pid == 12346u);
    assert(!result.agent_ready_received);
    assert(result.error_message == "wait runtime agent ready timed out");

    responder.join();
}

void TestSpawnAndWaitUsesStableSpawnResponseTimeoutBudget() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kSpawnRequest);

        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        SpawnResponse response;
        response.pid = 12349u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 12349u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-12349";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         106u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{0, 20});
    assert(result.spawn_response_received);
    assert(result.spawn_response.pid == 12349u);
    assert(result.agent_ready_received);
    assert(result.agent_ready.pid == 12349u);
    assert(result.error_message.empty());

    responder.join();
}

void TestSpawnAndWaitIgnoresMismatchedRuntimeReadyForSamePid() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kSpawnRequest);

        SpawnResponse response;
        response.pid = 12347u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 12347u;
        ready.process_name = "com.demo.other";
        ready.spawn_token = "spawn-token-12347";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         102u,
                                         EncodeAgentReady(ready)));

        ready.process_name = "com.demo.target";
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         103u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(result.spawn_response_received);
    assert(result.spawn_response.pid == 12347u);
    assert(result.agent_ready_received);
    assert(result.agent_ready.pid == 12347u);
    assert(result.agent_ready.process_name == "com.demo.target");
    assert(result.error_message.empty());

    responder.join();
}

void TestSpawnAndWaitIgnoresRuntimeReadyDeliveredBeforeSpawnResponse() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kSpawnRequest);

        AgentReady early_ready;
        early_ready.pid = 12348u;
        early_ready.process_name = "com.demo.target";
        early_ready.spawn_token = "spawn-token-12348-early";
        early_ready.arch = "arm64";
        early_ready.version = "0.1.0";
        early_ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         104u,
                                         EncodeAgentReady(early_ready)));

        SpawnResponse response;
        response.pid = 12348u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady current_ready = early_ready;
        current_ready.spawn_token = "spawn-token-12348-current";
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         105u,
                                         EncodeAgentReady(current_ready)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(result.spawn_response_received);
    assert(result.spawn_response.pid == 12348u);
    assert(result.agent_ready_received);
    assert(result.agent_ready.pid == 12348u);
    assert(result.agent_ready.spawn_token == "spawn-token-12348-current");
    assert(result.error_message.empty());

    responder.join();
}

void TestWaitForScriptMessageAfterSpawn() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.pid = 23456u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 23456u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-23456";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         88u,
                                         EncodeAgentReady(ready)));

        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         89u,
                                         EncodeAgentReady(ready)));

        ScriptMessage message;
        message.script_id = 1u;
        message.message = "{\"type\":\"send\",\"payload\":\"hello-host\"}";
        message.data = {0x13, 0x37};
        raw->PushIncoming(SerializeFrame(MessageType::kScriptMessage,
                                         90u,
                                         EncodeScriptMessage(message)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(result.spawn_response_received);
    assert(result.agent_ready_received);

    ScriptMessage message;
    assert(client.WaitForScriptMessage(1000, &message));
    assert(message.message == "{\"type\":\"send\",\"payload\":\"hello-host\"}");
    assert(message.data == std::vector<uint8_t>({0x13, 0x37}));

    responder.join();
}

void TestSendScriptPostAfterSpawn() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.pid = 45678u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 45678u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-45678";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         90u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult result = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(result.spawn_response_received);
    assert(result.agent_ready_received);
    raw->TakeSentBytes();

    ScriptPost post;
    post.script_id = 12u;
    post.message = "{\"type\":\"post\",\"payload\":\"ping-agent\"}";
    post.data = {0xAA, 0xBB, 0xCC};

    std::string error_message;
    assert(client.SendScriptPost(post, &error_message));
    assert(error_message.empty());

    const Frame frame = ParseSingleFrame(raw->TakeSentBytes());
    assert(frame.GetType() == MessageType::kScriptPost);

    ScriptPost parsed;
    assert(DecodeScriptPost(frame.GetPayload().data(), frame.GetPayload().size(), &parsed));
    assert(parsed.script_id == 12u);
    assert(parsed.message == "{\"type\":\"post\",\"payload\":\"ping-agent\"}");
    assert(parsed.data == std::vector<uint8_t>({0xAA, 0xBB, 0xCC}));

    responder.join();
}

void TestCreateScriptAfterSpawn() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.pid = 50001u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 50001u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-50001";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         91u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult spawn = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(spawn.spawn_response_received);
    assert(spawn.agent_ready_received);
    raw->TakeSentBytes();

    ScriptCreate create;
    create.source = "send('hello-script');";
    create.name = "smoke.js";

    std::thread create_responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kScriptCreate);

        ScriptCreate request;
        assert(DecodeScriptCreate(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.session_id != 0u);
        assert(request.source == "send('hello-script');");
        assert(request.name == "smoke.js");

        ScriptCreateResponse response;
        response.script_id = 300u;
        response.success = true;
        raw->PushIncoming(SerializeFrame(MessageType::kScriptCreateResp,
                                         parsed.GetMsgId(),
                                         EncodeScriptCreateResponse(response)));
    });

    ScriptCreateResponse response;
    std::string error_message;
    assert(client.CreateScript(create, 1000, &response, &error_message));
    assert(error_message.empty());
    assert(response.success);
    assert(response.script_id == 300u);

    create_responder.join();
    responder.join();
}

void TestLoadScriptAfterSpawn() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.pid = 50002u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 50002u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-50002";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         92u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult spawn = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(spawn.spawn_response_received);
    assert(spawn.agent_ready_received);
    raw->TakeSentBytes();

    ScriptLoad load;
    load.script_id = 300u;

    std::thread load_responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kScriptLoad);

        ScriptLoad request;
        assert(DecodeScriptLoad(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.script_id == 300u);

        ScriptResponse response;
        response.script_id = 300u;
        response.success = true;
        raw->PushIncoming(SerializeFrame(MessageType::kScriptLoadResp,
                                         parsed.GetMsgId(),
                                         EncodeScriptResponse(response)));
    });

    ScriptResponse response;
    std::string error_message;
    assert(client.LoadScript(load, 1000, &response, &error_message));
    assert(error_message.empty());
    assert(response.success);
    assert(response.script_id == 300u);

    load_responder.join();
    responder.join();
}

void TestUnloadScriptAfterSpawn() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.pid = 50003u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 50003u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-50003";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         93u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult spawn = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(spawn.spawn_response_received);
    assert(spawn.agent_ready_received);
    raw->TakeSentBytes();

    ScriptUnload unload;
    unload.script_id = 300u;

    std::thread unload_responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kScriptUnload);

        ScriptUnload request;
        assert(DecodeScriptUnload(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.script_id == 300u);

        ScriptResponse response;
        response.script_id = 300u;
        response.success = true;
        raw->PushIncoming(SerializeFrame(MessageType::kScriptUnloadResp,
                                         parsed.GetMsgId(),
                                         EncodeScriptResponse(response)));
    });

    ScriptResponse response;
    std::string error_message;
    assert(client.UnloadScript(unload, 1000, &response, &error_message));
    assert(error_message.empty());
    assert(response.success);
    assert(response.script_id == 300u);

    unload_responder.join();
    responder.join();
}

void TestResumeAfterSpawn() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.pid = 50004u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 50004u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-50004";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         94u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult spawn = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(spawn.spawn_response_received);
    assert(spawn.agent_ready_received);
    raw->TakeSentBytes();

    std::thread resume_responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kResumeRequest);

        ResumeRequest request;
        assert(DecodeResumeRequest(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.pid == 50004u);

        ResumeResponse response;
        response.pid = request.pid;
        raw->PushIncoming(SerializeFrame(MessageType::kResumeResponse,
                                         parsed.GetMsgId(),
                                         EncodeResumeResponse(response)));
    });

    ResumeResponse response;
    std::string error_message;
    assert(client.Resume(50004u, 1000, &response, &error_message));
    assert(error_message.empty());
    assert(response.pid == 50004u);
    assert(response.error.code == 0);

    resume_responder.join();
    responder.join();
}

void TestCallRpcAfterSpawn() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest request;
    request.identifier = "com.demo.target";

    std::thread responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));

        SpawnResponse response;
        response.pid = 50005u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 50005u;
        ready.process_name = "com.demo.target";
        ready.spawn_token = "spawn-token-50005";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         95u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult spawn = client.SpawnAndWait(request, HostSpawnOptions{1000, 1000});
    assert(spawn.spawn_response_received);
    assert(spawn.agent_ready_received);
    raw->TakeSentBytes();

    std::thread rpc_responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kRpcRequest);

        RpcRequest request;
        assert(DecodeRpcRequest(parsed.GetPayload().data(), parsed.GetPayload().size(), &request));
        assert(request.script_id == 1u);
        assert(request.method == "ping");
        assert(request.args_json == "[\"hello\"]");

        RpcResponse response;
        response.script_id = request.script_id;
        response.success = true;
        response.result_json = "{\"value\":\"pong\"}";
        raw->PushIncoming(SerializeFrame(MessageType::kRpcResponse,
                                         parsed.GetMsgId(),
                                         EncodeRpcResponse(response)));
    });

    RpcResponse response;
    std::string error_message;
    RpcRequest rpc_request;
    rpc_request.script_id = 1u;
    rpc_request.method = "ping";
    rpc_request.args_json = "[\"hello\"]";
    assert(client.CallRpc(rpc_request, 1000, &response, &error_message));
    assert(error_message.empty());
    assert(response.success);
    assert(response.result_json == "{\"value\":\"pong\"}");

    rpc_responder.join();
    responder.join();
}

void TestSpawnAndWaitSucceedsAcrossRepeatedDefaultSpawnCycles() {
    auto transport = std::make_unique<FakeTransport>();
    FakeTransport* raw = transport.get();
    HostSpawnClient client(std::move(transport));

    SpawnRequest first_request;
    first_request.identifier = "com.demo.target";

    std::thread first_responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kSpawnRequest);

        SpawnResponse response;
        response.pid = 60001u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 60001u;
        ready.process_name = "zygote64";
        ready.spawn_token = "spawn-token-60001";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         201u,
                                         EncodeAgentReady(ready)));

        ready.process_name = "com.demo.target";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         202u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult first = client.SpawnAndWait(first_request, HostSpawnOptions{1000, 1000});
    assert(first.spawn_response_received);
    assert(first.agent_ready_received);
    assert(first.spawn_response.pid == 60001u);
    assert(first.agent_ready.pid == 60001u);
    assert(first.agent_ready.process_name == "com.demo.target");
    first_responder.join();
    raw->TakeSentBytes();

    SpawnRequest second_request;
    second_request.identifier = "com.demo.target";

    std::thread second_responder([raw]() {
        std::vector<uint8_t> sent = raw->WaitSentBytes(Frame::kHeaderSize, 1000);
        Frame parsed;
        size_t consumed = 0;
        assert(Frame::Parse(sent.data(), sent.size(), &parsed, &consumed));
        assert(parsed.GetType() == MessageType::kSpawnRequest);

        SpawnResponse response;
        response.pid = 60002u;
        raw->PushIncoming(SerializeFrame(MessageType::kSpawnResponse,
                                         parsed.GetMsgId(),
                                         EncodeSpawnResponse(response)));

        AgentReady ready;
        ready.pid = 60002u;
        ready.process_name = "zygote64";
        ready.spawn_token = "spawn-token-60002";
        ready.arch = "arm64";
        ready.version = "0.1.0";
        ready.stage = AgentReadyStage::kControl;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         203u,
                                         EncodeAgentReady(ready)));

        ready.process_name = "com.demo.target";
        ready.stage = AgentReadyStage::kRuntime;
        raw->PushIncoming(SerializeFrame(MessageType::kAgentReady,
                                         204u,
                                         EncodeAgentReady(ready)));
    });

    HostSpawnResult second = client.SpawnAndWait(second_request, HostSpawnOptions{1000, 1000});
    assert(second.spawn_response_received);
    assert(second.agent_ready_received);
    assert(second.spawn_response.pid == 60002u);
    assert(second.agent_ready.pid == 60002u);
    assert(second.agent_ready.process_name == "com.demo.target");
    second_responder.join();
}

}  // namespace

int main() {
    TestSpawnAndWaitSuccess();
    TestSpawnAndWaitPropagatesSpawnFailure();
    TestSpawnAndWaitReportsSpawnResponseTimeoutStage();
    TestSpawnAndWaitReportsAgentReadyTimeoutStage();
    TestSpawnAndWaitUsesStableSpawnResponseTimeoutBudget();
    TestSpawnAndWaitIgnoresMismatchedRuntimeReadyForSamePid();
    TestSpawnAndWaitIgnoresRuntimeReadyDeliveredBeforeSpawnResponse();
    TestWaitForScriptMessageAfterSpawn();
    TestSendScriptPostAfterSpawn();
    TestCreateScriptAfterSpawn();
    TestLoadScriptAfterSpawn();
    TestUnloadScriptAfterSpawn();
    TestResumeAfterSpawn();
    TestCallRpcAfterSpawn();
    TestSpawnAndWaitSucceedsAcrossRepeatedDefaultSpawnCycles();
    return 0;
}
