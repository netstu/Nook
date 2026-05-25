#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "communication/protocol/messages.h"
#define private public
#include "server/ninjector_spawn_injector.h"
#undef private

using namespace nook::comm;
using namespace nook::server;

namespace {

namespace fs = std::filesystem;

void SetEnvValue(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

struct TraceState {
    std::vector<std::string> calls;
    std::string ncore_path;
    std::string clear_ncore_path;
    std::string package_name;
    std::string so_path;
    std::string runtime_dir;
    std::string spawn_token;
    bool strict_request = false;
};

NinjectorSpawnOps MakeBaseOps(TraceState* trace) {
    NinjectorSpawnOps ops;
    ops.get_pid = [trace](const char* process_name) {
        trace->calls.push_back(std::string("get_pid:") + process_name);
        return 791;
    };
    ops.is_zygote_monitor_ready = [](int zygote_pid) {
        (void)zygote_pid;
        return false;
    };
    ops.spawn_symbi = [trace](int zygote_pid,
                              const char* package_name,
                              const char* so_path,
                              const char* runtime_dir,
                              const char* spawn_token,
                              int* child_pid) {
        trace->calls.push_back(std::string("symbi:") + std::to_string(zygote_pid));
        trace->package_name = package_name != nullptr ? package_name : "";
        trace->so_path = so_path != nullptr ? so_path : "";
        trace->runtime_dir = runtime_dir != nullptr ? runtime_dir : "";
        trace->spawn_token = spawn_token != nullptr ? spawn_token : "";
        if (child_pid != nullptr) {
            *child_pid = 17001;
        }
        return true;
    };
    ops.spawn_symbi_embedded = [trace](int zygote_pid,
                                       const char* package_name,
                                       const char* runtime_dir,
                                       const char* spawn_token,
                                       int* child_pid) {
        trace->calls.push_back(std::string("symbi-embedded:") + std::to_string(zygote_pid));
        trace->package_name = package_name != nullptr ? package_name : "";
        trace->so_path.clear();
        trace->runtime_dir = runtime_dir != nullptr ? runtime_dir : "";
        trace->spawn_token = spawn_token != nullptr ? spawn_token : "";
        if (child_pid != nullptr) {
            *child_pid = 17001;
        }
        return true;
    };
    ops.prepare_spawn = [trace](int zygote_pid,
                                const char* ncore_path,
                                const char* package_name,
                                const char* so_path,
                                const char* spawn_token) {
        trace->calls.push_back(std::string("prepare:") + std::to_string(zygote_pid));
        trace->ncore_path = ncore_path;
        trace->package_name = package_name;
        trace->so_path = so_path;
        trace->calls.push_back(std::string("token:") + (spawn_token != nullptr ? spawn_token : ""));
        return true;
    };
    ops.clear_spawn = [trace](int zygote_pid, const char* ncore_path, const char* spawn_token) {
        trace->calls.push_back(std::string("clear:") + std::to_string(zygote_pid));
        trace->clear_ncore_path = ncore_path;
        trace->calls.push_back(std::string("clear-token:") + (spawn_token != nullptr ? spawn_token : ""));
        return true;
    };
    ops.inject_embedded_agent_by_pid = [trace](int pid,
                                               const char* runtime_dir,
                                               const char* ready_token) {
        trace->calls.push_back(std::string("inject-memfd:") + std::to_string(pid));
        trace->runtime_dir = runtime_dir != nullptr ? runtime_dir : "";
        trace->spawn_token = ready_token != nullptr ? ready_token : "";
        return true;
    };
    ops.inject_so_by_pid = [trace](int pid, const char* so_path, const char* ready_token) {
        trace->calls.push_back(std::string("inject:") + std::to_string(pid));
        trace->so_path = so_path != nullptr ? so_path : "";
        trace->spawn_token = ready_token != nullptr ? ready_token : "";
        return true;
    };
    ops.inject_zygote_so_by_pid = [trace](int pid, const char* runtime_dir_or_so_path) {
        trace->calls.push_back(std::string("zygote-inject-memfd:") + std::to_string(pid));
        if (runtime_dir_or_so_path == nullptr || runtime_dir_or_so_path[0] == '\0') {
            trace->runtime_dir.clear();
            return true;
        }
        const std::string value(runtime_dir_or_so_path);
        constexpr const char* kHelperRuntimeDirPrefix =
            "__embedded_zygote_helper_runtime_dir__:";
        if (value.rfind(kHelperRuntimeDirPrefix, 0) == 0) {
            trace->runtime_dir = value.substr(std::strlen(kHelperRuntimeDirPrefix));
            return true;
        }
        if (value == "__embedded_zygote_helper__" ||
            value == "__embedded_agent__") {
            const char* env_runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
            trace->runtime_dir = env_runtime_dir != nullptr ? env_runtime_dir : "";
            return true;
        }
        if (!value.empty() && value.find(".so") == std::string::npos) {
            trace->runtime_dir = value;
        } else {
            trace->runtime_dir = fs::path(value).parent_path().string();
        }
        return true;
    };
    ops.get_inject_error = []() {
        return std::string();
    };
    ops.start_target_app = [trace](const char* package_name) {
        trace->calls.push_back(std::string("start:") + package_name);
        return true;
    };
    ops.set_zygote_spawn_control = [trace](int zygote_pid,
                                           const char* package_name,
                                           const char* spawn_token,
                                           bool strict_request) {
        trace->calls.push_back(std::string("set-control:") + std::to_string(zygote_pid));
        trace->package_name = package_name != nullptr ? package_name : "";
        trace->spawn_token = spawn_token != nullptr ? spawn_token : "";
        trace->strict_request = strict_request;
        return true;
    };
    ops.clear_zygote_spawn_control = [trace](int zygote_pid,
                                             const char* spawn_token,
                                             bool strict_request) {
        trace->calls.push_back(std::string("clear-control:") + std::to_string(zygote_pid));
        trace->spawn_token = spawn_token != nullptr ? spawn_token : "";
        trace->strict_request = strict_request;
        return true;
    };
    return ops;
}

SpawnRequest MakeSpawnRequest(const std::string& identifier,
                              const std::vector<std::string>& argv = {}) {
    SpawnRequest request;
    request.identifier = identifier;
    request.argv = argv;
    return request;
}

fs::path MakeTempDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / "nook_spawn_injector_tests" / name;
    std::error_code remove_error;
    fs::remove_all(dir, remove_error);
    fs::create_directories(dir);
    return dir;
}

void RemoveAllIgnoringMissing(const fs::path& path) {
    std::error_code remove_error;
    fs::remove_all(path, remove_error);
}

void AssertTraceEquals(const std::vector<std::string>& actual,
                       const std::vector<std::string>& expected) {
    if (actual == expected) {
        return;
    }

    std::fprintf(stderr, "trace mismatch\nactual:\n");
    for (const auto& item : actual) {
        std::fprintf(stderr, "  %s\n", item.c_str());
    }
    std::fprintf(stderr, "expected:\n");
    for (const auto& item : expected) {
        std::fprintf(stderr, "  %s\n", item.c_str());
    }
    assert(false && "trace mismatch");
}

void TestSpawnStrictZygoteControlPrefersZygoteControlPath() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    bool install_called = false;
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int zygote_pid,
            const std::string& process_name,
            const std::string& agent_path,
            const std::string& target_package,
            const std::string& spawn_token,
            std::string* error_message) {
            install_called = true;
            trace.calls.push_back(std::string("install:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "usap64");
            assert(agent_path == "/data/local/tmp/nook/libnook-agent.so");
            assert(target_package == "com.demo.target");
            assert(spawn_token == "spawn-token-1");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int zygote_pid, const std::string& process_name, std::string* error_message) {
            uninstall_called = true;
            trace.calls.push_back(std::string("uninstall:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "usap64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        {},
        []() {
            return std::vector<ProcessInfo>{
                {791, "zygote64"},
                {792, "usap64"},
            };
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-1"}),
                          "/data/local/tmp/nook/libnook-agent.so",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(install_called);
    assert(!uninstall_called);
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "get_pid:zygote64",
                          "zygote-inject-memfd:791",
                          "install:791",
                          "zygote-inject-memfd:792",
                          "install:792",
                          "start:com.demo.target"});

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(uninstall_called);
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "get_pid:zygote64",
                          "zygote-inject-memfd:791",
                          "install:791",
                          "zygote-inject-memfd:792",
                          "install:792",
                          "start:com.demo.target",
                          "uninstall:791",
                          "uninstall:792"});
}

void TestSpawnDefaultStablePathSkipsZygoteControlWhenEnabled() {
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_ALLOW_SYMBI_FALLBACK", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("default_stable_skips_zygote_control");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-spawn-token=spawn-token-default-stable"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "get_pid:zygote64",
                          "symbi-embedded:791"});
    assert(trace.spawn_token == "spawn-token-default-stable");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "get_pid:zygote64",
                          "symbi-embedded:791"});

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnReinjectsWhenOnlyPreexistingControlReadySessionExistsWithoutOwnedTarget() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.is_zygote_monitor_ready = [](int) {
        return true;
    };

    bool install_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int zygote_pid,
            const std::string& process_name,
            const std::string&,
            const std::string&,
            const std::string&,
            std::string* error_message) {
            install_called = true;
            trace.calls.push_back(std::string("install:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [](int, const std::string&) {
            return false;
        },
        []() {
            return std::vector<ProcessInfo>{{791, "zygote64"}};
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-reinject"}),
                          "/data/local/tmp/nook/libnook-agent.so",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(install_called);
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "get_pid:zygote64",
                          "zygote-inject-memfd:791",
                          "install:791",
                          "start:com.demo.target"});
}

void TestSpawnStrictZygoteControlAbortsWhenInstallFails() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("strict_install_fail_abort");
    const fs::path agent = dir / "libnook-agent.so";

    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            trace.calls.push_back("install-fail");
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        [&](int, const std::string&, std::string* error_message) {
            uninstall_called = true;
            trace.calls.push_back("uninstall");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-strict-zygote-control",
                                             "--nook-spawn-token=spawn-token-2"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message ==
           "zygote-control stage=spawn class=soft state=install-hook detail=install zygote fork hook failed: rpc timeout");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install-fail"}));
    assert(!uninstall_called);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnStrictZygoteControlAbortsWhenControlFails() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("no_silent_legacy_fallback");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            trace.calls.push_back("install-fail");
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-strict-zygote-control",
                                             "--nook-spawn-token=spawn-token-no-fallback"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message ==
           "zygote-control stage=spawn class=soft state=install-hook detail=install zygote fork hook failed: rpc timeout");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install-fail"}));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnZygoteControlArmsBothZygoteFamiliesWhenPresent() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    bool install_called = false;
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int zygote_pid,
            const std::string& process_name,
            const std::string& agent_path,
            const std::string& target_package,
            const std::string& spawn_token,
            std::string* error_message) {
            install_called = true;
            trace.calls.push_back(std::string("install:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "zygote");
            assert(agent_path == "/data/local/tmp/nook/libnook-agent.so");
            assert(target_package == "com.demo.target");
            assert(spawn_token == "spawn-token-both-families");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int zygote_pid, const std::string& process_name, std::string* error_message) {
            uninstall_called = true;
            trace.calls.push_back(std::string("uninstall:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "zygote");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        {},
        []() {
            return std::vector<ProcessInfo>{
                {791, "zygote64"},
                {792, "zygote"},
            };
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(
        MakeSpawnRequest("com.demo.target",
                         {"--nook-strict-zygote-control",
                          "--nook-spawn-token=spawn-token-both-families"}),
        "/data/local/tmp/nook/libnook-agent.so",
        &pid,
        &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(install_called);
    assert(!uninstall_called);
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "get_pid:zygote64",
                          "zygote-inject-memfd:791",
                          "install:791",
                          "zygote-inject-memfd:792",
                          "install:792",
                          "start:com.demo.target"});

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(uninstall_called);
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "get_pid:zygote64",
                          "zygote-inject-memfd:791",
                          "install:791",
                          "zygote-inject-memfd:792",
                          "install:792",
                          "start:com.demo.target",
                          "uninstall:791",
                          "uninstall:792"});
}

void TestSpawnStrictZygoteControlPassesStrictFlagToArmControl() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        {},
        {},
        {},
        []() {
            return std::vector<ProcessInfo>{{791, "zygote64"}};
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-strict-prop"}),
                          "__embedded_agent__",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(trace.strict_request);
}

void TestSpawnReinjectsWhenMonitorReadyButSessionMissing() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.is_zygote_monitor_ready = [&trace](int zygote_pid) {
        trace.calls.push_back(std::string("monitor-ready:") + std::to_string(zygote_pid));
        return true;
    };

    bool install_called = false;
    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int zygote_pid,
            const std::string& process_name,
            const std::string& agent_path,
            const std::string& target_package,
            const std::string& spawn_token,
            std::string* error_message) {
            install_called = true;
            trace.calls.push_back(std::string("install:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64");
            assert(agent_path == "/data/local/tmp/nook/libnook-agent.so");
            assert(target_package == "com.demo.target");
            assert(spawn_token == "spawn-token-stale");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        {},
        [](int, const std::string&) {
            return false;
        },
        []() {
            return std::vector<ProcessInfo>{
                {791, "zygote64"},
            };
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-stale"}),
                          "/data/local/tmp/nook/libnook-agent.so",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(install_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install:791",
                               "start:com.demo.target"}));
}

void TestSpawnSkipsReinjectWhenMonitorReadyAndSessionPresent() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.is_zygote_monitor_ready = [&trace](int zygote_pid) {
        trace.calls.push_back(std::string("monitor-ready:") + std::to_string(zygote_pid));
        return true;
    };

    bool install_called = false;
    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int zygote_pid,
            const std::string& process_name,
            const std::string& agent_path,
            const std::string& target_package,
            const std::string& spawn_token,
            std::string* error_message) {
            install_called = true;
            trace.calls.push_back(std::string("install:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64");
            assert(agent_path == "/data/local/tmp/nook/libnook-agent.so");
            assert(target_package == "com.demo.target");
            assert(spawn_token == "spawn-token-warm");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        {},
        [](int zygote_pid, const std::string& process_name) {
            return zygote_pid == 791 && process_name == "zygote64";
        },
        []() {
            return std::vector<ProcessInfo>{
                {791, "zygote64"},
            };
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-warm"}),
                          "/data/local/tmp/nook/libnook-agent.so",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(install_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "monitor-ready:791",
                               "install:791",
                               "start:com.demo.target"}));
}

void TestSpawnZygoteControlInstallFailureClearsArmedControlBeforeFallback() {
    SetEnvValue("NOOK_ALLOW_SYMBI_FALLBACK", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("zygote_install_failure_clears_arm");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            trace.calls.push_back("install-fail");
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        [&](int, const std::string&, std::string* error_message) {
            trace.calls.push_back("uninstall");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-install-fail"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());

    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install-fail",
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:spawn-token-install-fail",
                               "start:com.demo.target"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_SYMBI_FALLBACK", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnZygoteControlStartFailureClearsArmedControlImmediately() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.start_target_app = [&trace](const char* package_name) {
        trace.calls.push_back(std::string("start-fail:") + package_name);
        return false;
    };

    bool uninstall_called = false;
    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int zygote_pid,
            const std::string& process_name,
            const std::string&,
            const std::string&,
            const std::string&,
            std::string* error_message) {
            trace.calls.push_back(std::string("install:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "usap64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int zygote_pid, const std::string& process_name, std::string* error_message) {
            uninstall_called = true;
            trace.calls.push_back(std::string("uninstall:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "usap64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        {},
        []() {
            return std::vector<ProcessInfo>{
                {791, "zygote64"},
                {792, "usap64"},
            };
        });

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-strict-zygote-control",
                                             "--nook-spawn-token=spawn-token-start-fail"}),
                           "/data/local/tmp/nook/libnook-agent.so",
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "zygote-control stage=spawn class=hard state=launch-app detail=start_target_app failed");
    assert(uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install:791",
                               "zygote-inject-memfd:792",
                               "install:792",
                               "start-fail:com.demo.target",
                               "uninstall:791",
                               "uninstall:792"}));
}

void TestSpawnZygoteControlArmFailureDoesNotFallbackByDefault() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.set_zygote_spawn_control = [&trace](int zygote_pid,
                                            const char* package_name,
                                            const char* spawn_token,
                                            bool strict_request) {
        trace.calls.push_back(std::string("set-control-fail:") + std::to_string(zygote_pid));
        trace.package_name = package_name != nullptr ? package_name : "";
        trace.spawn_token = spawn_token != nullptr ? spawn_token : "";
        trace.strict_request = strict_request;
        return false;
    };
    ops.get_inject_error = []() {
        return std::string("arm_stage_failed");
    };

    const fs::path dir = MakeTempDir("zygote_arm_failure_no_fallback");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-strict-zygote-control",
                                             "--nook-spawn-token=arm-fail-token"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "zygote-control stage=spawn class=hard state=arm-control detail=set zygote spawn control failed: arm_stage_failed");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "set-control-fail:791"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnServerStrictEnvDoesNotPromoteDefaultSpawnRoute() {
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_STRICT_ZYGOTE_CONTROL", "1");
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("server_strict_env_ignored_by_default_route");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-spawn-token=env-ignored-token"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.spawn_token == "env-ignored-token");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
    SetEnvValue("NOOK_STRICT_ZYGOTE_CONTROL", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnStrictZygoteControlRequestArgAbortsOnSoftInstallFailure() {
    SetEnvValue("NOOK_STRICT_ZYGOTE_CONTROL", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("strict_zygote_control_request_arg_no_fallback");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            trace.calls.push_back("install-fail");
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(
        MakeSpawnRequest("com.demo.target",
                         {"--nook-strict-zygote-control", "--nook-spawn-token=strict-arg-token"}),
        agent.string(),
        &pid,
        &error_message));
    assert(pid == 0);
    assert(error_message ==
           "zygote-control stage=spawn class=soft state=install-hook detail=install zygote fork hook failed: rpc timeout");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install-fail"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnStrictHelperLocalControlSkipsRpcInstallButStillUninstalls() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.uninstall_embedded_zygote_control_hooks = [&trace](int pid) {
        trace.calls.push_back(std::string("local-uninstall:") + std::to_string(pid));
        return true;
    };

    bool install_called = false;
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string*) {
            install_called = true;
            return true;
        },
        [&](int, const std::string&, std::string*) {
            uninstall_called = true;
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=strict-helper-token"}),
                          "__embedded_agent__",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(!install_called);
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "set-control:791",
                               "start:com.demo.target"}));
    assert(injector.active_spawn_owner_.zygote_control_transaction.helper_only_local_control);

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!install_called);
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "set-control:791",
                               "start:com.demo.target",
                               "local-uninstall:791",
                               "clear-control:791"}));
}

void TestSpawnStrictRequestEmbeddedZygoteControlUsesHelperLocalControl() {
    SetEnvValue("NOOK_STRICT_ZYGOTE_CONTROL", nullptr);

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.uninstall_embedded_zygote_control_hooks = [&trace](int pid) {
        trace.calls.push_back(std::string("local-uninstall:") + std::to_string(pid));
        return true;
    };

    bool install_called = false;
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string*) {
            install_called = true;
            return true;
        },
        [&](int, const std::string&, std::string*) {
            uninstall_called = true;
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=default-helper-token"}),
                          "__embedded_agent__",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(!install_called);
    assert(uninstall_called == false);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "set-control:791",
                               "start:com.demo.target"}));
    assert(injector.active_spawn_owner_.zygote_control_transaction.helper_only_local_control);

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!install_called);
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "set-control:791",
                               "start:com.demo.target",
                               "local-uninstall:791",
                               "clear-control:791"}));
}

