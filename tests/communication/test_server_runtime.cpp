#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "communication/io/io_loop.h"
#include "communication/session/session.h"
#include "communication/transport/transport.h"
#include "server/server_runtime.h"

using namespace nook::comm;
using namespace nook::server;

namespace {

namespace fs = std::filesystem;
std::atomic<uint32_t> g_temp_dir_counter{0};

class NullTransport final : public Transport {
public:
    NullTransport() {
        state_ = TransportState::kConnected;
    }

    bool Connect() override { return true; }
    void Disconnect() override { SetState(TransportState::kDisconnected); }
    bool IsConnected() const override { return GetState() == TransportState::kConnected; }
    TransportState GetState() const override { return state_; }
    ssize_t Send(const uint8_t*, size_t len) override { return static_cast<ssize_t>(len); }
    ssize_t Recv(uint8_t*, size_t, int = -1) override { return -1; }
    int GetFd() const override { return -1; }
    const char* GetTypeName() const override { return "Null"; }
};

void TestResolveAgentPathPrefersEnvironmentOverride() {
    const std::string path = ResolveAgentPathFromEnvironmentAndExecutable(
        "/custom/agent.so",
        "/data/local/tmp/nook-test/nook-server");
    assert(path == "/custom/agent.so");
}

void TestResolveAgentPathUsesExecutableDirectory() {
    const std::string path = ResolveAgentPathFromEnvironmentAndExecutable(
        nullptr,
        "/data/local/tmp/nook-test/nook-server");
    assert(path == "/data/local/tmp/nook-test/libnook-agent.so");
}

void TestResolveAgentPathFallsBackToLegacyLocation() {
    const std::string path = ResolveAgentPathFromEnvironmentAndExecutable(nullptr, nullptr);
    assert(path == "/data/local/tmp/nook/libnook-agent.so");
}

void TestResolveRuntimeDirectoryUsesExecutableDirectory() {
    const std::string path = ResolveRuntimeDirectoryFromEnvironmentAndExecutable(
        nullptr,
        "/data/local/tmp/nook-test/nook-server");
    assert(path == "/data/local/tmp/nook-test");
}

void TestResolveRuntimeDirectoryPrefersEnvironmentOverride() {
    const std::string path = ResolveRuntimeDirectoryFromEnvironmentAndExecutable(
        "/custom/runtime",
        "/data/local/tmp/nook-test/nook-server");
    assert(path == "/custom/runtime");
}

void TestResolveRuntimeDirectoryFallsBackToLegacyLocation() {
    const std::string path = ResolveRuntimeDirectoryFromEnvironmentAndExecutable(nullptr, nullptr);
    assert(path == "/data/local/tmp/nook");
}

void TestBuildSocketPathFromRuntimeDirectory() {
    const std::string path = BuildSocketPathFromRuntimeDirectory("/data/local/tmp/nook-test");
#if defined(__ANDROID__)
    assert(path == "@nook-830427b3.sock");
#else
    assert(path == "/tmp/nook.sock");
#endif
}

void TestResolveNcorePathFromRuntimeDirectory() {
    const std::string path = ResolveNcorePathFromRuntimeDirectory("/data/local/tmp/nook-test");
    assert(path == "/data/local/tmp/nook-test/libncore.so");
}

void TestResolveNcorePathPrefersEnvironmentOverride() {
    const std::string path = ResolveNcorePathFromEnvironmentAndRuntimeDirectory(
        "/custom/libncore.so",
        "/data/local/tmp/nook-test");
    assert(path == "/custom/libncore.so");
}

void TestResolveNcorePathFallsBackToLegacyLocation() {
    const std::string path = ResolveNcorePathFromRuntimeDirectory("");
    assert(path == "/data/local/tmp/nook/libncore.so");
}

void TestResolveNcorePathUsesRuntimeDirectoryWhenEnvironmentMissing() {
    const std::string path = ResolveNcorePathFromEnvironmentAndRuntimeDirectory(
        nullptr,
        "/data/local/tmp/nook-test");
    assert(path == "/data/local/tmp/nook-test/libncore.so");
}

std::vector<uint8_t> ReadAllBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
}