void TestSpawnDefaultEmbeddedAgentStaysOnStableLegacyRouteWhenZygoteControlIsEnabled() {
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    SetEnvValue("NOOK_STRICT_ZYGOTE_CONTROL", nullptr);

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("default_embedded_agent_stable_legacy");
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }
    SetEnvValue("NOOK_RUNTIME_DIR", dir.string().c_str());

    bool install_called = false;
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string*) {
            install_called = true;
            return true;
        },
        [&](int, const std::string&, std::string*) {
            uninstall_called = true;
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-spawn-token=default-embedded-stable-token"}),
                          "__embedded_agent__",
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert(!install_called);
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.spawn_token == "default-embedded-stable-token");
    assert(trace.so_path.empty());
    assert(!injector.active_spawn_owner_.zygote_control_transaction.helper_only_local_control);
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kSymbi);

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!install_called);
    assert(!uninstall_called);

    SetEnvValue("NOOK_RUNTIME_DIR", nullptr);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnOutcomeFallbackClassificationUsesOutcomeStateInsteadOfGlobalState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";
    outcome.zygote_control_state = ZygoteControlFailureState::kReadyWait;

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);

    assert(injector.ShouldAllowZygoteControlFallback(outcome, false));
}

void TestSpawnOutcomeFallbackClassificationPrefersFailedTransactionState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";
    outcome.failed_zygote_control_transaction.failure_state =
        ZygoteControlFailureState::kReadyWait;
    outcome.failed_zygote_control_transaction.lifecycle_state =
        ZygoteControlFailureState::kLaunchApp;

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);

    assert(injector.ShouldAllowZygoteControlFallback(outcome, false));
}

void TestSpawnOutcomeAbortFormatsOutcomeStateInsteadOfGlobalState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.final_status = NinjectorSpawnInjector::SpawnFinalStatus::kAbort;
    outcome.terminal_primary_backend =
        NinjectorSpawnInjector::SpawnTerminalBackend::kZygoteControl;
    outcome.terminal_secondary_backend =
        NinjectorSpawnInjector::SpawnTerminalBackend::kNone;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";
    outcome.zygote_control_state = ZygoteControlFailureState::kReadyWait;

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);

    std::string error_message;
    assert(!injector.FinalizeSpawnOutcome(MakeSpawnRequest("com.demo.target"),
                                          outcome,
                                          false,
                                          true,
                                          false,
                                          &error_message));
    assert(error_message.find(
               "zygote-control stage=spawn class=soft state=ready-wait detail=install zygote fork hook failed: rpc timeout") ==
           0);
}

void TestSpawnOutcomeAbortFormatsFailedTransactionStateInsteadOfDetailFallback() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.final_status = NinjectorSpawnInjector::SpawnFinalStatus::kAbort;
    outcome.terminal_primary_backend =
        NinjectorSpawnInjector::SpawnTerminalBackend::kZygoteControl;
    outcome.terminal_secondary_backend =
        NinjectorSpawnInjector::SpawnTerminalBackend::kNone;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";
    outcome.failed_zygote_control_transaction.failure_state =
        ZygoteControlFailureState::kReadyWait;

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);

    std::string error_message;
    assert(!injector.FinalizeSpawnOutcome(MakeSpawnRequest("com.demo.target"),
                                          outcome,
                                          false,
                                          true,
                                          false,
                                          &error_message));
    assert(error_message.find(
               "zygote-control stage=spawn class=soft state=ready-wait detail=install zygote fork hook failed: rpc timeout") ==
           0);
}

void TestResolveCurrentZygoteControlStatePrefersRecordedFailureState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kReadyWait);

    assert(injector.ResolveCurrentZygoteControlState("install zygote fork hook failed: rpc timeout") ==
           ZygoteControlFailureState::kLaunchApp);
}

void TestResolveCurrentZygoteControlStateFallsBackToLifecycleThenDetail() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    injector.ClearZygoteControlFailureState();
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kFinalizeClear);
    assert(injector.ResolveCurrentZygoteControlState("clear zygote spawn control failed") ==
           ZygoteControlFailureState::kFinalizeClear);

    injector.ClearZygoteControlLifecycleStage();
    assert(injector.ResolveCurrentZygoteControlState("install zygote fork hook failed: rpc timeout") ==
           ZygoteControlFailureState::kReadyWait);
}

void TestResolveTransactionZygoteControlStatePrefersTransactionState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.failure_state = ZygoteControlFailureState::kLaunchApp;
    transaction.lifecycle_state = ZygoteControlFailureState::kReadyWait;

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kArmControl);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kArmControl);

    assert(injector.ResolveTransactionZygoteControlState(
               &transaction,
               "install zygote fork hook failed: rpc timeout") ==
           ZygoteControlFailureState::kLaunchApp);
}

void TestResolveTransactionZygoteControlStatePrefersDetailBeforeRecorderFallback() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kArmControl);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kArmControl);

    assert(injector.ResolveTransactionZygoteControlState(
               &transaction,
               "install zygote fork hook failed: rpc timeout") ==
           ZygoteControlFailureState::kReadyWait);
}

void TestSuccessfulZygoteControlSpawnCommitsTransactionState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        {},
        {},
        []() {
            return std::vector<ProcessInfo>{
                {791, "zygote64"},
                {792, "usap64"},
            };
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=txn-state-token"}),
                          "/data/local/tmp/nook/libnook-agent.so",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier == "com.demo.target");
    assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token == "txn-state-token");
    assert(injector.active_spawn_owner_.zygote_control_transaction.failure_state ==
           ZygoteControlFailureState::kUnknown);
    assert(injector.active_spawn_owner_.zygote_control_transaction.lifecycle_state ==
           ZygoteControlFailureState::kLaunchApp);
    assert(!injector.active_spawn_owner_.zygote_control_transaction.targets.empty());
}

void TestFinalizeZygoteControlSpawnWritesStateBackToTransaction() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.clear_zygote_spawn_control = [&](int zygote_pid, const char* spawn_token, bool) {
        trace.calls.push_back(std::string("clear-control-fail:") + std::to_string(zygote_pid));
        trace.calls.push_back(std::string("clear-control-token:") + (spawn_token != nullptr ? spawn_token : ""));
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.identifier = "com.demo.target";
    transaction.spawn_token = "txn-finalize-token";
    transaction.lifecycle_state = ZygoteControlFailureState::kLaunchApp;
    transaction.targets.emplace_back(791, "zygote64");

    std::string error_message;
    assert(!injector.FinalizeZygoteControlSpawn(MakeSpawnRequest("com.demo.target"),
                                                &transaction,
                                                &error_message));
    assert(error_message == "clear zygote spawn control failed");
    assert(transaction.failure_state == ZygoteControlFailureState::kFinalizeClear);
    assert(transaction.lifecycle_state == ZygoteControlFailureState::kFinalizeClear);
    assert((trace.calls == std::vector<std::string>{
                               "clear-control-fail:791",
                               "clear-control-token:txn-finalize-token"}));
}

void TestFinalizeZygoteControlHelperOnlyLocalControlStopsOnUninstallFailure() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.uninstall_embedded_zygote_control_hooks = [&](int pid) {
        trace.calls.push_back(std::string("local-uninstall-fail:") + std::to_string(pid));
        return false;
    };
    ops.clear_zygote_spawn_control = [&](int zygote_pid, const char* spawn_token, bool) {
        trace.calls.push_back(std::string("clear-control-unexpected:") + std::to_string(zygote_pid));
        trace.calls.push_back(std::string("clear-control-token:") +
                              (spawn_token != nullptr ? spawn_token : ""));
        return true;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.identifier = "com.demo.target";
    transaction.spawn_token = "helper-finalize-token";
    transaction.helper_only_local_control = true;
    transaction.targets.emplace_back(791, "zygote64");

    std::string error_message;
    assert(!injector.FinalizeZygoteControlSpawn(MakeSpawnRequest("com.demo.target"),
                                                &transaction,
                                                &error_message));
    assert(error_message == "uninstall zygote helper hooks failed: embedded helper uninstall failed");
    assert(transaction.failure_state == ZygoteControlFailureState::kFinalizeClear);
    assert(transaction.lifecycle_state == ZygoteControlFailureState::kFinalizeClear);
    assert((trace.calls == std::vector<std::string>{
                               "local-uninstall-fail:791"}));
}

void TestFinalizeZygoteControlHelperOnlyLocalControlClearsRecorderOnSuccess() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.uninstall_embedded_zygote_control_hooks = [&](int pid) {
        trace.calls.push_back(std::string("local-uninstall:") + std::to_string(pid));
        return true;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.identifier = "com.demo.target";
    transaction.spawn_token = "helper-success-token";
    transaction.helper_only_local_control = true;
    transaction.targets.emplace_back(791, "zygote64");

    std::string error_message;
    assert(injector.FinalizeZygoteControlSpawn(MakeSpawnRequest("com.demo.target"),
                                               &transaction,
                                               &error_message));
    assert(error_message.empty());
    assert(transaction.failure_state == ZygoteControlFailureState::kUnknown);
    assert(transaction.lifecycle_state == ZygoteControlFailureState::kUnknown);
    assert(injector.ReadZygoteControlFailureState() == ZygoteControlFailureState::kUnknown);
    assert(injector.ReadZygoteControlLifecycleStage() == ZygoteControlFailureState::kUnknown);
    assert((trace.calls == std::vector<std::string>{
                               "local-uninstall:791",
                               "clear-control:791"}));
}

void TestSpawnOutcomeCanCarryFailedZygoteControlTransactionState() {
    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";
    outcome.zygote_control_state = ZygoteControlFailureState::kReadyWait;
    outcome.pending_commit.spawn_state.backend =
        NinjectorSpawnInjector::SpawnBackend::kZygoteControl;
    outcome.pending_commit.zygote_control_transaction.failure_state =
        ZygoteControlFailureState::kReadyWait;
    outcome.pending_commit.zygote_control_transaction.lifecycle_state =
        ZygoteControlFailureState::kInstallHook;

    assert(outcome.pending_commit.zygote_control_transaction.failure_state ==
           ZygoteControlFailureState::kReadyWait);
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kZygoteControl);
    assert(outcome.pending_commit.zygote_control_transaction.failure_state ==
           ZygoteControlFailureState::kReadyWait);
    assert(outcome.pending_commit.zygote_control_transaction.lifecycle_state ==
           ZygoteControlFailureState::kInstallHook);
}

void TestCommitPendingSpawnNormalizesZygoteControlShellOwnerState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::PendingSpawnCommit pending_commit;
    pending_commit.spawn_state.backend = NinjectorSpawnInjector::SpawnBackend::kZygoteControl;
    pending_commit.spawn_state.identifier = "com.demo.target";
    pending_commit.spawn_state.spawn_token = "request-token";
    pending_commit.spawn_state.ncore_path = "/data/local/tmp/nook/libncore.so";
    pending_commit.spawn_state.agent_path = "__embedded_agent__";
    pending_commit.spawn_state.materialized_ncore = true;
    pending_commit.spawn_state.materialized_agent = true;
    pending_commit.zygote_control_transaction.identifier = "com.demo.target";
    pending_commit.zygote_control_transaction.spawn_token = "txn-token";
    pending_commit.zygote_control_transaction.targets.emplace_back(791, "zygote64");

    injector.CommitPendingSpawn(pending_commit);

    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "request-token");
    assert(injector.active_spawn_owner_.spawn_state.ncore_path.empty());
    assert(injector.active_spawn_owner_.spawn_state.agent_path.empty());
    assert(!injector.active_spawn_owner_.spawn_state.materialized_ncore);
    assert(!injector.active_spawn_owner_.spawn_state.materialized_agent);
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier == "com.demo.target");
    assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token == "txn-token");
}

void TestCommitPendingSpawnSeparatesAuthoritativeAndCompatibilityShellState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::PendingSpawnCommit zygote_pending_commit;
    zygote_pending_commit.spawn_state.spawn_token = "request-token";
    zygote_pending_commit.zygote_control_transaction.identifier = "com.demo.target";
    zygote_pending_commit.zygote_control_transaction.spawn_token = "txn-token";

    injector.CommitPendingSpawn(zygote_pending_commit);

    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "request-token");
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier == "com.demo.target");

    NinjectorSpawnInjector::PendingSpawnCommit legacy_pending_commit;
    legacy_pending_commit.spawn_state.spawn_token = "legacy-token";
    legacy_pending_commit.spawn_state.ncore_path = "/data/local/tmp/nook/libncore.so";
    legacy_pending_commit.spawn_state.agent_path = "__embedded_agent__";
    legacy_pending_commit.spawn_state.materialized_ncore = true;
    legacy_pending_commit.spawn_state.materialized_agent = true;
    legacy_pending_commit.shell_owner_state.backend =
        NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
    legacy_pending_commit.shell_owner_state.identifier = "com.demo.target";
    legacy_pending_commit.shell_owner_state.spawn_token = "legacy-token";

    injector.CommitPendingSpawn(legacy_pending_commit);
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kLegacyNcore);
    assert(injector.active_spawn_owner_.shell_owner_state.identifier == "com.demo.target");
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "legacy-token");
    assert(injector.active_spawn_owner_.spawn_state.ncore_path.empty());
    assert(injector.active_spawn_owner_.spawn_state.agent_path.empty());
    assert(!injector.active_spawn_owner_.spawn_state.materialized_ncore);
    assert(!injector.active_spawn_owner_.spawn_state.materialized_agent);
}

void TestCommitPendingSpawnKeepsSymbiOwnerOutOfShellCompatibilityState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::PendingSpawnCommit pending_commit;
    pending_commit.spawn_state.identifier = "com.demo.target";
    pending_commit.spawn_state.spawn_token = "symbi-owner-token";
    pending_commit.spawn_state.agent_path = "__embedded_agent__";
    pending_commit.spawn_state.backend =
        NinjectorSpawnInjector::SpawnBackend::kSymbi;

    injector.CommitPendingSpawn(pending_commit);

    assert(injector.active_spawn_owner_.spawn_state.identifier == "com.demo.target");
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "symbi-owner-token");
    assert(injector.active_spawn_owner_.spawn_state.agent_path == "__embedded_agent__");
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kSymbi);
    assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.spawn_token.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestCommitPendingSpawnDoesNotPromoteCompatibilitySpawnStateIntoShellOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::PendingSpawnCommit pending_commit;
    pending_commit.spawn_state.identifier = "compat-only.target";
    pending_commit.spawn_state.spawn_token = "compat-token";
    pending_commit.spawn_state.backend = NinjectorSpawnInjector::SpawnBackend::kNone;

    injector.CommitPendingSpawn(pending_commit);
    assert(injector.active_spawn_owner_.spawn_state.identifier == "compat-only.target");
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "compat-token");
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.spawn_token.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestCommitPendingSpawnPreservesExplicitShellOwnerState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::PendingSpawnCommit pending_commit;
    pending_commit.spawn_state.identifier = "compat-only.target";
    pending_commit.spawn_state.spawn_token = "compat-token";
    pending_commit.spawn_state.backend = NinjectorSpawnInjector::SpawnBackend::kNone;
    pending_commit.shell_owner_state.identifier = "com.demo.target";
    pending_commit.shell_owner_state.spawn_token = "owner-token";
    pending_commit.shell_owner_state.backend =
        NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;

    injector.CommitPendingSpawn(pending_commit);

    assert(injector.active_spawn_owner_.spawn_state.identifier == "compat-only.target");
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "compat-token");
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.identifier == "com.demo.target");
    assert(injector.active_spawn_owner_.shell_owner_state.spawn_token == "owner-token");
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kLegacyNcore);
}

void TestFailedZygoteControlTransactionSnapshotTracksReadyWaitFailure() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("failed_txn_snapshot_ready_wait");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        {});

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-strict-zygote-control",
                                             "--nook-spawn-token=failed-txn-token"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message.find(
               "zygote-control stage=spawn class=soft state=install-hook detail=install zygote fork hook failed: rpc timeout") ==
           0);
    assert(error_message.find("fallback failed:") == std::string::npos);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnStrictZygoteControlInstallFailureDoesNotUseLegacyClearSideChannel() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.clear_zygote_spawn_control = [&](int zygote_pid, const char* spawn_token, bool) {
        trace.calls.push_back(std::string("clear-control-fail:") + std::to_string(zygote_pid));
        trace.spawn_token = spawn_token != nullptr ? spawn_token : "";
        return false;
    };
    ops.get_inject_error = []() {
        return std::string("rollback_clear_failed");
    };

    const fs::path dir = MakeTempDir("strict_install_fail_with_rollback_fail");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                *error_message = "ready wait timed out";
            }
            return false;
        },
        {});

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-strict-zygote-control",
                                             "--nook-spawn-token=rollback-fail-token"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message ==
           "zygote-control stage=spawn class=soft state=ready-wait detail=install zygote fork hook failed: ready wait timed out");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791"}));
    RemoveAllIgnoringMissing(dir);
}

void TestSnapshotCurrentZygoteControlTransactionStateCopiesRecorderAndTargets() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kReadyWait);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kInstallHook);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    std::vector<std::pair<int, std::string>> targets{
        {791, "zygote64"},
        {792, "usap64"},
    };
    injector.SnapshotCurrentZygoteControlTransactionState(&transaction, &targets);

    assert(transaction.failure_state == ZygoteControlFailureState::kReadyWait);
    assert(transaction.lifecycle_state == ZygoteControlFailureState::kInstallHook);
    assert(transaction.targets == targets);
}

void TestFailZygoteControlSpawnSnapshotsTransactionAndError() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kTargetsArmed);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    std::vector<std::pair<int, std::string>> targets{
        {791, "zygote64"},
    };
    std::string error_message;
    assert(!injector.FailZygoteControlSpawn(&transaction,
                                            &targets,
                                            "start_target_app failed",
                                            &error_message));
    assert(error_message == "start_target_app failed");
    assert(transaction.failure_state == ZygoteControlFailureState::kLaunchApp);
    assert(transaction.lifecycle_state == ZygoteControlFailureState::kTargetsArmed);
    assert(transaction.targets == targets);
}

void TestFailZygoteControlSpawnInfersStateWithoutRecorder() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    std::string error_message;
    assert(!injector.FailZygoteControlSpawn(&transaction,
                                            nullptr,
                                            "install zygote fork hook failed: rpc timeout",
                                            &error_message));
    assert(error_message == "install zygote fork hook failed: rpc timeout");
    assert(transaction.failure_state == ZygoteControlFailureState::kReadyWait);
    assert(transaction.lifecycle_state == ZygoteControlFailureState::kReadyWait);
}

void TestFailZygoteControlSpawnClearsGlobalRecorderAfterSnapshot() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kTargetsArmed);

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    std::string error_message;
    assert(!injector.FailZygoteControlSpawn(&transaction,
                                            nullptr,
                                            "start_target_app failed",
                                            &error_message));
    assert(error_message == "start_target_app failed");
    assert(transaction.failure_state == ZygoteControlFailureState::kLaunchApp);
    assert(transaction.lifecycle_state == ZygoteControlFailureState::kTargetsArmed);
    assert(injector.ReadZygoteControlFailureState() == ZygoteControlFailureState::kUnknown);
    assert(injector.ReadZygoteControlLifecycleStage() == ZygoteControlFailureState::kUnknown);
    assert(injector.ResolveCurrentZygoteControlState("install zygote fork hook failed: rpc timeout") ==
           ZygoteControlFailureState::kReadyWait);
}

void TestTrySpawnViaZygoteControlReturnsStructuredFailureResult() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.start_target_app = [&trace](const char* package_name) {
        trace.calls.push_back(std::string("start-fail:") + package_name);
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::ZygoteControlAttemptResult result =
        injector.TrySpawnViaZygoteControl(MakeSpawnRequest("com.demo.target"), "/data/local/tmp/nook/libnook-agent.so");

    assert(!result.success);
    assert(result.pid == 0);
    assert(result.error_message == "start_target_app failed");
    assert(result.owned_transaction.identifier == "com.demo.target");
    assert(result.owned_transaction.failure_state == ZygoteControlFailureState::kLaunchApp);
    assert(result.owned_transaction.lifecycle_state == ZygoteControlFailureState::kLaunchApp);
    assert(result.owned_transaction.targets == (std::vector<std::pair<int, std::string>>{
               {791, "zygote64"},
           }));
}

void TestSpawnZygoteControlAtomicAttachInjectFailureFallsBackByDefault() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.inject_zygote_so_by_pid = [&](int zygote_pid, const char*) {
        trace.calls.push_back(std::string("zygote-inject-fail:") + std::to_string(zygote_pid));
        return false;
    };
    ops.get_inject_error = []() {
        return std::string("attach_process_failed:atomic_inject");
    };

    const fs::path dir = MakeTempDir("zygote_atomic_attach_inject_fallback");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-spawn-token=atomic-inject-fail-token"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-fail:791",
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:atomic-inject-fail-token",
                               "start:com.demo.target"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestApplyFailedZygoteControlOutcomeSeedsStateAndTransaction() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    transaction.lifecycle_state = ZygoteControlFailureState::kInstallHook;

    const bool allow_fallback =
        injector.ApplyFailedZygoteControlOutcome(&outcome, transaction, true);

    assert(!allow_fallback);
    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kForbidden);
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kAbort);
    assert(outcome.zygote_control_state == ZygoteControlFailureState::kReadyWait);
    assert(outcome.failed_zygote_control_transaction.failure_state ==
           ZygoteControlFailureState::kReadyWait);
    assert(outcome.failed_zygote_control_transaction.failure_state ==
           ZygoteControlFailureState::kReadyWait);
}

void TestApplyFailedZygoteControlOutcomeAllowsFallbackWhenStateIsSoft() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    transaction.lifecycle_state = ZygoteControlFailureState::kInstallHook;

    const bool allow_fallback =
        injector.ApplyFailedZygoteControlOutcome(&outcome, transaction, false);

    assert(allow_fallback);
    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kAllowed);
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kUnknown);
    assert(outcome.zygote_control_state == ZygoteControlFailureState::kReadyWait);
    assert(outcome.failed_zygote_control_transaction.lifecycle_state ==
           ZygoteControlFailureState::kInstallHook);
    assert(outcome.failed_zygote_control_transaction.lifecycle_state ==
           ZygoteControlFailureState::kInstallHook);
}

void TestApplyFailedZygoteControlAttemptResultSeedsOutcome() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::ZygoteControlAttemptResult attempt;
    attempt.error_message = "install zygote fork hook failed: rpc timeout";
    attempt.owned_transaction.identifier = "com.demo.target";
    attempt.owned_transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    attempt.owned_transaction.lifecycle_state = ZygoteControlFailureState::kInstallHook;

    const bool allow_fallback =
        injector.ApplyFailedZygoteControlAttemptResult(&outcome, attempt, false);

    assert(allow_fallback);
    assert(outcome.zygote_control_error == "install zygote fork hook failed: rpc timeout");
    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kAllowed);
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kUnknown);
    assert(outcome.failed_zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.failed_zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.zygote_control_state == ZygoteControlFailureState::kReadyWait);
}

void TestApplySuccessfulZygoteControlAttemptResultSeedsCommit() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::ZygoteControlAttemptResult attempt;
    attempt.success = true;
    attempt.pid = 17001;
    attempt.owned_transaction.identifier = "com.demo.target";
    attempt.owned_transaction.spawn_token = "success-token";
    attempt.owned_transaction.failure_state = ZygoteControlFailureState::kUnknown;
    attempt.owned_transaction.lifecycle_state = ZygoteControlFailureState::kLaunchApp;
    attempt.owned_transaction.targets.push_back({791, "zygote64"});

    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "request-token";
    owned_state.ncore_path = "/data/local/tmp/nook/libncore.so";
    owned_state.agent_path = "__embedded_agent__";
    owned_state.materialized_ncore = true;
    owned_state.materialized_agent = true;

    int pid = 0;
    std::string error_message;
    assert(injector.ApplySuccessfulZygoteControlAttemptResult(&outcome,
                                                              attempt,
                                                              std::move(owned_state),
                                                              &pid,
                                                              &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kSuccess);
    assert(outcome.pending_commit.zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(outcome.pending_commit.spawn_state.identifier.empty());
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(outcome.pending_commit.spawn_state.spawn_token == "request-token");
    assert(outcome.pending_commit.spawn_state.ncore_path.empty());
    assert(outcome.pending_commit.spawn_state.agent_path.empty());
    assert(!outcome.pending_commit.spawn_state.materialized_ncore);
    assert(!outcome.pending_commit.spawn_state.materialized_agent);
    assert(outcome.pending_commit.zygote_control_transaction.spawn_token == "success-token");
    assert(outcome.pending_commit.zygote_control_transaction.targets ==
           (std::vector<std::pair<int, std::string>>{{791, "zygote64"}}));
}

void TestApplyZygoteControlRouteSuccessCommitsOutcome() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::ZygoteControlAttemptResult attempt;
    attempt.success = true;
    attempt.pid = 18001;
    attempt.owned_transaction.identifier = "com.demo.target";
    attempt.owned_transaction.spawn_token = "route-success-token";

    int pid = 0;
    std::string error_message;
    assert(injector.ApplyZygoteControlRouteAttempt(MakeSpawnRequest("com.demo.target",
                                                                    {"--nook-spawn-token=request-token"}),
                                                   attempt,
                                                   false,
                                                   &outcome,
                                                   &pid,
                                                   &error_message));
    assert(pid == 18001);
    assert(error_message.empty());
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kSuccess);
    assert(outcome.pending_commit.zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(outcome.pending_commit.spawn_state.identifier.empty());
    assert(outcome.pending_commit.spawn_state.spawn_token == "request-token");
}

void TestApplyZygoteControlRouteAbortsStrictFailure() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::ZygoteControlAttemptResult attempt;
    attempt.success = false;
    attempt.error_message = "install zygote fork hook failed: rpc timeout";
    attempt.owned_transaction.identifier = "com.demo.target";
    attempt.owned_transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    attempt.owned_transaction.lifecycle_state = ZygoteControlFailureState::kInstallHook;

    int pid = 0;
    std::string error_message;
    assert(!injector.ApplyZygoteControlRouteAttempt(MakeSpawnRequest("com.demo.target"),
                                                    attempt,
                                                    true,
                                                    &outcome,
                                                    &pid,
                                                    &error_message));
    assert(pid == 0);
    assert(error_message ==
           "zygote-control stage=spawn class=soft state=ready-wait detail=install zygote fork hook failed: rpc timeout");
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kAbort);
    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kForbidden);
}

void TestApplyZygoteControlRouteAbortKeepsResidualTransactionScopedToAttempt() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::ZygoteControlAttemptResult attempt;
    attempt.success = false;
    attempt.error_message = "start_target_app failed; rollback failed: clear zygote spawn control failed";
    attempt.owned_transaction.identifier = "com.demo.target";
    attempt.owned_transaction.spawn_token = "rollback-token";
    attempt.owned_transaction.failure_state = ZygoteControlFailureState::kLaunchApp;
    attempt.owned_transaction.lifecycle_state = ZygoteControlFailureState::kLaunchApp;
    attempt.owned_transaction.targets.push_back({791, "zygote64"});

    int pid = 0;
    std::string error_message;
    assert(!injector.ApplyZygoteControlRouteAttempt(MakeSpawnRequest("com.demo.target"),
                                                    attempt,
                                                    true,
                                                    &outcome,
                                                    &pid,
                                                    &error_message));

    assert(pid == 0);
    assert(error_message ==
           "zygote-control stage=spawn class=hard state=launch-app detail=start_target_app failed; rollback failed: clear zygote spawn control failed");
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kAbort);
    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kForbidden);
    assert(outcome.failed_zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.failed_zygote_control_transaction.spawn_token == "rollback-token");
    assert(outcome.failed_zygote_control_transaction.targets ==
           (std::vector<std::pair<int, std::string>>{{791, "zygote64"}}));
}

void TestApplySymbiRouteSuccessCommitsOutcome() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "symbi-success-token";

    int pid = 18002;
    std::string error_message;
    assert(injector.ApplySymbiRouteResult(MakeSpawnRequest("com.demo.target",
                                                           {"--nook-spawn-backend=symbi"}),
                                          true,
                                          true,
                                          std::string(),
                                          std::move(owned_state),
                                          &outcome,
                                          &pid,
                                          &error_message));
    assert(pid == 18002);
    assert(error_message.empty());
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kSuccess);
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kSymbi);
    assert(outcome.pending_commit.spawn_state.identifier == "com.demo.target");
    assert(outcome.pending_commit.spawn_state.spawn_token == "symbi-success-token");
    assert(outcome.pending_commit.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestApplySymbiRouteFailureAllowsFallback() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::SpawnOwnedState owned_state;

    int pid = 0;
    std::string error_message;
    assert(injector.ApplySymbiRouteResult(MakeSpawnRequest("com.demo.target"),
                                          false,
                                          false,
                                          "symbi_timeout",
                                          std::move(owned_state),
                                          &outcome,
                                          &pid,
                                          &error_message));
    assert(pid == 0);
    assert(error_message.empty());
    assert(outcome.symbi_error == "symbi_timeout");
}

void TestApplyLegacyRouteSuccessCommitsOutcome() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "legacy-success-token";
    owned_state.ncore_path = "/data/local/tmp/nook/libncore.so";

    int pid = 19001;
    std::string error_message;
    assert(injector.ApplyLegacyRouteResult(MakeSpawnRequest("com.demo.target"),
                                           true,
                                           std::move(owned_state),
                                           std::string(),
                                           &outcome,
                                           &pid,
                                           &error_message));
    assert(pid == 19001);
    assert(error_message.empty());
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kSuccess);
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(outcome.pending_commit.spawn_state.identifier.empty());
    assert(outcome.pending_commit.spawn_state.spawn_token == "legacy-success-token");
    assert(outcome.pending_commit.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kLegacyNcore);
    assert(outcome.pending_commit.shell_owner_state.identifier == "com.demo.target");
}

void TestApplyLegacyRouteFailureCapturesErrorWhenProbeOnly() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    int pid = 0;
    std::string error_message;
    assert(injector.ApplyLegacyRouteResult(MakeSpawnRequest("com.demo.target"),
                                           false,
                                           NinjectorSpawnInjector::SpawnOwnedState{},
                                           "legacy_prepare_failed",
                                           &outcome,
                                           &pid,
                                           &error_message));
    assert(pid == 0);
    assert(error_message.empty());
    assert(outcome.legacy_error == "legacy_prepare_failed");
}

void TestApplySpawnRoutingSucceedsOnZygoteControlSuccess() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.policy.primary_route =
        NinjectorSpawnInjector::SpawnPrimaryRoute::kStrictZygoteControl;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = true;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;

    int pid = 0;
    std::string error_message;
    assert(injector.ApplySpawnRoutingAttempts(MakeSpawnRequest("com.demo.target",
                                                               {"--nook-spawn-token=zygote-success-token"}),
                                              "/data/local/tmp/nook/libnook-agent.so",
                                              &state,
                                              &pid,
                                              &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteCommitted);
    assert(state.ownership_state ==
           NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(state.routing_state ==
           NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromZygoteControl);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kCommitted);
    assert(state.current_route_step ==
           NinjectorSpawnInjector::SpawnRouteStep::kZygoteControl);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kAfterZygoteControl);
    assert(state.routing_windows.zygote_control ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kEntered);
    assert(state.routing_windows.symbi ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kNotConsidered);
    assert(state.routing_windows.legacy ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kNotConsidered);
    assert(state.outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kSuccess);
    assert(state.outcome.pending_commit.zygote_control_transaction.identifier ==
           "com.demo.target");
    assert(state.outcome.pending_commit.zygote_control_transaction.spawn_token ==
           "zygote-success-token");
}

void TestApplySpawnRoutingAttemptsOnDefaultStablePathSkipsZygoteControl() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.policy.primary_route =
        NinjectorSpawnInjector::SpawnPrimaryRoute::kLegacyDefault;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = false;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;

    int pid = 0;
    std::string error_message;
    assert(injector.ApplySpawnRoutingAttempts(MakeSpawnRequest("com.demo.target",
                                                               {"--nook-spawn-token=zygote-fail-token"}),
                                              "/data/local/tmp/nook/libnook-agent.so",
                                              &state,
                                              &pid,
                                              &error_message));
    assert(pid == 0);
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteDeferred);
    assert(state.ownership_state == NinjectorSpawnInjector::SpawnOwnershipState::kNone);
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kDeferredToTerminal);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped);
    assert(state.current_route_step ==
           NinjectorSpawnInjector::SpawnRouteStep::kLegacy);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kAfterLegacy);
    assert(state.routing_windows.zygote_control ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kSkippedByPolicy);
    assert(state.routing_windows.symbi ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kSkippedByPolicy);
    assert(state.routing_windows.legacy ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kEntered);
    assert(state.outcome.zygote_control_error.empty());
    assert(!state.outcome.legacy_error.empty());
}

void TestApplyZygoteControlRoutingCommitsSuccessfulRoute() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = false;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;

    int pid = 0;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.ApplyZygoteControlRouting(MakeSpawnRequest("com.demo.target",
                                                               {"--nook-spawn-token=zygote-helper-success"}),
                                              "/data/local/tmp/nook/libnook-agent.so",
                                              &state,
                                              &pid,
                                              &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteCommitted);
    assert(state.routing_state ==
           NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromZygoteControl);
    assert(state.ownership_state ==
           NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
}

void TestApplyZygoteControlRoutingDefersSoftFailureToFallback() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = false;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;

    int pid = 0;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.ApplyZygoteControlRouting(MakeSpawnRequest("com.demo.target",
                                                               {"--nook-spawn-token=zygote-helper-fail"}),
                                              "/data/local/tmp/nook/libnook-agent.so",
                                              &state,
                                              &pid,
                                              &error_message));
    assert(pid == 0);
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouting);
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kDeferredToFallback);
    assert(state.outcome.zygote_control_error ==
           "install zygote fork hook failed: rpc timeout");
}

void TestApplyZygoteControlRoutingStrictSoftFailureAbortsWithoutFallback() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                *error_message = "rpc timeout";
            }
            return false;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = true;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;

    int pid = 0;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(!injector.ApplyZygoteControlRouting(MakeSpawnRequest("com.demo.target",
                                                                {"--nook-strict-zygote-control",
                                                                 "--nook-spawn-token=zygote-helper-strict-fail"}),
                                               "/data/local/tmp/nook/libnook-agent.so",
                                               &state,
                                               &pid,
                                               &error_message));
    assert(pid == 0);
    assert(error_message ==
           "zygote-control stage=spawn class=soft state=install-hook detail=install zygote fork hook failed: rpc timeout");
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouting);
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kAborted);
    assert(state.outcome.zygote_control_error ==
           "install zygote fork hook failed: rpc timeout");
}

void TestApplyZygoteControlRoutingStrictHelperLocalControlCommitsRoute() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string*) {
            assert(false && "strict helper-only local control should not invoke rpc install");
            return false;
        },
        [&](int, const std::string&, std::string*) {
            assert(false && "strict helper-only local control should not invoke rpc uninstall");
            return false;
        });

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = true;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;

    int pid = 0;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.ApplyZygoteControlRouting(MakeSpawnRequest("com.demo.target",
                                                               {"--nook-strict-zygote-control",
                                                                "--nook-spawn-token=zygote-helper-strict-success"}),
                                              "__embedded_agent__",
                                              &state,
                                              &pid,
                                              &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteCommitted);
    assert(state.routing_state ==
           NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromZygoteControl);
    assert(state.ownership_state ==
           NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kCommitted);
    assert(injector.active_spawn_owner_.zygote_control_transaction.helper_only_local_control);
}

void TestApplyTerminalSpawnOutcomeClassifiesThenFinalizes() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.phase = NinjectorSpawnInjector::SpawnExecutionPhase::kRouteDeferred;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = false;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;
    state.outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";
    state.outcome.failed_zygote_control_transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    state.outcome.legacy_error = "legacy_prepare_failed";

    std::string error_message;
    assert(!injector.ApplyTerminalSpawnOutcome(MakeSpawnRequest("com.demo.target"),
                                               &state,
                                               &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kTerminalFinalized);
    assert(state.outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kFallbackFailed);
    assert(error_message.find(
               "zygote-control stage=spawn class=soft state=ready-wait detail=install zygote fork hook failed: rpc timeout") ==
           0);
    assert(error_message.find("fallback failed: legacy_prepare_failed") != std::string::npos);
}

void TestCompleteSpawnAfterRoutingCommitsCompletedRoutePath() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.phase = NinjectorSpawnInjector::SpawnExecutionPhase::kRouteCommitted;
    state.outcome.final_status = NinjectorSpawnInjector::SpawnFinalStatus::kSuccess;

    std::string error_message;
    assert(injector.CompleteSpawnAfterRouting(MakeSpawnRequest("com.demo.target"),
                                              "owner-token",
                                              &state,
                                              &error_message));
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kCompleted);
    assert(state.phase_reason ==
           NinjectorSpawnInjector::SpawnExecutionReason::kCompletedAfterCommittedRoute);
}

void TestCompleteSpawnAfterRoutingFinishesDeferredTerminalPath() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
    }

    NinjectorSpawnInjector::SpawnExecutionState state;
    state.phase = NinjectorSpawnInjector::SpawnExecutionPhase::kRouteDeferred;
    state.policy.explicit_symbi_requested = false;
    state.policy.should_try_symbi_first = false;
    state.policy.strict_zygote_control = false;
    state.policy.allow_symbi_backend = false;
    state.policy.allow_legacy_backend_fallback = true;
    state.outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";
    state.outcome.failed_zygote_control_transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    state.outcome.legacy_error = "legacy_prepare_failed";

    std::string error_message;
    assert(!injector.CompleteSpawnAfterRouting(MakeSpawnRequest("com.demo.target"),
                                               "owner-token",
                                               &state,
                                               &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kCompleted);
    assert(state.phase_reason ==
           NinjectorSpawnInjector::SpawnExecutionReason::kCompletedAfterTerminalOutcome);
    assert(error_message.find(
               "zygote-control stage=spawn class=soft state=ready-wait detail=install zygote fork hook failed: rpc timeout") ==
           0);
    assert(error_message.find("fallback failed: legacy_prepare_failed") != std::string::npos);
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestAdmitSpawnRequestRejectsMatchingActiveOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.spawn_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
    }

    std::string error_message;
    assert(!injector.AdmitSpawnRequest(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "spawn already active for identifier");
}