fs::path MakeTemporaryDirectory() {
    const fs::path root = fs::temp_directory_path() / "nook_server_runtime_tests";
    fs::create_directories(root);
    const uint32_t id = g_temp_dir_counter.fetch_add(1, std::memory_order_relaxed);
    const fs::path dir = root / ("case-" + std::to_string(id));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void TestEnsureEmbeddedFileCreatesMissingFile() {
    const std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};
    const fs::path dir = MakeTemporaryDirectory();
    const fs::path output = dir / "libnook-agent.so";
    std::string error;
    EmbeddedFileMaterializationResult result = EmbeddedFileMaterializationResult::kError;

    const bool ok = EnsureEmbeddedFileAtPath(output.string().c_str(),
                                             payload.data(),
                                             payload.size(),
                                             &result,
                                             &error);

    assert(ok);
    assert(error.empty());
    assert(result == EmbeddedFileMaterializationResult::kCreated);
    assert(fs::exists(output));
    assert(ReadAllBytes(output) == payload);
    fs::remove_all(dir);
}

void TestEnsureEmbeddedFileReusesMatchingFile() {
    const std::vector<uint8_t> payload = {0x10, 0x20, 0x30};
    const fs::path dir = MakeTemporaryDirectory();
    const fs::path output = dir / "libnook-agent.so";
    {
        std::ofstream stream(output, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(payload.data()),
                     static_cast<std::streamsize>(payload.size()));
    }
    const auto before = fs::last_write_time(output);
    std::string error;
    EmbeddedFileMaterializationResult result = EmbeddedFileMaterializationResult::kError;

    const bool ok = EnsureEmbeddedFileAtPath(output.string().c_str(),
                                             payload.data(),
                                             payload.size(),
                                             &result,
                                             &error);

    assert(ok);
    assert(error.empty());
    assert(result == EmbeddedFileMaterializationResult::kReused);
    assert(fs::last_write_time(output) == before);
    assert(ReadAllBytes(output) == payload);
    fs::remove_all(dir);
}

void TestEnsureEmbeddedFileReplacesStaleFile() {
    const std::vector<uint8_t> stale = {0xaa, 0xbb};
    const std::vector<uint8_t> payload = {0x99, 0x88, 0x77, 0x66};
    const fs::path dir = MakeTemporaryDirectory();
    const fs::path output = dir / "libnook-agent.so";
    {
        std::ofstream stream(output, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(stale.data()),
                     static_cast<std::streamsize>(stale.size()));
    }
    std::string error;
    EmbeddedFileMaterializationResult result = EmbeddedFileMaterializationResult::kError;

    const bool ok = EnsureEmbeddedFileAtPath(output.string().c_str(),
                                             payload.data(),
                                             payload.size(),
                                             &result,
                                             &error);

    assert(ok);
    assert(error.empty());
    assert(result == EmbeddedFileMaterializationResult::kReplaced);
    assert(ReadAllBytes(output) == payload);
    fs::remove_all(dir);
}

void TestEnsureEmbeddedFileIfMissingPreservesExistingFile() {
    const std::vector<uint8_t> existing = {0xde, 0xad, 0xbe, 0xef};
    const std::vector<uint8_t> embedded = {0x11, 0x22, 0x33};
    const fs::path dir = MakeTemporaryDirectory();
    const fs::path output = dir / "libnook-agent.so";
    {
        std::ofstream stream(output, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(existing.data()),
                     static_cast<std::streamsize>(existing.size()));
    }

    std::string error;
    EmbeddedFileMaterializationResult result = EmbeddedFileMaterializationResult::kError;
    const auto before = fs::last_write_time(output);

    const bool ok = EnsureEmbeddedFileAtPathIfMissing(output.string().c_str(),
                                                      embedded.data(),
                                                      embedded.size(),
                                                      &result,
                                                      &error);

    assert(ok);
    assert(error.empty());
    assert(result == EmbeddedFileMaterializationResult::kPreserved);
    assert(fs::last_write_time(output) == before);
    assert(ReadAllBytes(output) == existing);
    fs::remove_all(dir);
}

void TestResolveAgentPathFallsBackWhenExecutableHasNoDirectory() {
    const std::string path = ResolveAgentPathFromEnvironmentAndExecutable(nullptr, "nook-server");
    assert(path == "/data/local/tmp/nook/libnook-agent.so");
}