void TestAdmitSpawnRequestRejectsForeignActiveOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "other.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
    }

    std::string error_message;
    assert(!injector.AdmitSpawnRequest(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "spawn already active");
}

void TestAdmitSpawnRequestIgnoresCompatibilityShellWithoutBackend() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.spawn_state.spawn_token = "compat-token";
        injector.active_spawn_owner_.spawn_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kNone;
    }

    std::string error_message;
    assert(injector.AdmitSpawnRequest(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
}

void TestAdmitSpawnRequestRecognizesSymbiOwnerFromSpawnState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.spawn_state.spawn_token = "symbi-owner-token";
        injector.active_spawn_owner_.spawn_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kSymbi;
    }

    std::string error_message;
    assert(!injector.AdmitSpawnRequest(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "spawn already active for identifier");
}

void TestAdmitSpawnRequestRejectsMatchingResidualZygoteTransaction() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    std::string error_message;
    assert(!injector.AdmitSpawnRequest(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "spawn already active for identifier");
}

void TestAdmitSpawnRequestRejectsForeignResidualZygoteTransaction() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "other.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    std::string error_message;
    assert(!injector.AdmitSpawnRequest(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "spawn already active");
}

void TestBuildSpawnExecutionPolicyDefaultStablePath() {
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    const auto policy = injector.BuildSpawnExecutionPolicy(MakeSpawnRequest("com.demo.target"));
    assert(policy.primary_route ==
           NinjectorSpawnInjector::SpawnPrimaryRoute::kSymbiDefault);
    assert(!policy.explicit_symbi_requested);
    assert(policy.should_try_symbi_first);
    assert(!policy.strict_zygote_control);
    assert(policy.allow_symbi_backend);
    assert(policy.allow_legacy_backend_fallback);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
}

void TestBuildSpawnExecutionPolicyLegacyPreferredWhenSymbiPreferenceDisabled() {
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    const auto policy = injector.BuildSpawnExecutionPolicy(MakeSpawnRequest("com.demo.target"));
    assert(policy.primary_route ==
           NinjectorSpawnInjector::SpawnPrimaryRoute::kLegacyDefault);
    assert(!policy.explicit_symbi_requested);
    assert(!policy.should_try_symbi_first);
    assert(!policy.strict_zygote_control);
    assert(!policy.allow_symbi_backend);
    assert(policy.allow_legacy_backend_fallback);
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
}

void TestBuildSpawnExecutionPolicyExplicitSymbiRequest() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    const auto policy = injector.BuildSpawnExecutionPolicy(
        MakeSpawnRequest("com.demo.target", {"--nook-spawn-backend=symbi"}));
    assert(policy.primary_route ==
           NinjectorSpawnInjector::SpawnPrimaryRoute::kExplicitSymbi);
    assert(policy.explicit_symbi_requested);
    assert(policy.should_try_symbi_first);
    assert(policy.allow_symbi_backend);
    assert(!policy.allow_legacy_backend_fallback);
}

void TestBuildSpawnExecutionPolicyStrictZygoteControlOverridesExplicitSymbiRequest() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    SetEnvValue("NOOK_ALLOW_SYMBI_FALLBACK", nullptr);

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    const auto policy = injector.BuildSpawnExecutionPolicy(
        MakeSpawnRequest("com.demo.target",
                         {"--nook-strict-zygote-control", "--nook-spawn-backend=symbi"}));
    assert(policy.primary_route ==
           NinjectorSpawnInjector::SpawnPrimaryRoute::kStrictZygoteControl);
    assert(!policy.should_try_symbi_first);
    assert(policy.strict_zygote_control);
    assert(!policy.allow_symbi_backend);
    assert(!policy.allow_legacy_backend_fallback);

    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    SetEnvValue("NOOK_ALLOW_SYMBI_FALLBACK", nullptr);
}

void TestBuildSpawnExecutionStateCarriesPolicyAndAttempt() {
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    const auto state =
        injector.BuildSpawnExecutionState(MakeSpawnRequest("com.demo.target"), "/data/local/tmp/nook/libnook-agent.so");
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kInit);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kInitialized);
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kNotStarted);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kNotStarted);
    assert(state.current_route_step == NinjectorSpawnInjector::SpawnRouteStep::kNone);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kNotStarted);
    assert(state.routing_windows.zygote_control ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kNotConsidered);
    assert(state.routing_windows.symbi ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kNotConsidered);
    assert(state.routing_windows.legacy ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kNotConsidered);
    assert(state.policy.primary_route ==
           NinjectorSpawnInjector::SpawnPrimaryRoute::kSymbiDefault);
    assert(!state.policy.explicit_symbi_requested);
    assert(state.policy.should_try_symbi_first);
    assert(!state.policy.strict_zygote_control);
    assert(state.policy.allow_symbi_backend);
    assert(!state.zygote_attempt.success);
    assert(state.outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kUnknown);
    assert(trace.calls.empty());
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
}

void TestBuildSpawnExecutionStateDoesNotTriggerZygoteControlAttempt() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("build_state_no_zygote_attempt");
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    bool install_called = false;
    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            install_called = true;
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    const auto state =
        injector.BuildSpawnExecutionState(MakeSpawnRequest("com.demo.target",
                                                           {"--nook-spawn-token=build-only-token"}),
                                          "/data/local/tmp/nook/libnook-agent.so");

    assert(!state.zygote_attempt.success);
    assert(state.zygote_attempt.pid == 0);
    assert(state.zygote_attempt.error_message.empty());
    assert(state.zygote_attempt.owned_transaction.identifier.empty());
    assert(!install_called);
    assert(trace.calls.empty());

    RemoveAllIgnoringMissing(dir);
}

void TestBeginSpawnRoutingSeedsRunningRoutingState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouting);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kBeginRouting);
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting);
    assert(state.current_route_step == NinjectorSpawnInjector::SpawnRouteStep::kNone);
}

void TestEnterZygoteControlRouteSeedsEnteredRouteContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.EnterZygoteControlRoute(&state, &error_message));
    assert(error_message.empty());
    assert(state.current_route_step ==
           NinjectorSpawnInjector::SpawnRouteStep::kZygoteControl);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered);
    assert(state.routing_windows.zygote_control ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kEntered);
}

void TestSkipZygoteControlRouteSeedsSkippedRouteContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.SkipZygoteControlRoute(&state, &error_message));
    assert(error_message.empty());
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped);
    assert(state.routing_windows.zygote_control ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kSkippedByPolicy);
}

void TestAbortZygoteControlRouteKeepsRunningEnteredContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.EnterZygoteControlRoute(&state, &error_message));
    assert(injector.AbortZygoteControlRoute(&state, &error_message));
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouting);
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kAborted);
    assert(state.current_route_step ==
           NinjectorSpawnInjector::SpawnRouteStep::kZygoteControl);
}

void TestCommitZygoteControlRouteAdvancesCommittedRouteContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.EnterZygoteControlRoute(&state, &error_message));
    assert(injector.CommitZygoteControlRoute(&state, &error_message));
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteCommitted);
    assert(state.phase_reason ==
           NinjectorSpawnInjector::SpawnExecutionReason::kRouteCommittedFromZygoteControl);
    assert(state.routing_state ==
           NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromZygoteControl);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kAfterZygoteControl);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kCommitted);
}

void TestDeferZygoteControlRouteToFallbackKeepsRunningUntilAdvance() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.EnterZygoteControlRoute(&state, &error_message));
    assert(injector.DeferZygoteControlRouteToFallback(&state, &error_message));
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouting);
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kDeferredToFallback);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting);
}

void TestAdvancePastZygoteControlRouteMovesRoutingProgressForward() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.SkipZygoteControlRoute(&state, &error_message));
    assert(injector.AdvancePastZygoteControlRoute(&state, &error_message));
    assert(error_message.empty());
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kAfterZygoteControl);
}

void TestCommitNonZygoteControlRouteCommitsSymbiContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterSymbi,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
            .update_symbi_window = true,
            .symbi_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(injector.CommitNonZygoteControlRoute(
        &state,
        NinjectorSpawnInjector::SpawnBackend::kSymbi,
        &error_message));
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteCommitted);
    assert(state.phase_reason ==
           NinjectorSpawnInjector::SpawnExecutionReason::kRouteCommittedFromSymbi);
    assert(state.ownership_state ==
           NinjectorSpawnInjector::SpawnOwnershipState::kSymbiOwned);
    assert(state.routing_state ==
           NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromSymbi);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kAfterSymbi);
}

void TestCommitNonZygoteControlRouteCommitsLegacyContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.BeginSpawnRouting(&state, &error_message));
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterLegacy,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kLegacy,
            .update_legacy_window = true,
            .legacy_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(injector.CommitNonZygoteControlRoute(
        &state,
        NinjectorSpawnInjector::SpawnBackend::kLegacyNcore,
        &error_message));
    assert(error_message.empty());
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteCommitted);
    assert(state.phase_reason ==
           NinjectorSpawnInjector::SpawnExecutionReason::kRouteCommittedFromLegacy);
    assert(state.ownership_state ==
           NinjectorSpawnInjector::SpawnOwnershipState::kLegacyOwned);
    assert(state.routing_state ==
           NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromLegacy);
    assert(state.routing_progress ==
           NinjectorSpawnInjector::SpawnRoutingProgress::kAfterLegacy);
}

void TestTransitionSpawnOwnershipStateAllowsSingleCommitOwnership() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.TransitionSpawnOwnershipState(
        &state,
        NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned,
        &error_message));
    assert(error_message.empty());
    assert(state.ownership_state ==
           NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
}

void TestTransitionSpawnOwnershipStateRejectsOwnerRewrite() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.TransitionSpawnOwnershipState(
        &state,
        NinjectorSpawnInjector::SpawnOwnershipState::kLegacyOwned,
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.TransitionSpawnOwnershipState(
        &state,
        NinjectorSpawnInjector::SpawnOwnershipState::kSymbiOwned,
        &error_message));
    assert(state.ownership_state ==
           NinjectorSpawnInjector::SpawnOwnershipState::kLegacyOwned);
    assert(error_message == "invalid spawn ownership state transition");
}

void TestResolveOwnershipStateFromBackendMapsFinalizeOwners() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    assert(injector.ResolveOwnershipStateFromBackend(
               NinjectorSpawnInjector::SpawnBackend::kNone) ==
           NinjectorSpawnInjector::SpawnOwnershipState::kNone);
    assert(injector.ResolveOwnershipStateFromBackend(
               NinjectorSpawnInjector::SpawnBackend::kZygoteControl) ==
           NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(injector.ResolveOwnershipStateFromBackend(
               NinjectorSpawnInjector::SpawnBackend::kSymbi) ==
           NinjectorSpawnInjector::SpawnOwnershipState::kSymbiOwned);
    assert(injector.ResolveOwnershipStateFromBackend(
               NinjectorSpawnInjector::SpawnBackend::kLegacyNcore) ==
           NinjectorSpawnInjector::SpawnOwnershipState::kLegacyOwned);
}

void TestBuildPendingSpawnCommitSeedsZygoteControlOwnedRecord() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "request-token";
    owned_state.agent_path = "__embedded_agent__";
    owned_state.ncore_path = "/data/local/tmp/nook/libncore.so";
    owned_state.materialized_ncore = true;
    owned_state.materialized_agent = true;

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    owned_transaction.identifier = "com.demo.target";
    owned_transaction.spawn_token = "zygote-token";
    owned_transaction.targets.push_back({791, "zygote64"});

    const NinjectorSpawnInjector::PendingSpawnCommit pending_commit =
        injector.BuildPendingSpawnCommit(NinjectorSpawnInjector::SpawnBackend::kZygoteControl,
                                         "com.demo.target",
                                         std::move(owned_state),
                                         std::move(owned_transaction));

    assert(pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(pending_commit.spawn_state.identifier.empty());
    assert(pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(pending_commit.spawn_state.spawn_token == "request-token");
    assert(pending_commit.spawn_state.ncore_path.empty());
    assert(pending_commit.spawn_state.agent_path.empty());
    assert(!pending_commit.spawn_state.materialized_ncore);
    assert(!pending_commit.spawn_state.materialized_agent);
    assert(pending_commit.zygote_control_transaction.identifier == "com.demo.target");
    assert(pending_commit.zygote_control_transaction.spawn_token == "zygote-token");
    assert(pending_commit.zygote_control_transaction.targets ==
           (std::vector<std::pair<int, std::string>>{{791, "zygote64"}}));
}

void TestBuildPendingSpawnCommitKeepsNonZygoteOwnersSessionLocal() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "legacy-token";
    owned_state.ncore_path = "/data/local/tmp/nook/libncore.so";
    owned_state.agent_path = "__embedded_agent__";
    owned_state.materialized_ncore = true;
    owned_state.materialized_agent = true;

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction foreign_transaction;
    foreign_transaction.identifier = "foreign";
    foreign_transaction.spawn_token = "foreign-token";
    foreign_transaction.targets.push_back({123, "zygote64"});

    const NinjectorSpawnInjector::PendingSpawnCommit pending_commit =
        injector.BuildPendingSpawnCommit(NinjectorSpawnInjector::SpawnBackend::kLegacyNcore,
                                         "com.demo.target",
                                         std::move(owned_state),
                                         std::move(foreign_transaction));

    assert(pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(pending_commit.spawn_state.identifier.empty());
    assert(pending_commit.spawn_state.spawn_token == "legacy-token");
    assert(pending_commit.spawn_state.ncore_path.empty());
    assert(pending_commit.spawn_state.agent_path.empty());
    assert(!pending_commit.spawn_state.materialized_ncore);
    assert(!pending_commit.spawn_state.materialized_agent);
    assert(pending_commit.shell_owner_state.identifier == "com.demo.target");
    assert(pending_commit.shell_owner_state.spawn_token == "legacy-token");
    assert(pending_commit.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kLegacyNcore);
    assert(pending_commit.zygote_control_transaction.identifier.empty());
    assert(pending_commit.zygote_control_transaction.spawn_token.empty());
    assert(pending_commit.zygote_control_transaction.targets.empty());
}

void TestBuildPendingSpawnCommitKeepsSymbiOwnerInSpawnState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "symbi-token";
    owned_state.agent_path = "__embedded_agent__";

    const NinjectorSpawnInjector::PendingSpawnCommit pending_commit =
        injector.BuildPendingSpawnCommit(NinjectorSpawnInjector::SpawnBackend::kSymbi,
                                         "com.demo.target",
                                         std::move(owned_state),
                                         NinjectorSpawnInjector::ZygoteControlOwnedTransaction{});

    assert(pending_commit.spawn_state.identifier == "com.demo.target");
    assert(pending_commit.spawn_state.spawn_token == "symbi-token");
    assert(pending_commit.spawn_state.agent_path == "__embedded_agent__");
    assert(pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kSymbi);
    assert(pending_commit.shell_owner_state.identifier.empty());
    assert(pending_commit.shell_owner_state.spawn_token.empty());
    assert(pending_commit.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestBuildPendingSpawnCommitStripsLegacyNcoreResidueFromSymbiOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "symbi-token";
    owned_state.ncore_path = "/data/local/tmp/nook/libncore.so";
    owned_state.agent_path = "__embedded_agent__";
    owned_state.materialized_ncore = true;
    owned_state.materialized_agent = true;

    const NinjectorSpawnInjector::PendingSpawnCommit pending_commit =
        injector.BuildPendingSpawnCommit(NinjectorSpawnInjector::SpawnBackend::kSymbi,
                                         "com.demo.target",
                                         std::move(owned_state),
                                         NinjectorSpawnInjector::ZygoteControlOwnedTransaction{});

    assert(pending_commit.spawn_state.identifier == "com.demo.target");
    assert(pending_commit.spawn_state.spawn_token == "symbi-token");
    assert(pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kSymbi);
    assert(pending_commit.spawn_state.ncore_path.empty());
    assert(!pending_commit.spawn_state.materialized_ncore);
    assert(pending_commit.spawn_state.agent_path == "__embedded_agent__");
    assert(pending_commit.spawn_state.materialized_agent);
}

void TestApplySuccessfulRouteCommitSeedsLegacyFallbackAndOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "legacy-token";
    owned_state.ncore_path = "/data/local/tmp/nook/libncore.so";
    owned_state.agent_path = "__embedded_agent__";
    owned_state.materialized_ncore = true;
    owned_state.materialized_agent = true;

    int pid = 0;
    std::string error_message;
    assert(injector.ApplySuccessfulRouteCommit(&outcome,
                                               NinjectorSpawnInjector::SpawnBackend::kLegacyNcore,
                                               "com.demo.target",
                                               std::move(owned_state),
                                               19001,
                                               NinjectorSpawnInjector::SpawnFallbackPolicy::kAllowed,
                                               NinjectorSpawnInjector::ZygoteControlOwnedTransaction{},
                                               &pid,
                                               &error_message));

    assert(pid == 19001);
    assert(error_message.empty());
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kSuccess);
    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kAllowed);
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(outcome.pending_commit.spawn_state.identifier.empty());
    assert(outcome.pending_commit.spawn_state.spawn_token == "legacy-token");
    assert(outcome.pending_commit.spawn_state.ncore_path.empty());
    assert(outcome.pending_commit.spawn_state.agent_path.empty());
    assert(!outcome.pending_commit.spawn_state.materialized_ncore);
    assert(!outcome.pending_commit.spawn_state.materialized_agent);
    assert(outcome.pending_commit.shell_owner_state.identifier == "com.demo.target");
    assert(outcome.pending_commit.shell_owner_state.spawn_token == "legacy-token");
    assert(outcome.pending_commit.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kLegacyNcore);
}

void TestApplySuccessfulRouteCommitSeedsZygoteOwnerTransaction() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    NinjectorSpawnInjector::SpawnOwnedState owned_state;
    owned_state.spawn_token = "request-token";

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    owned_transaction.identifier = "com.demo.target";
    owned_transaction.spawn_token = "zygote-token";
    owned_transaction.targets.push_back({791, "zygote64"});

    int pid = 0;
    std::string error_message;
    assert(injector.ApplySuccessfulRouteCommit(&outcome,
                                               NinjectorSpawnInjector::SpawnBackend::kZygoteControl,
                                               "com.demo.target",
                                               std::move(owned_state),
                                               17001,
                                               NinjectorSpawnInjector::SpawnFallbackPolicy::kUnknown,
                                               std::move(owned_transaction),
                                               &pid,
                                               &error_message));

    assert(pid == 17001);
    assert(error_message.empty());
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kSuccess);
    assert(outcome.pending_commit.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(outcome.pending_commit.spawn_state.identifier.empty());
    assert(outcome.pending_commit.spawn_state.spawn_token == "request-token");
    assert(outcome.pending_commit.zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.pending_commit.zygote_control_transaction.spawn_token == "zygote-token");
}

void TestApplyTerminalOutcomeClassificationSeedsFallbackFailurePair() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    injector.ApplyTerminalOutcomeClassification(
        &outcome,
        NinjectorSpawnInjector::SpawnFinalStatus::kFallbackFailed,
        NinjectorSpawnInjector::SpawnTerminalBackend::kZygoteControl,
        NinjectorSpawnInjector::SpawnTerminalBackend::kLegacy);

    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kFallbackFailed);
    assert(outcome.terminal_primary_backend ==
           NinjectorSpawnInjector::SpawnTerminalBackend::kZygoteControl);
    assert(outcome.terminal_secondary_backend ==
           NinjectorSpawnInjector::SpawnTerminalBackend::kLegacy);
}

void TestApplyTerminalOutcomeClassificationSeedsBackendUnavailablePair() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    injector.ApplyTerminalOutcomeClassification(
        &outcome,
        NinjectorSpawnInjector::SpawnFinalStatus::kBackendUnavailable,
        NinjectorSpawnInjector::SpawnTerminalBackend::kSymbi,
        NinjectorSpawnInjector::SpawnTerminalBackend::kNone);

    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kBackendUnavailable);
    assert(outcome.terminal_primary_backend ==
           NinjectorSpawnInjector::SpawnTerminalBackend::kSymbi);
    assert(outcome.terminal_secondary_backend ==
           NinjectorSpawnInjector::SpawnTerminalBackend::kNone);
}

void TestApplyFailedZygoteControlClassificationSeedsAbortOutcome() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.identifier = "com.demo.target";
    transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    transaction.lifecycle_state = ZygoteControlFailureState::kInstallHook;

    injector.ApplyFailedZygoteControlClassification(&outcome, transaction, false);

    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kForbidden);
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kAbort);
    assert(outcome.failed_zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.failed_zygote_control_transaction.identifier == "com.demo.target");
    assert(outcome.zygote_control_state == ZygoteControlFailureState::kReadyWait);
}

void TestApplyFailedZygoteControlClassificationSeedsFallbackOutcome() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.identifier = "com.demo.target";
    transaction.failure_state = ZygoteControlFailureState::kReadyWait;
    transaction.lifecycle_state = ZygoteControlFailureState::kInstallHook;

    injector.ApplyFailedZygoteControlClassification(&outcome, transaction, true);

    assert(outcome.fallback_policy == NinjectorSpawnInjector::SpawnFallbackPolicy::kAllowed);
    assert(outcome.final_status == NinjectorSpawnInjector::SpawnFinalStatus::kUnknown);
    assert(outcome.failed_zygote_control_transaction.lifecycle_state ==
           ZygoteControlFailureState::kInstallHook);
    assert(outcome.failed_zygote_control_transaction.lifecycle_state ==
           ZygoteControlFailureState::kInstallHook);
    assert(outcome.zygote_control_state == ZygoteControlFailureState::kReadyWait);
}

void TestApplyFailedZygoteControlClassificationInfersTransactionStateFromDetail() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOutcome outcome;
    outcome.zygote_control_error = "install zygote fork hook failed: rpc timeout";

    NinjectorSpawnInjector::ZygoteControlOwnedTransaction transaction;
    transaction.identifier = "com.demo.target";

    injector.ApplyFailedZygoteControlClassification(&outcome, transaction, true);

    assert(outcome.failed_zygote_control_transaction.failure_state ==
           ZygoteControlFailureState::kReadyWait);
    assert(outcome.failed_zygote_control_transaction.lifecycle_state ==
           ZygoteControlFailureState::kReadyWait);
    assert(outcome.zygote_control_state == ZygoteControlFailureState::kReadyWait);
}

void TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.spawn_state.ncore_path = "/data/local/tmp/nook/libncore.so";
        injector.active_spawn_owner_.spawn_state.agent_path = "__embedded_agent__";
        injector.active_spawn_owner_.spawn_state.materialized_ncore = true;
        injector.active_spawn_owner_.spawn_state.materialized_agent = true;
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
    }

    assert(injector.ReleaseActiveOwnerAfterDeferredRouting("com.demo.target", "owner-token"));
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "owner-token");
    assert(injector.active_spawn_owner_.spawn_state.ncore_path.empty());
    assert(injector.active_spawn_owner_.spawn_state.agent_path.empty());
    assert(!injector.active_spawn_owner_.spawn_state.materialized_ncore);
    assert(!injector.active_spawn_owner_.spawn_state.materialized_agent);
}

void TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingSymbiOwnerOnly() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.spawn_state.spawn_token = "symbi-owner-token";
        injector.active_spawn_owner_.spawn_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kSymbi;
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "compat-token";
    }

    assert(injector.ReleaseActiveOwnerAfterDeferredRouting("com.demo.target",
                                                           "symbi-owner-token"));
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token.empty());
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.spawn_token == "compat-token");
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestReleaseActiveOwnerAfterDeferredRoutingPreservesForeignOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
    }

    assert(!injector.ReleaseActiveOwnerAfterDeferredRouting("com.demo.target", "other-token"));
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kLegacyNcore);
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "owner-token");
}

void TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingResidualTransactionOnly() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "foreign-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "other.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "foreign-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    assert(injector.ReleaseActiveOwnerAfterDeferredRouting("com.demo.target", "txn-token"));
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.identifier == "other.target");
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "foreign-token");
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier.empty());
    assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token.empty());
}

void TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingSpawnOwnerOnly() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.spawn_state.ncore_path = "/data/local/tmp/nook/libncore.so";
        injector.active_spawn_owner_.spawn_state.agent_path = "__embedded_agent__";
        injector.active_spawn_owner_.spawn_state.materialized_ncore = true;
        injector.active_spawn_owner_.spawn_state.materialized_agent = true;
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "other.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "foreign-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    assert(injector.ReleaseActiveOwnerAfterDeferredRouting("com.demo.target", "owner-token"));
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "owner-token");
    assert(injector.active_spawn_owner_.spawn_state.ncore_path.empty());
    assert(injector.active_spawn_owner_.spawn_state.agent_path.empty());
    assert(!injector.active_spawn_owner_.spawn_state.materialized_ncore);
    assert(!injector.active_spawn_owner_.spawn_state.materialized_agent);
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier == "other.target");
    assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token == "foreign-token");
}

void TestReleaseActiveOwnerAfterDeferredRoutingClearsZygoteOwnerShellByTransactionMatch() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token.clear();
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token.clear();
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kZygoteControl;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    assert(injector.ReleaseActiveOwnerAfterDeferredRouting("com.demo.target", "txn-token"));
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token.empty());
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier.empty());
    assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token.empty());
}

void TestFinalizeWithoutOwnedBackendReturnsForeignOwnerSuccess() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    std::string error_message;
    assert(injector.FinalizeWithoutOwnedBackend(MakeSpawnRequest("com.demo.target"),
                                                false,
                                                NinjectorSpawnInjector::ZygoteControlOwnedTransaction{},
                                                &error_message));
    assert(error_message.empty());
}

void TestFinalizeWithoutOwnedBackendDoesNotUseGlobalRecorderWithoutResidualTransaction() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.clear_spawn = [&](int, const char*, const char*) {
        trace.calls.push_back("legacy-clear-fail");
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);

    std::string error_message;
    assert(!injector.FinalizeWithoutOwnedBackend(MakeSpawnRequest("com.demo.target"),
                                                 false,
                                                 NinjectorSpawnInjector::ZygoteControlOwnedTransaction{},
                                                 &error_message));
    assert(error_message == "clear_spawn_in_zygote failed");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "legacy-clear-fail"}));
}

void TestFinalizeSpawnTreatsResidualZygoteTransactionAsOwnedPath() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        {},
        [&](int zygote_pid, const std::string& process_name, std::string* error_message) {
            uninstall_called = true;
            trace.calls.push_back(std::string("uninstall:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-finalize-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    std::string error_message;
    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(uninstall_called);
    AssertTraceEquals(trace.calls,
                      std::vector<std::string>{
                          "uninstall:791",
                          "clear-control:791"});
}

void TestBuildFinalizeSessionCapturesOwnedOwnerContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "owner-token";
    }

    const auto session =
        injector.BuildFinalizeSession(MakeSpawnRequest("com.demo.target"));

    assert(session.finalize_owner ==
           NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(session.owned_spawn_state.identifier.empty());
    assert(session.owned_spawn_state.spawn_token.empty());
    assert(session.owned_spawn_state.backend == NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(session.owned_zygote_transaction.identifier == "com.demo.target");
    assert(session.owned_zygote_transaction.spawn_token == "owner-token");
    assert(!session.has_foreign_active_owner);
    assert(session.owned_zygote_transaction.targets.empty());
}

void TestBuildFinalizeSessionCapturesForeignOwnerAndResidualFlags() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "other.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    const auto session =
        injector.BuildFinalizeSession(MakeSpawnRequest("com.demo.target"));

    assert(session.finalize_owner ==
           NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(session.owned_spawn_state.identifier.empty());
    assert(session.owned_zygote_transaction.identifier == "com.demo.target");
    assert(session.owned_zygote_transaction.targets ==
           (std::vector<std::pair<int, std::string>>{{791, "zygote64"}}));
    assert(session.has_foreign_active_owner);
}

void TestTakeActiveOwnerForFinalizeExtractsUnifiedOwnerRecord() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "owner-token";
    }

    NinjectorSpawnInjector::SpawnOwnershipState finalize_owner =
        NinjectorSpawnInjector::SpawnOwnershipState::kNone;
    NinjectorSpawnInjector::SpawnOwnedState owned_spawn_state;
    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    bool has_foreign_active_owner = true;

    assert(injector.TakeActiveOwnerForFinalize("com.demo.target",
                                               &finalize_owner,
                                               &owned_spawn_state,
                                               &owned_transaction,
                                               &has_foreign_active_owner,
                                               nullptr));
    assert(finalize_owner == NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(owned_spawn_state.identifier.empty());
    assert(owned_spawn_state.spawn_token.empty());
    assert(owned_spawn_state.backend == NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(owned_transaction.identifier == "com.demo.target");
    assert(owned_transaction.spawn_token == "owner-token");
    assert(!has_foreign_active_owner);
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
}

void TestTakeActiveOwnerForFinalizeClearsTokenOnlyCompatWhenTransactionOwnsRequest() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "retry-token";
        injector.active_spawn_owner_.spawn_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kNone;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "retry-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    NinjectorSpawnInjector::SpawnOwnershipState finalize_owner =
        NinjectorSpawnInjector::SpawnOwnershipState::kNone;
    NinjectorSpawnInjector::SpawnOwnedState owned_spawn_state;
    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    bool has_foreign_active_owner = false;

    assert(injector.TakeActiveOwnerForFinalize("com.demo.target",
                                               &finalize_owner,
                                               &owned_spawn_state,
                                               &owned_transaction,
                                               &has_foreign_active_owner,
                                               nullptr));
    assert(finalize_owner == NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(owned_spawn_state.identifier.empty());
    assert(owned_spawn_state.spawn_token.empty());
    assert(owned_transaction.identifier == "com.demo.target");
    assert(owned_transaction.spawn_token == "retry-token");
    assert(!has_foreign_active_owner);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token.empty());
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestTakeActiveOwnerForFinalizeIgnoresCompatibilitySpawnStateAsOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.spawn_state.spawn_token = "compat-token";
        injector.active_spawn_owner_.spawn_state.backend = NinjectorSpawnInjector::SpawnBackend::kNone;
    }

    NinjectorSpawnInjector::SpawnOwnershipState finalize_owner =
        NinjectorSpawnInjector::SpawnOwnershipState::kNone;
    NinjectorSpawnInjector::SpawnOwnedState owned_spawn_state;
    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    bool has_foreign_active_owner = false;

    assert(injector.TakeActiveOwnerForFinalize("com.demo.target",
                                               &finalize_owner,
                                               &owned_spawn_state,
                                               &owned_transaction,
                                               &has_foreign_active_owner,
                                               nullptr));
    assert(finalize_owner == NinjectorSpawnInjector::SpawnOwnershipState::kNone);
    assert(owned_spawn_state.identifier.empty());
    assert(owned_spawn_state.spawn_token.empty());
    assert(owned_spawn_state.backend == NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(owned_transaction.identifier.empty());
    assert(!has_foreign_active_owner);
    assert(injector.active_spawn_owner_.spawn_state.identifier == "com.demo.target");
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "compat-token");
    assert(injector.active_spawn_owner_.spawn_state.backend == NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestTakeActiveOwnerForFinalizePreservesForeignResidualTransaction() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.spawn_state.ncore_path = "/data/local/tmp/nook/libncore.so";
        injector.active_spawn_owner_.spawn_state.agent_path = "__embedded_agent__";
        injector.active_spawn_owner_.spawn_state.materialized_ncore = true;
        injector.active_spawn_owner_.spawn_state.materialized_agent = true;
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "other.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "foreign-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    NinjectorSpawnInjector::SpawnOwnershipState finalize_owner =
        NinjectorSpawnInjector::SpawnOwnershipState::kNone;
    NinjectorSpawnInjector::SpawnOwnedState owned_spawn_state;
    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    bool has_foreign_active_owner = false;

    assert(injector.TakeActiveOwnerForFinalize("com.demo.target",
                                               &finalize_owner,
                                               &owned_spawn_state,
                                               &owned_transaction,
                                               &has_foreign_active_owner,
                                               nullptr));
    assert(finalize_owner == NinjectorSpawnInjector::SpawnOwnershipState::kLegacyOwned);
    assert(owned_spawn_state.identifier == "com.demo.target");
    assert(owned_transaction.identifier.empty());
    assert(has_foreign_active_owner);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "owner-token");
    assert(injector.active_spawn_owner_.spawn_state.ncore_path.empty());
    assert(injector.active_spawn_owner_.spawn_state.agent_path.empty());
    assert(!injector.active_spawn_owner_.spawn_state.materialized_ncore);
    assert(!injector.active_spawn_owner_.spawn_state.materialized_agent);
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier == "other.target");
    assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token == "foreign-token");
}

void TestTakeActiveOwnerForFinalizePromotesResidualTransactionToOwnedOwner() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "foreign-owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "other.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "foreign-owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-owner-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    NinjectorSpawnInjector::SpawnOwnershipState finalize_owner =
        NinjectorSpawnInjector::SpawnOwnershipState::kNone;
    NinjectorSpawnInjector::SpawnOwnedState owned_spawn_state;
    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    bool has_foreign_active_owner = false;

    assert(injector.TakeActiveOwnerForFinalize("com.demo.target",
                                               &finalize_owner,
                                               &owned_spawn_state,
                                               &owned_transaction,
                                               &has_foreign_active_owner,
                                               nullptr));
    assert(finalize_owner == NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(owned_spawn_state.identifier.empty());
    assert(owned_transaction.identifier == "com.demo.target");
    assert(owned_transaction.spawn_token == "txn-owner-token");
    assert(has_foreign_active_owner);
    assert(injector.active_spawn_owner_.spawn_state.spawn_token == "foreign-owner-token");
    assert(injector.active_spawn_owner_.shell_owner_state.identifier == "other.target");
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier.empty());
}

void TestTakeActiveOwnerForFinalizePrefersMatchingZygoteTransactionOverShellBackend() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "legacy-owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "legacy-owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-owner-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    NinjectorSpawnInjector::SpawnOwnershipState finalize_owner =
        NinjectorSpawnInjector::SpawnOwnershipState::kNone;
    NinjectorSpawnInjector::SpawnOwnedState owned_spawn_state;
    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    bool has_foreign_active_owner = true;

    assert(injector.TakeActiveOwnerForFinalize("com.demo.target",
                                               &finalize_owner,
                                               &owned_spawn_state,
                                               &owned_transaction,
                                               &has_foreign_active_owner,
                                               nullptr));
    assert(finalize_owner == NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(owned_spawn_state.identifier == "com.demo.target");
    assert(owned_spawn_state.spawn_token == "legacy-owner-token");
    assert(owned_transaction.identifier == "com.demo.target");
    assert(owned_transaction.spawn_token == "txn-owner-token");
    assert(owned_transaction.targets ==
           (std::vector<std::pair<int, std::string>>{{791, "zygote64"}}));
    assert(!has_foreign_active_owner);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.zygote_control_transaction.identifier.empty());
}

void TestTakeActiveOwnerForFinalizeNormalizesReturnedShellBackendWhenTransactionOwnsRequest() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "legacy-owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "legacy-owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-owner-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    NinjectorSpawnInjector::SpawnOwnershipState finalize_owner =
        NinjectorSpawnInjector::SpawnOwnershipState::kNone;
    NinjectorSpawnInjector::SpawnOwnedState owned_spawn_state;
    NinjectorSpawnInjector::ZygoteControlOwnedTransaction owned_transaction;
    bool has_foreign_active_owner = true;

    assert(injector.TakeActiveOwnerForFinalize("com.demo.target",
                                               &finalize_owner,
                                               &owned_spawn_state,
                                               &owned_transaction,
                                               &has_foreign_active_owner,
                                               nullptr));
    assert(finalize_owner == NinjectorSpawnInjector::SpawnOwnershipState::kZygoteControlOwned);
    assert(owned_spawn_state.backend == NinjectorSpawnInjector::SpawnBackend::kNone);
    assert(owned_spawn_state.identifier == "com.demo.target");
    assert(owned_spawn_state.spawn_token == "legacy-owner-token");
    assert(owned_spawn_state.ncore_path.empty());
    assert(owned_spawn_state.agent_path.empty());
    assert(!owned_spawn_state.materialized_ncore);
    assert(!owned_spawn_state.materialized_agent);
    assert(owned_transaction.identifier == "com.demo.target");
    assert(owned_transaction.spawn_token == "txn-owner-token");
    assert(!has_foreign_active_owner);
}

void TestApplySpawnRoutingSnapshotRejectsCommittedSymbiWithoutOwnedState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
        },
        &error_message));
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
            .update_symbi_window = true,
            .symbi_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterSymbi,
        },
        &error_message));

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromSymbi,
        },
        &error_message));
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsCommittedLegacyWithoutOwnedState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
        },
        &error_message));
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kLegacy,
            .update_legacy_window = true,
            .legacy_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterLegacy,
        },
        &error_message));

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromLegacy,
        },
        &error_message));
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestTransitionSpawnExecutionPhaseAllowsLegalTransitions() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kRouting,
        NinjectorSpawnInjector::SpawnExecutionReason::kBeginRouting,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouting);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kBeginRouting);
    assert(error_message.empty());

    assert(injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kRouteDeferred,
        NinjectorSpawnInjector::SpawnExecutionReason::kRouteDeferredForTerminalClassification,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouteDeferred);
    assert(state.phase_reason ==
           NinjectorSpawnInjector::SpawnExecutionReason::kRouteDeferredForTerminalClassification);
    assert(error_message.empty());

    assert(injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kTerminal,
        NinjectorSpawnInjector::SpawnExecutionReason::kBeginTerminalClassification,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kTerminal);
    assert(state.phase_reason ==
           NinjectorSpawnInjector::SpawnExecutionReason::kBeginTerminalClassification);
    assert(error_message.empty());

    assert(injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kTerminalResolved,
        NinjectorSpawnInjector::SpawnExecutionReason::kTerminalOutcomeResolved,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kTerminalResolved);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kTerminalOutcomeResolved);
    assert(error_message.empty());

    assert(injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kTerminalFinalized,
        NinjectorSpawnInjector::SpawnExecutionReason::kTerminalOutcomeFinalized,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kTerminalFinalized);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kTerminalOutcomeFinalized);
    assert(error_message.empty());

    assert(injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kCompleted,
        NinjectorSpawnInjector::SpawnExecutionReason::kCompletedAfterTerminalOutcome,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kCompleted);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kCompletedAfterTerminalOutcome);
    assert(error_message.empty());
}

void TestTransitionSpawnExecutionPhaseRejectsIllegalTransitions() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kTerminal,
        NinjectorSpawnInjector::SpawnExecutionReason::kBeginTerminalClassification,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kInit);
    assert(error_message == "invalid spawn execution phase transition");
}