void TestBuildEmbeddedAgentPathUsesStableRuntimePath() {
    const std::vector<uint8_t> payload_a = {0x01, 0x02, 0x03};
    const std::vector<uint8_t> payload_b = {0x10, 0x20, 0x30, 0x40};

    const std::string path_a = BuildEmbeddedAgentPathForRuntimeDirectory(
        "/data/local/tmp/nook-test",
        payload_a.data(),
        payload_a.size());
    const std::string path_b = BuildEmbeddedAgentPathForRuntimeDirectory(
        "/data/local/tmp/nook-test",
        payload_b.data(),
        payload_b.size());

    assert(path_a == "/data/local/tmp/nook-test/libnook-agent.so");
    assert(path_b == "/data/local/tmp/nook-test/libnook-agent.so");
}

void TestBuildEmbeddedNcorePathUsesStableRuntimePath() {
    const std::vector<uint8_t> payload = {0xaa, 0xbb, 0xcc};
    const std::string path = BuildEmbeddedNcorePathForRuntimeDirectory(
        "/data/local/tmp/nook-test",
        payload.data(),
        payload.size());
    assert(path == "/data/local/tmp/nook-test/libncore.so");
}

void TestCleanupEmbeddedNcoreArtifactsRemovesStaleCopies() {
    const fs::path dir = MakeTemporaryDirectory();
    const fs::path keep = dir / "libncore.so";
    const fs::path stale = dir / "libncore-old.so";
    const fs::path other = dir / "other.txt";

    {
        std::ofstream keep_stream(keep, std::ios::binary);
        keep_stream << "keep";
    }
    {
        std::ofstream stale_stream(stale, std::ios::binary);
        stale_stream << "stale";
    }
    {
        std::ofstream other_stream(other, std::ios::binary);
        other_stream << "other";
    }

    std::string error;
    const bool ok = CleanupEmbeddedNcoreArtifacts(dir.string(), keep.string(), &error);
    assert(ok);
    assert(error.empty());
    assert(fs::exists(keep));
    assert(!fs::exists(stale));
    assert(fs::exists(other));
    fs::remove_all(dir);
}

void TestRemoveFileIfExistsRemovesPresentFile() {
    const fs::path dir = MakeTemporaryDirectory();
    const fs::path file = dir / "libncore.so";
    {
        std::ofstream stream(file, std::ios::binary);
        stream << "payload";
    }

    std::string error;
    assert(RemoveFileIfExists(file.string(), &error));
    assert(error.empty());
    assert(!fs::exists(file));
    fs::remove_all(dir);
}

void TestRemoveFileIfExistsIgnoresMissingFile() {
    const fs::path dir = MakeTemporaryDirectory();
    const fs::path file = dir / "missing.so";
    std::string error;
    assert(RemoveFileIfExists(file.string(), &error));
    assert(error.empty());
    fs::remove_all(dir);
}

void TestResolveSpawnGateAgentSessionPrefersSpawnRuntimeIdentity() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto wrong_runtime_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    assert(control_agent->Start());
    assert(wrong_runtime_agent->Start());

    registry.RegisterAgentSession(7001, control_agent.get());
    registry.RegisterControlReadyAgentSession(7001, control_agent.get());
    registry.RegisterAgentProcessName(7001, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7001);
    registry.MarkAgentReadyStage(7001, AgentReadyStage::kControl);

    registry.MarkSpawnSuspended(7001,
                                77u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.target",
                                "com.demo.target");

    registry.RegisterAgentSession(7001, wrong_runtime_agent.get());
    registry.RegisterAgentProcessName(7001, "com.demo.other");
    registry.MarkAgentReadyStage(7001, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7001);

    assert(ResolveSpawnGateAgentSession(&registry, 7001) == wrong_runtime_agent.get());
}

void TestResolveSpawnGateAgentSessionFallsBackWhenNotSpawnRuntimeBound() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    assert(control_agent->Start());

    registry.RegisterAgentSession(7002, control_agent.get());
    registry.RegisterControlReadyAgentSession(7002, control_agent.get());
    registry.RegisterAgentProcessName(7002, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7002);
    registry.MarkAgentReadyStage(7002, AgentReadyStage::kControl);

    assert(ResolveSpawnGateAgentSession(&registry, 7002) == control_agent.get());
}