void TestBuildSpawnExecutionStateSeedsPhaseReason() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    const auto state =
        injector.BuildSpawnExecutionState(MakeSpawnRequest("com.demo.target"), "/data/local/tmp/nook/libnook-agent.so");
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kInit);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kInitialized);
}

void TestTransitionSpawnExecutionPhaseRecordsReason() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.TransitionSpawnExecutionPhase(
        &state,
        NinjectorSpawnInjector::SpawnExecutionPhase::kRouting,
        NinjectorSpawnInjector::SpawnExecutionReason::kBeginRouting,
        &error_message));
    assert(state.phase == NinjectorSpawnInjector::SpawnExecutionPhase::kRouting);
    assert(state.phase_reason == NinjectorSpawnInjector::SpawnExecutionReason::kBeginRouting);
    assert(error_message.empty());
}

void TestApplySpawnRoutingSnapshotRejectsInvalidCommittedStateJump() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromLegacy,
        },
        &error_message));
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kNotStarted);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsInvalidProgressJump() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterSymbi,
        },
        &error_message));
    assert(state.routing_progress == NinjectorSpawnInjector::SpawnRoutingProgress::kNotStarted);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsRouteStepBeforeProgressBegins() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kLegacy,
        },
        &error_message));
    assert(state.current_route_step == NinjectorSpawnInjector::SpawnRouteStep::kNone);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsInvalidZygoteControlRouteCommitJump() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kCommitted,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kNotStarted);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsInvalidZygoteControlRouteDeferredJump() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kDeferredToFallback,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kNotStarted);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsZygoteControlEnteredWithoutZygoteControlStep() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kNotStarted);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsZygoteControlCommittedWithoutCommittedRoutingState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kZygoteControl,
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered,
            .update_zygote_control_window = true,
            .zygote_control_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterZygoteControl,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kCommitted,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsZygoteControlSkippedWithoutSkippedWindow() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kNotStarted);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsZygoteControlDeferredWithoutRunningZygoteControlContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped,
            .update_zygote_control_window = true,
            .zygote_control_window =
                NinjectorSpawnInjector::SpawnRouteWindowState::kSkippedByPolicy,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kDeferredToFallback,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsZygoteControlAbortedWithoutRunningZygoteControlContext() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kZygoteControl,
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered,
            .update_zygote_control_window = true,
            .zygote_control_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kAborted,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsZygoteControlSkippedToEnteredTransition() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped,
            .update_zygote_control_window = true,
            .zygote_control_window =
                NinjectorSpawnInjector::SpawnRouteWindowState::kSkippedByPolicy,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kZygoteControl,
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered,
            .update_zygote_control_window = true,
            .zygote_control_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped);
    assert(state.current_route_step == NinjectorSpawnInjector::SpawnRouteStep::kSymbi);
    assert(state.routing_windows.zygote_control ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kSkippedByPolicy);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsZygoteControlEnteredToSkippedTransition() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kZygoteControl,
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered,
            .update_zygote_control_window = true,
            .zygote_control_window = NinjectorSpawnInjector::SpawnRouteWindowState::kEntered,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_zygote_control_route_state = true,
            .zygote_control_route_state =
                NinjectorSpawnInjector::SpawnZygoteControlRouteState::kSkipped,
            .update_zygote_control_window = true,
            .zygote_control_window =
                NinjectorSpawnInjector::SpawnRouteWindowState::kSkippedByPolicy,
        },
        &error_message));
    assert(state.zygote_control_route_state ==
           NinjectorSpawnInjector::SpawnZygoteControlRouteState::kEntered);
    assert(state.routing_windows.zygote_control ==
           NinjectorSpawnInjector::SpawnRouteWindowState::kEntered);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsCommittedZygoteControlWithoutAlignedProgressAndStep() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
        },
        &error_message));
    assert(error_message.empty());
    error_message.clear();
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterZygoteControl,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromZygoteControl,
        },
        &error_message));
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsCommittedLegacyWithoutAlignedProgressAndStep() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
        },
        &error_message));
    assert(error_message.empty());
    error_message.clear();
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterLegacy,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromLegacy,
        },
        &error_message));
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsCommittedSymbiWithoutAlignedProgressAndStep() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
        },
        &error_message));
    assert(error_message.empty());
    error_message.clear();
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterSymbi,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kLegacy,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kCommittedFromSymbi,
        },
        &error_message));
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestApplySpawnRoutingSnapshotRejectsDeferredTerminalWithoutLegacyProgress() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnExecutionState state;
    std::string error_message;
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kRunning,
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kEnteredRouting,
        },
        &error_message));
    assert(error_message.empty());
    error_message.clear();
    assert(injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_progress = true,
            .routing_progress = NinjectorSpawnInjector::SpawnRoutingProgress::kAfterSymbi,
            .update_current_route_step = true,
            .current_route_step = NinjectorSpawnInjector::SpawnRouteStep::kSymbi,
        },
        &error_message));
    assert(error_message.empty());

    error_message.clear();
    assert(!injector.ApplySpawnRoutingSnapshot(
        &state,
        NinjectorSpawnInjector::SpawnRoutingSnapshot{
            .update_routing_state = true,
            .routing_state = NinjectorSpawnInjector::SpawnRoutingState::kDeferredToTerminal,
        },
        &error_message));
    assert(state.routing_state == NinjectorSpawnInjector::SpawnRoutingState::kRunning);
    assert(error_message == "invalid spawn routing snapshot transition");
}

void TestSpawnSuccessReturnsArmedLegacyPidSentinel() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};
    const fs::path dir = MakeTempDir("legacy_success");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target"),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:",
                               "start:com.demo.target"}));
    assert(trace.ncore_path == (dir / "libncore.so").string());
    assert(trace.package_name == "com.demo.target");
    assert(trace.so_path == agent.string());

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:",
                               "start:com.demo.target",
                               "get_pid:zygote64",
                               "clear:791",
                               "clear-token:"}));
    assert(trace.clear_ncore_path == (dir / "libncore.so").string());
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnDefaultStablePathSkipsZygoteControlEvenWhenSupportIsEnabled() {
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("default_zygote");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    bool install_called = false;
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string*) {
            install_called = true;
            return true;
        },
        [&](int, const std::string&, std::string*) {
            uninstall_called = true;
            trace.calls.push_back("uninstall");
            return true;
        });

    int pid = 0;
    std::string error_message;
    const bool spawn_ok = injector.Spawn(MakeSpawnRequest("com.demo.target"),
                                         agent.string(),
                                         &pid,
                                         &error_message);
    if (!spawn_ok) {
        std::fprintf(stderr,
                     "TestSpawnDefaultStablePathSkipsZygoteControlEvenWhenSupportIsEnabled failed: %s\n",
                     error_message.c_str());
        std::fflush(stderr);
    }
    assert(spawn_ok);
    assert(pid == 17001);
    assert(error_message.empty());
    assert(!install_called);
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.spawn_token.empty());

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnDefaultSymbiWhenZygoteControlIsDisabled() {
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("default_legacy_when_zygote_control_disabled");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target", {"--nook-spawn-token=spawn-token-symbi"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.spawn_token == "spawn-token-symbi");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnDefaultStablePathDoesNotTrySymbiWhenPreferenceDisabled() {
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", "1");
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("default_legacy_does_not_try_symbi");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    ops.spawn_symbi = [&trace](int zygote_pid,
                               const char*,
                               const char*,
                               const char*,
                               const char*,
                               int*) {
        trace.calls.push_back(std::string("symbi-fail:") + std::to_string(zygote_pid));
        return false;
    };
    ops.spawn_symbi_embedded = {};
    ops.get_inject_error = []() {
        return std::string("symbi_timeout");
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target", {"--nook-spawn-token=spawn-token-default-fallback"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:spawn-token-default-fallback",
                               "start:com.demo.target"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnDefaultPathPrefersSymbiByDefault() {
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("default_symbi_preferred");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target", {"--nook-spawn-token=spawn-token-default-symbi"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.spawn_token == "spawn-token-default-symbi");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
}

void TestSpawnDefaultPathFallsBackToLegacyWhenSymbiFails() {
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_DISABLE_SYMBI_PREFERENCE", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi_embedded = {};
    const fs::path dir = MakeTempDir("default_symbi_fallback_legacy");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    ops.spawn_symbi = [&trace](int zygote_pid,
                               const char*,
                               const char*,
                               const char*,
                               const char*,
                               int*) {
        trace.calls.push_back(std::string("symbi-fail:") + std::to_string(zygote_pid));
        return false;
    };
    ops.get_inject_error = []() {
        return std::string("symbi_timeout");
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target", {"--nook-spawn-token=spawn-token-default-symbi-fallback"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-fail:791",
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:spawn-token-default-symbi-fallback",
                               "start:com.demo.target"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnExplicitSymbiPrefersSymbiBackend() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("explicit_symbi_prefers_symbi");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(
        MakeSpawnRequest("com.demo.target",
                         {"--nook-spawn-backend=symbi", "--nook-spawn-token=spawn-token-explicit-symbi"}),
        agent.string(),
        &pid,
        &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.spawn_token == "spawn-token-explicit-symbi");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnExplicitSymbiFailureDoesNotFallbackToLegacyBackend() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi_embedded = {};
    const fs::path dir = MakeTempDir("explicit_symbi_fallback_legacy");
    const fs::path agent = dir / "libnook-agent.so";

    ops.spawn_symbi = [&trace](int zygote_pid,
                               const char*,
                               const char*,
                               const char*,
                               const char*,
                               int*) {
        trace.calls.push_back(std::string("symbi-fail:") + std::to_string(zygote_pid));
        return false;
    };
    ops.get_inject_error = []() {
        return std::string("symbi_timeout");
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(
        MakeSpawnRequest("com.demo.target",
                         {"--nook-spawn-backend=symbi", "--nook-spawn-token=spawn-token-explicit-fallback"}),
        agent.string(),
        &pid,
        &error_message));
    assert(pid == 0);
    assert(error_message == "symbi spawn failed (--symbi): spawn_symbi failed: symbi_timeout");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-fail:791"}));
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnSymbiMaterializesEmbeddedAgentOnDemand() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("symbi_materialize_agent");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!fs::exists(agent));
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                          {"--nook-spawn-backend=symbi",
                                           "--nook-spawn-token=spawn-token-symbi-embedded"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert(!fs::exists(agent));
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.package_name == "com.demo.target");
    assert(trace.so_path.empty());
    assert(trace.runtime_dir == dir.string());
    assert(trace.spawn_token == "spawn-token-symbi-embedded");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!fs::exists(agent));
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnSymbiUsesEmbeddedBackendWithoutMaterializingAgentFile() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("symbi_embedded_backend");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!fs::exists(agent));
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                          {"--nook-spawn-backend=symbi",
                                           "--nook-spawn-token=spawn-token-symbi-memfd"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert(!fs::exists(agent));
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-embedded:791"}));
    assert(trace.package_name == "com.demo.target");
    assert(trace.so_path.empty());
    assert(trace.runtime_dir == dir.string());
    assert(trace.spawn_token == "spawn-token-symbi-memfd");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!fs::exists(agent));
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnDefaultConfigKeepsRuntimeDirectoryArtifacts() {
    SetEnvValue("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", nullptr);
    SetEnvValue("NOOK_RUNTIME_DIR", "/data/local/tmp/nook-test");
    const NinjectorSpawnConfig config = NinjectorSpawnInjector::DefaultConfig();
    assert(!config.enable_zygote_control);
    assert(config.ncore_path == "/data/local/tmp/nook-test/libncore.so");

    SetEnvValue("NOOK_RUNTIME_DIR", nullptr);
    SetEnvValue("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", nullptr);
}

void TestSpawnDefaultConfigUsesSymbiByDefault() {
    SetEnvValue("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", nullptr);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", "1");
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    const fs::path dir = MakeTempDir("default_config_legacy");
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }
    SetEnvValue("NOOK_RUNTIME_DIR", dir.string().c_str());

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(NinjectorSpawnInjector::DefaultConfig(), ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target"),
                          "__embedded_agent__",
                          &pid,
                          &error_message));
    assert(pid == 17001);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote",
                               "symbi-embedded:791"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    SetEnvValue("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", nullptr);
    SetEnvValue("NOOK_PREFER_SYMBI_BACKEND", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    SetEnvValue("NOOK_RUNTIME_DIR", nullptr);
    RemoveAllIgnoringMissing(dir);
}

void TestSpawnZygoteControlPathDoesNotEnterLegacyPrepareWhenInstallCallbackSucceeds() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    bool install_called = false;
    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            install_called = true;
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-rpc"}),
                          "__embedded_agent__",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(!install_called);
    for (const std::string& call : trace.calls) {
        assert(call.rfind("prepare:", 0) != 0);
    }
}

void TestFinalizeSpawnDoesNotFallbackToLegacyClearAfterZygoteControlCommit() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    bool install_called = false;
    bool uninstall_called = false;
    bool clear_spawn_called = false;

    ops.is_zygote_monitor_ready = [](int) {
        return true;
    };
    ops.clear_spawn = [&](int, const char*, const char*) {
        clear_spawn_called = true;
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            install_called = true;
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int, const std::string&, std::string* error_message) {
            uninstall_called = true;
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-zc-finalize"}),
                          "/data/local/tmp/nook/libnook-agent.so",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(install_called);

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(uninstall_called);
    assert(!clear_spawn_called);
}

void TestSpawnCanDisableZygoteControlExplicitly() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.spawn_symbi_embedded = {};
    const fs::path dir = MakeTempDir("default_legacy");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    bool install_called = false;
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string*) {
            install_called = true;
            return true;
        },
        [&](int, const std::string&, std::string*) {
            uninstall_called = true;
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target"),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(!install_called);
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:",
                               "start:com.demo.target"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:",
                               "start:com.demo.target",
                               "get_pid:zygote64",
                               "clear:791",
                               "clear-token:"}));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnFailureFromPrepareStopsFlow() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops;
    ops.spawn_symbi = {};
    const fs::path dir = MakeTempDir("prepare_fail_with_detail");
    const fs::path agent = dir / "libnook-agent.so";

    ops.get_pid = [&](const char* process_name) {
        trace.calls.push_back(std::string("get_pid:") + process_name);
        return 791;
    };
    ops.prepare_spawn = [&](int, const char*, const char*, const char*, const char*) {
        trace.calls.push_back("prepare");
        return false;
    };
    ops.clear_spawn = [&](int, const char*, const char*) {
        trace.calls.push_back("clear");
        return true;
    };
    ops.start_target_app = [&](const char*) {
        trace.calls.push_back("start");
        return true;
    };
    ops.get_inject_error = [&]() {
        trace.calls.push_back("prepare_error");
        return std::string("dlsym_failed:ainject");
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target"),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message ==
           "prepare_spawn_in_zygote failed (embedded: dlsym_failed:ainject): dlsym_failed:ainject");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare_error",
                               "prepare",
                               "prepare_error"}));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnFailureFromPrepareWithoutDetailKeepsBaseMessage() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    NinjectorSpawnOps ops;
    ops.spawn_symbi = {};
    const fs::path dir = MakeTempDir("prepare_fail_no_detail");
    const fs::path agent = dir / "libnook-agent.so";

    ops.get_pid = [](const char*) {
        return 791;
    };
    ops.prepare_spawn = [](int, const char*, const char*, const char*, const char*) {
        return false;
    };
    ops.clear_spawn = [](int, const char*, const char*) {
        return true;
    };
    ops.start_target_app = [](const char*) {
        return true;
    };
    ops.get_inject_error = []() {
        return std::string();
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target"),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "prepare_spawn_in_zygote failed");
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestLegacySpawnStartFailureRollsBackPreparedState() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops;
    ops.spawn_symbi = {};
    ops.get_pid = [&](const char* process_name) {
        trace.calls.push_back(std::string("get_pid:") + process_name);
        return 791;
    };
    ops.prepare_spawn = [&](int, const char*, const char*, const char*, const char*) {
        trace.calls.push_back("prepare");
        return true;
    };
    ops.clear_spawn = [&](int, const char*, const char*) {
        trace.calls.push_back("clear");
        return true;
    };
    ops.start_target_app = [&](const char*) {
        trace.calls.push_back("start");
        return false;
    };

    const fs::path dir = MakeTempDir("legacy_start_fail_rollback");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target"),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "start_target_app failed");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare",
                               "start",
                               "clear",
                           }));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFailedRespawnDoesNotClearExistingLegacyOwnerState() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};

    const fs::path dir = MakeTempDir("failed_respawn_keeps_legacy_owner");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    bool fail_second_prepare = false;
    ops.prepare_spawn = [&](int zygote_pid,
                            const char* ncore_path,
                            const char* package_name,
                            const char* so_path,
                            const char* spawn_token) {
        trace.calls.push_back(std::string("prepare:") + std::to_string(zygote_pid));
        trace.ncore_path = ncore_path != nullptr ? ncore_path : "";
        trace.package_name = package_name != nullptr ? package_name : "";
        trace.so_path = so_path != nullptr ? so_path : "";
        trace.calls.push_back(std::string("token:") + (spawn_token != nullptr ? spawn_token : ""));
        return !fail_second_prepare;
    };
    ops.get_inject_error = [&]() {
        return fail_second_prepare ? std::string("second_prepare_failed") : std::string();
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-spawn-token=owner-token-1"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());

    fail_second_prepare = true;
    pid = 0;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-spawn-token=owner-token-2"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "spawn already active for identifier");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:owner-token-1",
                               "start:com.demo.target",
                               "get_pid:zygote64",
                               "clear:791",
                               "clear-token:owner-token-1"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestDuplicateActiveSpawnIsRejectedAndPreservesOwnerState() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};

    const fs::path dir = MakeTempDir("duplicate_active_spawn_rejected");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-spawn-token=owner-token-1"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());

    pid = 0;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-spawn-token=owner-token-2"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "spawn already active for identifier");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:owner-token-1",
                               "start:com.demo.target"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:owner-token-1",
                               "start:com.demo.target",
                               "get_pid:zygote64",
                               "clear:791",
                               "clear-token:owner-token-1"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestDifferentIdentifierSpawnIsRejectedWhileOwnerIsActive() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};

    const fs::path dir = MakeTempDir("different_identifier_spawn_rejected");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.owner",
                                           {"--nook-spawn-token=owner-token-1"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());

    pid = 0;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.other",
                                            {"--nook-spawn-token=owner-token-2"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "spawn already active");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:owner-token-1",
                               "start:com.demo.owner"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.owner"), &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:owner-token-1",
                               "start:com.demo.owner",
                               "get_pid:zygote64",
                               "clear:791",
                               "clear-token:owner-token-1"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestActiveOwnerRejectsSpawnBeforeZygoteControlAttempt() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("reject_before_zygote_attempt");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    bool install_called = false;
    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int, const std::string&, const std::string&, const std::string&, const std::string&, std::string* error_message) {
            install_called = true;
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int, const std::string&, std::string* error_message) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "owner-token";
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
    }

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target",
                                            {"--nook-spawn-token=blocked-token"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "spawn already active for identifier");
    assert(!install_called);
    assert(trace.calls.empty());

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestSpawnSymbiFailureStopsFlow() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi_embedded = {};
    const fs::path dir = MakeTempDir("symbi_fail");
    const fs::path agent = dir / "libnook-agent.so";

    ops.spawn_symbi = [&trace](int zygote_pid,
                              const char*,
                              const char*,
                              const char*,
                              const char*,
                              int*) {
        trace.calls.push_back(std::string("symbi-fail:") + std::to_string(zygote_pid));
        return false;
    };
    ops.get_inject_error = []() {
        return std::string("symbi_timeout");
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target", {"--nook-spawn-backend=symbi"}),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "symbi spawn failed (--symbi): spawn_symbi failed: symbi_timeout");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "symbi-fail:791"}));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFinalizeLegacyFailurePropagatesClearError() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};
    const fs::path dir = MakeTempDir("legacy_finalize_fail");
    const fs::path agent = dir / "libnook-agent.so";
    ops.clear_spawn = [&](int zygote_pid, const char* ncore_path, const char* spawn_token) {
        trace.calls.push_back(std::string("clear-fail:") + std::to_string(zygote_pid));
        trace.clear_ncore_path = ncore_path;
        trace.calls.push_back(std::string("clear-token:") + (spawn_token != nullptr ? spawn_token : ""));
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target"),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());

    assert(!injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "clear_spawn_in_zygote failed");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:",
                               "start:com.demo.target",
                               "get_pid:zygote64",
                               "clear-fail:791",
                               "clear-token:"}));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFinalizeOwnedResidualTransactionFormatsLocalZygoteControlStateInsteadOfGlobalState() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    NinjectorSpawnInjector* injector_ptr = nullptr;
    ops.clear_zygote_spawn_control = [&](int zygote_pid, const char* spawn_token, bool) {
        trace.calls.push_back(std::string("clear-control-fail:") + std::to_string(zygote_pid));
        trace.calls.push_back(std::string("clear-control-token:") + (spawn_token != nullptr ? spawn_token : ""));
        return false;
    };
    ops.clear_spawn = [&](int, const char*, const char*) {
        trace.calls.push_back("legacy-clear-fail");
        if (injector_ptr != nullptr) {
            injector_ptr->RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
            injector_ptr->RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);
        }
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        {},
        {},
        {},
        {}); 
    injector_ptr = &injector;

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "finalize-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    injector.RecordZygoteControlFailureState(ZygoteControlFailureState::kLaunchApp);
    injector.RecordZygoteControlLifecycleStage(ZygoteControlFailureState::kLaunchApp);

    std::string error_message;
    assert(!injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message ==
           "clear zygote spawn control failed");
    assert((trace.calls == std::vector<std::string>{
                               "clear-control-fail:791",
                               "clear-control-token:finalize-token"}));

    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFinalizeSpawnFailurePreservesMatchingOwnedZygoteTransactionForRetry() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.clear_zygote_spawn_control = [&](int zygote_pid, const char* spawn_token, bool) {
        trace.calls.push_back(std::string("clear-control-fail:") + std::to_string(zygote_pid));
        trace.calls.push_back(std::string("clear-control-token:") + (spawn_token != nullptr ? spawn_token : ""));
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        {},
        [&](int zygote_pid, const std::string& process_name, std::string* error_message) {
            trace.calls.push_back(std::string("uninstall:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "retry-token";
        injector.active_spawn_owner_.spawn_state.backend = NinjectorSpawnInjector::SpawnBackend::kNone;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "retry-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    std::string error_message;
    assert(!injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "clear zygote spawn control failed");

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        assert(injector.active_spawn_owner_.zygote_control_transaction.identifier == "com.demo.target");
        assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token == "retry-token");
        assert(injector.active_spawn_owner_.zygote_control_transaction.targets ==
               (std::vector<std::pair<int, std::string>>{{791, "zygote64"}}));
        assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
        assert(injector.active_spawn_owner_.shell_owner_state.spawn_token.empty());
        assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
        assert(injector.active_spawn_owner_.spawn_state.spawn_token.empty());
        assert(injector.active_spawn_owner_.spawn_state.backend ==
               NinjectorSpawnInjector::SpawnBackend::kNone);
    }
}

void TestFinalizeSpawnFailureDoesNotRestoreNormalizedLegacyCompatIntoSpawnState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.clear_zygote_spawn_control = [&](int zygote_pid, const char* spawn_token, bool) {
        trace.calls.push_back(std::string("clear-control-fail:") + std::to_string(zygote_pid));
        trace.calls.push_back(std::string("clear-control-token:") + (spawn_token != nullptr ? spawn_token : ""));
        return false;
    };

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        {},
        [&](int zygote_pid, const std::string& process_name, std::string* error_message) {
            trace.calls.push_back(std::string("uninstall:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        injector.active_spawn_owner_.spawn_state.spawn_token = "legacy-owner-token";
        injector.active_spawn_owner_.shell_owner_state.identifier = "com.demo.target";
        injector.active_spawn_owner_.shell_owner_state.spawn_token = "legacy-owner-token";
        injector.active_spawn_owner_.shell_owner_state.ncore_path = "/data/local/tmp/nook/libncore.so";
        injector.active_spawn_owner_.shell_owner_state.agent_path = "__embedded_agent__";
        injector.active_spawn_owner_.shell_owner_state.materialized_ncore = true;
        injector.active_spawn_owner_.shell_owner_state.materialized_agent = true;
        injector.active_spawn_owner_.shell_owner_state.backend =
            NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;
        injector.active_spawn_owner_.zygote_control_transaction.identifier = "com.demo.target";
        injector.active_spawn_owner_.zygote_control_transaction.spawn_token = "txn-owner-token";
        injector.active_spawn_owner_.zygote_control_transaction.targets.emplace_back(791, "zygote64");
    }

    std::string error_message;
    assert(!injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message == "clear zygote spawn control failed");

    {
        std::lock_guard<std::mutex> lock(injector.transaction_mutex_);
        assert(injector.active_spawn_owner_.zygote_control_transaction.identifier == "com.demo.target");
        assert(injector.active_spawn_owner_.zygote_control_transaction.spawn_token == "txn-owner-token");
        assert(injector.active_spawn_owner_.zygote_control_transaction.targets ==
               (std::vector<std::pair<int, std::string>>{{791, "zygote64"}}));
        assert(injector.active_spawn_owner_.shell_owner_state.identifier.empty());
        assert(injector.active_spawn_owner_.shell_owner_state.spawn_token.empty());
        assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
        assert(injector.active_spawn_owner_.spawn_state.spawn_token.empty());
        assert(injector.active_spawn_owner_.spawn_state.backend ==
               NinjectorSpawnInjector::SpawnBackend::kNone);
    }
}

void TestRestoreOwnedSpawnStateForRetryKeepsLegacyOwnerOutOfSpawnState() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    NinjectorSpawnInjector::SpawnOwnedState legacy_state;
    legacy_state.identifier = "com.demo.target";
    legacy_state.spawn_token = "legacy-retry-token";
    legacy_state.backend = NinjectorSpawnInjector::SpawnBackend::kLegacyNcore;

    injector.RestoreOwnedSpawnStateForRetry(legacy_state);

    assert(injector.active_spawn_owner_.shell_owner_state.identifier == "com.demo.target");
    assert(injector.active_spawn_owner_.shell_owner_state.spawn_token == "legacy-retry-token");
    assert(injector.active_spawn_owner_.shell_owner_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kLegacyNcore);
    assert(injector.active_spawn_owner_.spawn_state.identifier.empty());
    assert(injector.active_spawn_owner_.spawn_state.spawn_token.empty());
    assert(injector.active_spawn_owner_.spawn_state.backend ==
           NinjectorSpawnInjector::SpawnBackend::kNone);
}

void TestInjectFailurePropagatesDetailedError() {
    NinjectorSpawnOps ops;
    const fs::path dir = MakeTempDir("inject_failure_detail");
    const fs::path agent = dir / "libnook-agent.so";
    ops.inject_embedded_agent_by_pid = [](int, const char*, const char*) {
        return false;
    };
    ops.inject_so_by_pid = [](int, const char*, const char*) {
        return false;
    };
    ops.get_inject_error = []() {
        return std::string("attach_process_failed:init_stage");
    };

    NinjectorSpawnInjector injector(NinjectorSpawnConfig{}, ops);

    std::string error_message;
    assert(!injector.InjectAgent(1234, agent.string(), "", &error_message));
    assert(error_message ==
           "inject_embedded_agent_by_pid failed: attach_process_failed:init_stage; "
           "sidecar fallback failed: inject_so_by_pid failed: attach_process_failed:init_stage");
    RemoveAllIgnoringMissing(dir);
}

void TestInjectAgentUsesDirectFileInjectionPath() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("inject_direct_file_path");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(NinjectorSpawnConfig{}, ops);

    std::string error_message;
    assert(injector.InjectAgent(1234, agent.string(), "", &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "inject-memfd:1234"}));
    assert(trace.runtime_dir == dir.string());
    assert(trace.so_path.empty());
    RemoveAllIgnoringMissing(dir);
}

void TestInjectAgentTreatsFileFallbackAsSuccessWithoutRemoteInit() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("inject_file_fallback_success");
    const fs::path agent = dir / "libnook-agent.so";

    ops.inject_embedded_agent_by_pid = [&trace](int pid,
                                                const char* runtime_dir,
                                                const char* ready_token) {
        trace.calls.push_back(std::string("inject-memfd-fail:") + std::to_string(pid));
        trace.runtime_dir = runtime_dir != nullptr ? runtime_dir : "";
        trace.spawn_token = ready_token != nullptr ? ready_token : "";
        return false;
    };
    ops.inject_so_by_pid = [&trace](int pid, const char* so_path, const char* ready_token) {
        trace.calls.push_back(std::string("inject-file:") + std::to_string(pid));
        trace.so_path = so_path != nullptr ? so_path : "";
        trace.spawn_token = ready_token != nullptr ? ready_token : "";
        return true;
    };

    NinjectorSpawnInjector injector(NinjectorSpawnConfig{}, ops);

    std::string error_message;
    assert(injector.InjectAgent(5678, agent.string(), "", &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "inject-memfd-fail:5678",
                               "inject-file:5678"}));
    assert(trace.so_path == agent.string());
    RemoveAllIgnoringMissing(dir);
}

void TestInjectAgentUsesEmbeddedInjectionWithoutTryingSidecarWhenMemfdSucceeds() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    const fs::path dir = MakeTempDir("inject_direct_file_only");
    const fs::path agent = dir / "libnook-agent.so";

    ops.inject_embedded_agent_by_pid = [&trace](int pid,
                                                const char* runtime_dir,
                                                const char* ready_token) {
        trace.calls.push_back(std::string("inject-memfd-unexpected:") + std::to_string(pid));
        trace.runtime_dir = runtime_dir != nullptr ? runtime_dir : "";
        trace.spawn_token = ready_token != nullptr ? ready_token : "";
        return true;
    };
    ops.inject_so_by_pid = [&trace](int pid, const char* so_path, const char* ready_token) {
        trace.calls.push_back(std::string("inject-file:") + std::to_string(pid));
        trace.so_path = so_path != nullptr ? so_path : "";
        trace.spawn_token = ready_token != nullptr ? ready_token : "";
        return true;
    };

    NinjectorSpawnInjector injector(NinjectorSpawnConfig{}, ops);

    std::string error_message;
    assert(injector.InjectAgent(6789, agent.string(), "", &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "inject-memfd-unexpected:6789"}));
    assert(trace.runtime_dir == dir.string());
    assert(trace.so_path.empty());
    RemoveAllIgnoringMissing(dir);
}

void TestDefaultConfigUsesRuntimeDirectoryForNcorePathAndKeepsZygoteControlOptIn() {
#if defined(_WIN32)
    _putenv_s("NOOK_RUNTIME_DIR", "/data/local/tmp/nook-test");
    _putenv_s("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "");
#else
    setenv("NOOK_RUNTIME_DIR", "/data/local/tmp/nook-test", 1);
    unsetenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL");
#endif

    const NinjectorSpawnConfig config = NinjectorSpawnInjector::DefaultConfig();
    assert(config.ncore_path == "/data/local/tmp/nook-test/libncore.so");
    assert(!config.enable_zygote_control);

#if defined(_WIN32)
    _putenv_s("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "0");
#else
    setenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "0", 1);
#endif

    const NinjectorSpawnConfig disabled_config = NinjectorSpawnInjector::DefaultConfig();
    assert(!disabled_config.enable_zygote_control);

#if defined(_WIN32)
    _putenv_s("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "1");
#else
    setenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "1", 1);
#endif

    const NinjectorSpawnConfig enabled_config = NinjectorSpawnInjector::DefaultConfig();
    assert(enabled_config.enable_zygote_control);

#if defined(_WIN32)
    _putenv_s("NOOK_RUNTIME_DIR", "");
    _putenv_s("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "");
#else
    unsetenv("NOOK_RUNTIME_DIR");
    unsetenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL");
#endif
}

void TestEmbeddedZygoteInjectionKeepsAuthoritativeRuntimeDirectory() {
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);

    const fs::path dir = MakeTempDir("zygote_runtime_dir_authoritative");
    SetEnvValue("NOOK_RUNTIME_DIR", dir.string().c_str());
    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .runtime_dir = dir.string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops);

    auto attempt = injector.TrySpawnViaZygoteControl(
        MakeSpawnRequest("com.demo.target", {"--nook-spawn-token=zygote-runtime-dir-token"}),
        "__embedded_agent__");
    assert(attempt.success);
    assert(trace.runtime_dir == dir.string());
    assert(std::find(trace.calls.begin(),
                     trace.calls.end(),
                     "zygote-inject-memfd:791") != trace.calls.end());
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_RUNTIME_DIR", nullptr);
}

void TestLegacySpawnMaterializesEmbeddedNcoreOnDemand() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};

    const fs::path dir = MakeTempDir("materialize_ncore");
    const fs::path ncore = dir / "libncore.so";
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = ncore.string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!fs::exists(ncore));
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target"),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(fs::exists(ncore));
    assert(trace.ncore_path == ncore.string());

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!fs::exists(ncore));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestLegacySpawnPreservesExistingNcoreSidecar() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};

    const fs::path dir = MakeTempDir("preserve_sidecar");
    const fs::path ncore = dir / "libncore.so";
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(ncore, std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = ncore.string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target"),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(fs::exists(ncore));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(fs::exists(ncore));
    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFinalizeForeignOwnerSkipsZygoteControlProbe() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    bool uninstall_called = false;

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = "/data/local/tmp/nook/libncore.so",
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        [&](int zygote_pid,
            const std::string& process_name,
            const std::string& agent_path,
            const std::string& target_package,
            const std::string& spawn_token,
            std::string* error_message) {
            trace.calls.push_back(std::string("install:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "usap64");
            assert(agent_path == "/data/local/tmp/nook/libnook-agent.so");
            assert(target_package == "com.demo.target");
            assert(spawn_token == "spawn-token-foreign-owner");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        [&](int zygote_pid, const std::string& process_name, std::string* error_message) {
            uninstall_called = true;
            trace.calls.push_back(std::string("uninstall:") + std::to_string(zygote_pid));
            assert(process_name == "zygote64" || process_name == "usap64");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        },
        {},
        []() {
            return std::vector<ProcessInfo>{
                {791, "zygote64"},
                {792, "usap64"},
            };
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                           {"--nook-strict-zygote-control",
                                            "--nook-spawn-token=spawn-token-foreign-owner"}),
                          "/data/local/tmp/nook/libnook-agent.so",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install:791",
                               "zygote-inject-memfd:792",
                               "install:792",
                               "start:com.demo.target"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("other.target"), &error_message));
    assert(error_message.empty());
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install:791",
                               "zygote-inject-memfd:792",
                               "install:792",
                               "start:com.demo.target"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "zygote-inject-memfd:791",
                               "install:791",
                               "zygote-inject-memfd:792",
                               "install:792",
                               "start:com.demo.target",
                               "uninstall:791",
                               "uninstall:792"}));

    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFinalizeForeignOwnerDoesNotClearLegacySpawnState() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};
    bool uninstall_called = false;

    const fs::path dir = MakeTempDir("finalize_skip_zygote_probe_when_disabled");
    const fs::path agent = dir / "libnook-agent.so";
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = false,
        },
        ops,
        {},
        [&](int, const std::string&, std::string* error_message) {
            uninstall_called = true;
            trace.calls.push_back("uninstall:791");
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target",
                                          {"--nook-spawn-token=legacy-owner-token"}),
                          agent.string(),
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());

    assert(injector.FinalizeSpawn(MakeSpawnRequest("other.target"), &error_message));
    assert(error_message.empty());
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:legacy-owner-token",
                               "start:com.demo.target"}));

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:legacy-owner-token",
                               "start:com.demo.target",
                               "get_pid:zygote64",
                               "clear:791",
                               "clear-token:legacy-owner-token"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFinalizeWithoutResidualZygoteTargetsSkipsZygoteControlProbe() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    bool uninstall_called = false;

    const fs::path dir = MakeTempDir("finalize_skip_zygote_probe_without_targets");
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
            .enable_zygote_control = true,
        },
        ops,
        {},
        [&](int, const std::string&, std::string* error_message) {
            uninstall_called = true;
            if (error_message != nullptr) {
                error_message->clear();
            }
            return true;
        });

    std::string error_message;
    assert(injector.FinalizeSpawn(MakeSpawnRequest("other.target"), &error_message));
    assert(error_message.empty());
    assert(!uninstall_called);
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "clear:791",
                               "clear-token:"}));

    RemoveAllIgnoringMissing(dir);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
}

void TestFormatZygoteControlSpawnDecisionLogUsesStableShape() {
    assert(FormatZygoteControlSpawnDecisionLog("fallback",
                                               "com.demo.target",
                                               false,
                                               "legacy-or-symbi",
                                               "rpc timeout") ==
           "zygote-control stage=spawn-route event=fallback package=com.demo.target strict=0 fallback=legacy-or-symbi detail=rpc timeout");
}

void TestFormatZygoteControlSpawnDecisionLogUsesUnknownDefaults() {
    assert(FormatZygoteControlSpawnDecisionLog(nullptr,
                                               "",
                                               true,
                                               nullptr,
                                               "") ==
           "zygote-control stage=spawn-route event=unknown package=unknown strict=1 fallback=none");
}

void TestFormatZygoteControlFinalizeDecisionLogUsesStableShape() {
    assert(FormatZygoteControlFinalizeDecisionLog("owned-legacy",
                                                  "com.demo.target",
                                                  "legacy-ncore",
                                                  "fallback clear path") ==
           "zygote-control stage=finalize-route event=owned-legacy package=com.demo.target backend=legacy-ncore detail=fallback clear path");
}

void TestFormatZygoteControlFinalizeDecisionLogUsesUnknownDefaults() {
    assert(FormatZygoteControlFinalizeDecisionLog(nullptr,
                                                  "",
                                                  nullptr,
                                                  "") ==
           "zygote-control stage=finalize-route event=unknown package=unknown backend=unknown");
}

void TestFormatZygoteControlTerminalOutcomeLogUsesStableShape() {
    assert(FormatZygoteControlTerminalOutcomeLog("spawn-result",
                                                 "fail",
                                                 "com.demo.target",
                                                 "zygote-control",
                                                 "legacy",
                                                 "ready-wait",
                                                 "rpc timeout") ==
           "zygote-control stage=spawn-result event=fail package=com.demo.target primary=zygote-control secondary=legacy state=ready-wait detail=rpc timeout");
}