void TestResolveSpawnGateAgentSessionRejectsPidFallbackForMismatchedIdentity() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    assert(control_agent->Start());

    registry.RegisterAgentSession(7004, control_agent.get());
    registry.RegisterControlReadyAgentSession(7004, control_agent.get());
    registry.RegisterAgentProcessName(7004, "com.demo.other");
    registry.MarkAgentAuthoritativeReady(7004);
    registry.MarkAgentReadyStage(7004, AgentReadyStage::kControl);

    registry.MarkSpawnSuspended(7004,
                                79u,
                                PendingSpawnReadyStage::kControlReady,
                                "com.demo.target",
                                "com.demo.target");

    assert(ResolveSpawnGateAgentSession(&registry, 7004) == nullptr);
}

void TestResolveSpawnGateAgentSessionPrefersAuthoritativeRuntimeIdentityOverTargetName() {
    SessionRegistry registry;

    auto control_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    auto runtime_agent = std::make_unique<Session>(std::make_unique<NullTransport>());
    assert(control_agent->Start());
    assert(runtime_agent->Start());

    registry.RegisterAgentSession(7003, control_agent.get());
    registry.RegisterControlReadyAgentSession(7003, control_agent.get());
    registry.RegisterAgentProcessName(7003, "com.demo.target");
    registry.MarkAgentAuthoritativeReady(7003);
    registry.MarkAgentReadyStage(7003, AgentReadyStage::kControl);

    registry.MarkSpawnSuspended(7003,
                                78u,
                                PendingSpawnReadyStage::kRuntimeReady,
                                "com.demo.runtime",
                                "com.demo.target");

    registry.RegisterAgentSession(7003, runtime_agent.get());
    registry.RegisterAgentProcessName(7003, "com.demo.runtime");
    registry.MarkAgentReadyStage(7003, AgentReadyStage::kRuntime);
    registry.MarkAgentRuntimeReady(7003);

    assert(ResolveSpawnGateAgentSession(&registry, 7003) == runtime_agent.get());
}

}  // namespace

int main() {
    IoLoop io_loop;
    std::atomic<bool> task_executed{false};

    RunIoLoopUntilStop(&io_loop, [&]() {
        io_loop.Post([&]() {
            task_executed.store(true, std::memory_order_release);
        });

        for (int i = 0; i < 20; ++i) {
            if (task_executed.load(std::memory_order_acquire)) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        assert(false && "io loop task should run before shutdown");
    });

    assert(task_executed.load(std::memory_order_acquire));
    TestResolveAgentPathPrefersEnvironmentOverride();
    TestResolveAgentPathUsesExecutableDirectory();
    TestResolveAgentPathFallsBackToLegacyLocation();
    TestResolveRuntimeDirectoryUsesExecutableDirectory();
    TestResolveRuntimeDirectoryPrefersEnvironmentOverride();
    TestResolveRuntimeDirectoryFallsBackToLegacyLocation();
    TestBuildSocketPathFromRuntimeDirectory();
    TestResolveNcorePathFromRuntimeDirectory();
    TestResolveNcorePathPrefersEnvironmentOverride();
    TestResolveNcorePathFallsBackToLegacyLocation();
    TestResolveNcorePathUsesRuntimeDirectoryWhenEnvironmentMissing();
    TestEnsureEmbeddedFileCreatesMissingFile();
    TestEnsureEmbeddedFileReusesMatchingFile();
    TestEnsureEmbeddedFileReplacesStaleFile();
    TestEnsureEmbeddedFileIfMissingPreservesExistingFile();
    TestResolveAgentPathFallsBackWhenExecutableHasNoDirectory();
    TestBuildEmbeddedAgentPathUsesStableRuntimePath();
    TestBuildEmbeddedNcorePathUsesStableRuntimePath();
    TestCleanupEmbeddedNcoreArtifactsRemovesStaleCopies();
    TestRemoveFileIfExistsRemovesPresentFile();
    TestRemoveFileIfExistsIgnoresMissingFile();
    TestResolveSpawnGateAgentSessionPrefersSpawnRuntimeIdentity();
    TestResolveSpawnGateAgentSessionFallsBackWhenNotSpawnRuntimeBound();
    TestResolveSpawnGateAgentSessionRejectsPidFallbackForMismatchedIdentity();
    TestResolveSpawnGateAgentSessionPrefersAuthoritativeRuntimeIdentityOverTargetName();
    return 0;
}