void TestFormatZygoteControlTerminalOutcomeLogUsesUnknownDefaults() {
    assert(FormatZygoteControlTerminalOutcomeLog(nullptr,
                                                 nullptr,
                                                 "",
                                                 nullptr,
                                                 nullptr,
                                                 nullptr,
                                                 "") ==
           "zygote-control stage=unknown event=unknown package=unknown primary=unknown secondary=none state=unknown");
}

void TestFormatZygoteControlLifecycleStageLogUsesStableShape() {
    assert(FormatZygoteControlLifecycleStageLog("spawn-lifecycle",
                                                "enter",
                                                "com.demo.target",
                                                "launch-app",
                                                "arm complete") ==
           "zygote-control stage=spawn-lifecycle event=enter package=com.demo.target state=launch-app detail=arm complete");
}

void TestFormatZygoteControlLifecycleStageLogUsesUnknownDefaults() {
    assert(FormatZygoteControlLifecycleStageLog(nullptr,
                                                nullptr,
                                                "",
                                                nullptr,
                                                "") ==
           "zygote-control stage=unknown event=unknown package=unknown state=unknown");
}

void TestLegacySpawnMaterializesEmbeddedAgentWhenSentinelIsUsed() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", "1");
    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.spawn_symbi_embedded = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};

    const fs::path dir = MakeTempDir("materialize_embedded_agent");
    {
        std::ofstream stream(dir / "libncore.so", std::ios::binary);
        stream << "existing";
    }
    SetEnvValue("NOOK_RUNTIME_DIR", dir.string().c_str());

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(injector.Spawn(MakeSpawnRequest("com.demo.target", {"--nook-spawn-token=spawn-token-embedded-agent"}),
                          "__embedded_agent__",
                          &pid,
                          &error_message));
    assert(pid == 1);
    assert(error_message.empty());
    assert(!fs::exists(dir / "libnook-agent.so"));
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64",
                               "prepare:791",
                               "token:spawn-token-embedded-agent",
                               "start:com.demo.target"}));
    assert(trace.so_path == "__embedded_agent__");

    assert(injector.FinalizeSpawn(MakeSpawnRequest("com.demo.target"), &error_message));
    assert(error_message.empty());
    assert(!fs::exists(dir / "libnook-agent.so"));

    SetEnvValue("NOOK_RUNTIME_DIR", nullptr);
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    RemoveAllIgnoringMissing(dir);
}

void TestLegacySpawnDoesNotFallbackToSidecarByDefault() {
    SetEnvValue("NOOK_ALLOW_NCORE_SIDECAR_FALLBACK", nullptr);
    SetEnvValue("NOOK_NCORE_PATH", nullptr);

    TraceState trace;
    NinjectorSpawnOps ops = MakeBaseOps(&trace);
    ops.spawn_symbi = {};
    ops.inject_so_by_pid = {};
    ops.inject_zygote_so_by_pid = {};

    const fs::path dir = MakeTempDir("legacy_no_sidecar_fallback");
    const fs::path agent = dir / "libnook-agent.so";

    NinjectorSpawnInjector injector(
        NinjectorSpawnConfig{
            .ncore_path = (dir / "libncore.so").string(),
            .spawn_source_process = "zygote64",
        },
        ops);

    int pid = 0;
    std::string error_message;
    assert(!injector.Spawn(MakeSpawnRequest("com.demo.target"),
                           agent.string(),
                           &pid,
                           &error_message));
    assert(pid == 0);
    assert(error_message == "prepare_spawn_in_zygote_embedded failed");
    assert((trace.calls == std::vector<std::string>{
                               "get_pid:zygote64"}));
    RemoveAllIgnoringMissing(dir);
}

}  // namespace

int main() {
    TestSpawnStrictZygoteControlPrefersZygoteControlPath();
    TestSpawnStrictZygoteControlPassesStrictFlagToArmControl();
    TestSpawnDefaultStablePathSkipsZygoteControlWhenEnabled();
    TestSpawnZygoteControlArmsBothZygoteFamiliesWhenPresent();
    TestSpawnReinjectsWhenMonitorReadyButSessionMissing();
    TestSpawnSkipsReinjectWhenMonitorReadyAndSessionPresent();
    TestSpawnZygoteControlStartFailureClearsArmedControlImmediately();
    TestSpawnZygoteControlArmFailureDoesNotFallbackByDefault();
    TestSpawnServerStrictEnvDoesNotPromoteDefaultSpawnRoute();
    TestSpawnStrictZygoteControlAbortsWhenInstallFails();
    TestSpawnStrictZygoteControlAbortsWhenControlFails();
    TestSpawnStrictZygoteControlRequestArgAbortsOnSoftInstallFailure();
    TestSpawnStrictHelperLocalControlSkipsRpcInstallButStillUninstalls();
    TestSpawnStrictRequestEmbeddedZygoteControlUsesHelperLocalControl();
    TestSpawnDefaultEmbeddedAgentStaysOnStableLegacyRouteWhenZygoteControlIsEnabled();
    TestSpawnOutcomeFallbackClassificationUsesOutcomeStateInsteadOfGlobalState();
    TestSpawnOutcomeFallbackClassificationPrefersFailedTransactionState();
    TestSpawnOutcomeAbortFormatsOutcomeStateInsteadOfGlobalState();
    TestSpawnOutcomeAbortFormatsFailedTransactionStateInsteadOfDetailFallback();
    TestResolveCurrentZygoteControlStatePrefersRecordedFailureState();
    TestResolveCurrentZygoteControlStateFallsBackToLifecycleThenDetail();
    TestResolveTransactionZygoteControlStatePrefersTransactionState();
    TestResolveTransactionZygoteControlStatePrefersDetailBeforeRecorderFallback();
    TestSuccessfulZygoteControlSpawnCommitsTransactionState();
    TestFinalizeZygoteControlSpawnWritesStateBackToTransaction();
    TestFinalizeZygoteControlHelperOnlyLocalControlStopsOnUninstallFailure();
    TestFinalizeZygoteControlHelperOnlyLocalControlClearsRecorderOnSuccess();
    TestSpawnOutcomeCanCarryFailedZygoteControlTransactionState();
    TestCommitPendingSpawnNormalizesZygoteControlShellOwnerState();
    TestCommitPendingSpawnSeparatesAuthoritativeAndCompatibilityShellState();
    TestCommitPendingSpawnKeepsSymbiOwnerOutOfShellCompatibilityState();
    TestCommitPendingSpawnDoesNotPromoteCompatibilitySpawnStateIntoShellOwner();
    TestCommitPendingSpawnPreservesExplicitShellOwnerState();
    TestFailedZygoteControlTransactionSnapshotTracksReadyWaitFailure();
    TestSpawnStrictZygoteControlInstallFailureDoesNotUseLegacyClearSideChannel();
    TestSnapshotCurrentZygoteControlTransactionStateCopiesRecorderAndTargets();
    TestFailZygoteControlSpawnSnapshotsTransactionAndError();
    TestFailZygoteControlSpawnInfersStateWithoutRecorder();
    TestFailZygoteControlSpawnClearsGlobalRecorderAfterSnapshot();
    TestTrySpawnViaZygoteControlReturnsStructuredFailureResult();
    TestApplyFailedZygoteControlOutcomeSeedsStateAndTransaction();
    TestApplyFailedZygoteControlOutcomeAllowsFallbackWhenStateIsSoft();
    TestApplyFailedZygoteControlAttemptResultSeedsOutcome();
    TestApplySuccessfulZygoteControlAttemptResultSeedsCommit();
    TestApplyZygoteControlRouteSuccessCommitsOutcome();
    TestApplyZygoteControlRouteAbortsStrictFailure();
    TestApplyZygoteControlRouteAbortKeepsResidualTransactionScopedToAttempt();
    TestApplySymbiRouteSuccessCommitsOutcome();
    TestApplySymbiRouteFailureAllowsFallback();
    TestApplyLegacyRouteSuccessCommitsOutcome();
    TestApplyLegacyRouteFailureCapturesErrorWhenProbeOnly();
    TestApplySpawnRoutingSucceedsOnZygoteControlSuccess();
    TestApplySpawnRoutingAttemptsOnDefaultStablePathSkipsZygoteControl();
    TestApplyZygoteControlRoutingCommitsSuccessfulRoute();
    TestApplyZygoteControlRoutingDefersSoftFailureToFallback();
    TestApplyZygoteControlRoutingStrictSoftFailureAbortsWithoutFallback();
    TestApplyZygoteControlRoutingStrictHelperLocalControlCommitsRoute();
    TestApplyTerminalSpawnOutcomeClassifiesThenFinalizes();
    TestCompleteSpawnAfterRoutingCommitsCompletedRoutePath();
    TestCompleteSpawnAfterRoutingFinishesDeferredTerminalPath();
    TestAdmitSpawnRequestRejectsMatchingActiveOwner();
    TestAdmitSpawnRequestRejectsForeignActiveOwner();
    TestAdmitSpawnRequestIgnoresCompatibilityShellWithoutBackend();
    TestAdmitSpawnRequestRecognizesSymbiOwnerFromSpawnState();
    TestAdmitSpawnRequestRejectsMatchingResidualZygoteTransaction();
    TestAdmitSpawnRequestRejectsForeignResidualZygoteTransaction();
    TestBuildSpawnExecutionPolicyDefaultStablePath();
    TestBuildSpawnExecutionPolicyLegacyPreferredWhenSymbiPreferenceDisabled();
    TestBuildSpawnExecutionPolicyExplicitSymbiRequest();
    TestBuildSpawnExecutionPolicyStrictZygoteControlOverridesExplicitSymbiRequest();
    TestBuildSpawnExecutionStateCarriesPolicyAndAttempt();
    TestBuildSpawnExecutionStateDoesNotTriggerZygoteControlAttempt();
    TestBeginSpawnRoutingSeedsRunningRoutingState();
    TestEnterZygoteControlRouteSeedsEnteredRouteContext();
    TestSkipZygoteControlRouteSeedsSkippedRouteContext();
    TestAbortZygoteControlRouteKeepsRunningEnteredContext();
    TestCommitZygoteControlRouteAdvancesCommittedRouteContext();
    TestDeferZygoteControlRouteToFallbackKeepsRunningUntilAdvance();
    TestAdvancePastZygoteControlRouteMovesRoutingProgressForward();
    TestCommitNonZygoteControlRouteCommitsSymbiContext();
    TestCommitNonZygoteControlRouteCommitsLegacyContext();
    TestTransitionSpawnOwnershipStateAllowsSingleCommitOwnership();
    TestTransitionSpawnOwnershipStateRejectsOwnerRewrite();
    TestResolveOwnershipStateFromBackendMapsFinalizeOwners();
    TestBuildPendingSpawnCommitSeedsZygoteControlOwnedRecord();
    TestBuildPendingSpawnCommitKeepsNonZygoteOwnersSessionLocal();
    TestBuildPendingSpawnCommitKeepsSymbiOwnerInSpawnState();
    TestBuildPendingSpawnCommitStripsLegacyNcoreResidueFromSymbiOwner();
    TestApplySuccessfulRouteCommitSeedsLegacyFallbackAndOwner();
    TestApplySuccessfulRouteCommitSeedsZygoteOwnerTransaction();
    TestApplyTerminalOutcomeClassificationSeedsFallbackFailurePair();
    TestApplyTerminalOutcomeClassificationSeedsBackendUnavailablePair();
    TestApplyFailedZygoteControlClassificationSeedsAbortOutcome();
    TestApplyFailedZygoteControlClassificationSeedsFallbackOutcome();
    TestApplyFailedZygoteControlClassificationInfersTransactionStateFromDetail();
    TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingOwner();
    TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingSymbiOwnerOnly();
    TestReleaseActiveOwnerAfterDeferredRoutingPreservesForeignOwner();
    TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingResidualTransactionOnly();
    TestReleaseActiveOwnerAfterDeferredRoutingClearsMatchingSpawnOwnerOnly();
    TestReleaseActiveOwnerAfterDeferredRoutingClearsZygoteOwnerShellByTransactionMatch();
    TestFinalizeWithoutOwnedBackendReturnsForeignOwnerSuccess();
    TestFinalizeWithoutOwnedBackendDoesNotUseGlobalRecorderWithoutResidualTransaction();
    TestFinalizeSpawnTreatsResidualZygoteTransactionAsOwnedPath();
    TestBuildFinalizeSessionCapturesOwnedOwnerContext();
    TestBuildFinalizeSessionCapturesForeignOwnerAndResidualFlags();
    TestTakeActiveOwnerForFinalizeExtractsUnifiedOwnerRecord();
    TestTakeActiveOwnerForFinalizeClearsTokenOnlyCompatWhenTransactionOwnsRequest();
    TestTakeActiveOwnerForFinalizeIgnoresCompatibilitySpawnStateAsOwner();
    TestTakeActiveOwnerForFinalizePreservesForeignResidualTransaction();
    TestTakeActiveOwnerForFinalizePromotesResidualTransactionToOwnedOwner();
    TestTakeActiveOwnerForFinalizePrefersMatchingZygoteTransactionOverShellBackend();
    TestTakeActiveOwnerForFinalizeNormalizesReturnedShellBackendWhenTransactionOwnsRequest();
    TestApplySpawnRoutingSnapshotRejectsCommittedSymbiWithoutOwnedState();
    TestApplySpawnRoutingSnapshotRejectsCommittedLegacyWithoutOwnedState();
    TestBuildSpawnExecutionStateSeedsPhaseReason();
    TestTransitionSpawnExecutionPhaseAllowsLegalTransitions();
    TestTransitionSpawnExecutionPhaseRejectsIllegalTransitions();
    TestTransitionSpawnExecutionPhaseRecordsReason();
    TestApplySpawnRoutingSnapshotRejectsInvalidCommittedStateJump();
    TestApplySpawnRoutingSnapshotRejectsInvalidProgressJump();
    TestApplySpawnRoutingSnapshotRejectsRouteStepBeforeProgressBegins();
    TestApplySpawnRoutingSnapshotRejectsInvalidZygoteControlRouteCommitJump();
    TestApplySpawnRoutingSnapshotRejectsInvalidZygoteControlRouteDeferredJump();
    TestApplySpawnRoutingSnapshotRejectsZygoteControlEnteredWithoutZygoteControlStep();
    TestApplySpawnRoutingSnapshotRejectsZygoteControlCommittedWithoutCommittedRoutingState();
    TestApplySpawnRoutingSnapshotRejectsZygoteControlSkippedWithoutSkippedWindow();
    TestApplySpawnRoutingSnapshotRejectsZygoteControlDeferredWithoutRunningZygoteControlContext();
    TestApplySpawnRoutingSnapshotRejectsZygoteControlAbortedWithoutRunningZygoteControlContext();
    TestApplySpawnRoutingSnapshotRejectsZygoteControlSkippedToEnteredTransition();
    TestApplySpawnRoutingSnapshotRejectsZygoteControlEnteredToSkippedTransition();
    TestApplySpawnRoutingSnapshotRejectsCommittedZygoteControlWithoutAlignedProgressAndStep();
    TestApplySpawnRoutingSnapshotRejectsCommittedLegacyWithoutAlignedProgressAndStep();
    TestApplySpawnRoutingSnapshotRejectsCommittedSymbiWithoutAlignedProgressAndStep();
    TestApplySpawnRoutingSnapshotRejectsDeferredTerminalWithoutLegacyProgress();
    TestSpawnSuccessReturnsArmedLegacyPidSentinel();
    TestSpawnDefaultStablePathSkipsZygoteControlEvenWhenSupportIsEnabled();
    TestSpawnReinjectsWhenOnlyPreexistingControlReadySessionExistsWithoutOwnedTarget();
    TestSpawnDefaultSymbiWhenZygoteControlIsDisabled();
    TestSpawnDefaultStablePathDoesNotTrySymbiWhenPreferenceDisabled();
    TestSpawnDefaultPathPrefersSymbiByDefault();
    TestSpawnDefaultPathFallsBackToLegacyWhenSymbiFails();
    TestSpawnExplicitSymbiPrefersSymbiBackend();
    TestSpawnExplicitSymbiFailureDoesNotFallbackToLegacyBackend();
    TestSpawnSymbiMaterializesEmbeddedAgentOnDemand();
    TestSpawnSymbiUsesEmbeddedBackendWithoutMaterializingAgentFile();
    TestSpawnDefaultConfigKeepsRuntimeDirectoryArtifacts();
    TestSpawnDefaultConfigUsesSymbiByDefault();
    TestSpawnZygoteControlPathDoesNotEnterLegacyPrepareWhenInstallCallbackSucceeds();
    TestFinalizeSpawnDoesNotFallbackToLegacyClearAfterZygoteControlCommit();
    TestSpawnCanDisableZygoteControlExplicitly();
    TestSpawnFailureFromPrepareStopsFlow();
    TestSpawnFailureFromPrepareWithoutDetailKeepsBaseMessage();
    TestFailedRespawnDoesNotClearExistingLegacyOwnerState();
    TestDuplicateActiveSpawnIsRejectedAndPreservesOwnerState();
    TestDifferentIdentifierSpawnIsRejectedWhileOwnerIsActive();
    TestActiveOwnerRejectsSpawnBeforeZygoteControlAttempt();
    TestSpawnSymbiFailureStopsFlow();
    TestFinalizeLegacyFailurePropagatesClearError();
    TestFinalizeOwnedResidualTransactionFormatsLocalZygoteControlStateInsteadOfGlobalState();
    TestFinalizeSpawnFailurePreservesMatchingOwnedZygoteTransactionForRetry();
    TestFinalizeSpawnFailureDoesNotRestoreNormalizedLegacyCompatIntoSpawnState();
    TestRestoreOwnedSpawnStateForRetryKeepsLegacyOwnerOutOfSpawnState();
    TestInjectFailurePropagatesDetailedError();
    TestFinalizeForeignOwnerSkipsZygoteControlProbe();
    TestFinalizeForeignOwnerDoesNotClearLegacySpawnState();
    TestFinalizeWithoutResidualZygoteTargetsSkipsZygoteControlProbe();
    TestFormatZygoteControlSpawnDecisionLogUsesStableShape();
    TestFormatZygoteControlSpawnDecisionLogUsesUnknownDefaults();
    TestFormatZygoteControlFinalizeDecisionLogUsesStableShape();
    TestFormatZygoteControlFinalizeDecisionLogUsesUnknownDefaults();
    TestFormatZygoteControlTerminalOutcomeLogUsesStableShape();
    TestFormatZygoteControlTerminalOutcomeLogUsesUnknownDefaults();
    TestFormatZygoteControlLifecycleStageLogUsesStableShape();
    TestFormatZygoteControlLifecycleStageLogUsesUnknownDefaults();
    TestInjectAgentUsesDirectFileInjectionPath();
    TestInjectAgentTreatsFileFallbackAsSuccessWithoutRemoteInit();
    TestInjectAgentUsesEmbeddedInjectionWithoutTryingSidecarWhenMemfdSucceeds();
    TestDefaultConfigUsesRuntimeDirectoryForNcorePathAndKeepsZygoteControlOptIn();
    TestEmbeddedZygoteInjectionKeepsAuthoritativeRuntimeDirectory();
    TestLegacySpawnMaterializesEmbeddedNcoreOnDemand();
    TestLegacySpawnPreservesExistingNcoreSidecar();
    TestLegacySpawnMaterializesEmbeddedAgentWhenSentinelIsUsed();
    TestLegacySpawnDoesNotFallbackToSidecarByDefault();
    return 0;
}
