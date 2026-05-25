#include "../src/communication/handler/message_dispatcher.h"
#include "../src/communication/io/io_loop.h"
#include "../src/communication/protocol/frame.h"
#include "../src/communication/protocol/message_types.h"
#include "../src/communication/protocol/messages.h"
#include "../src/communication/session/session_manager.h"
#include "../src/communication/transport/tcp_transport.h"
#include "../src/communication/transport/unix_transport.h"
#include "ninjector_compat.h"
#include "ninjector_spawn_injector.h"
#include "process_manager.h"
#include "server_handlers.h"
#include "generated/nook_embedded_agent_blob.h"
#include "generated/nook_embedded_ncore_blob.h"
#include "generated/nook_embedded_zygote_helper_blob.h"
#include "server_runtime.h"
#include "session_registry.h"
#include "zygote_control_rpc.h"
#include <sstream>

#include <memory>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <thread>

#if defined(__ANDROID__)
#include <android/log.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>
#define NOOK_SERVER_MAIN_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookServer", __VA_ARGS__))
#define NOOK_SERVER_MAIN_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookServer", __VA_ARGS__))
#else
#define NOOK_SERVER_MAIN_LOGI(...) ((void)0)
#define NOOK_SERVER_MAIN_LOGE(...) ((void)0)
#endif

using namespace nook::comm;
using namespace nook::server;

namespace {

constexpr const char* kEmbeddedAgentSentinel = "__embedded_agent__";
constexpr const char* kEnableZygoteControlFlag = "--enable-zygote-control";
constexpr const char* kDisableZygoteControlFlag = "--disable-zygote-control";

void LogEmbeddedArtifactInfo() {
    if (nook::server::kNookEmbeddedAgentBlobSize > 0) {
        NOOK_SERVER_MAIN_LOGI(
            "embedded agent blob size=%u source_size=%u sha256=%s source=%s built_utc=%s",
            nook::server::kNookEmbeddedAgentBlobSize,
            nook::server::kNookEmbeddedAgentSourceFileSize,
            nook::server::kNookEmbeddedAgentSourceSha256,
            nook::server::kNookEmbeddedAgentSourcePath,
            nook::server::kNookEmbeddedAgentSourceLastWriteUtc);
    } else {
        NOOK_SERVER_MAIN_LOGI("embedded agent blob disabled");
    }

    if (nook::server::kNookEmbeddedZygoteHelperBlobSize > 0) {
        NOOK_SERVER_MAIN_LOGI(
            "embedded zygote helper blob size=%u source_size=%u sha256=%s source=%s built_utc=%s",
            nook::server::kNookEmbeddedZygoteHelperBlobSize,
            nook::server::kNookEmbeddedZygoteHelperSourceFileSize,
            nook::server::kNookEmbeddedZygoteHelperSourceSha256,
            nook::server::kNookEmbeddedZygoteHelperSourcePath,
            nook::server::kNookEmbeddedZygoteHelperSourceLastWriteUtc);
    } else {
        NOOK_SERVER_MAIN_LOGI("embedded zygote helper blob disabled");
    }

    if (nook::server::kNookEmbeddedNcoreBlobSize > 0) {
        NOOK_SERVER_MAIN_LOGI(
            "embedded ncore blob size=%u source_size=%u sha256=%s source=%s built_utc=%s",
            nook::server::kNookEmbeddedNcoreBlobSize,
            nook::server::kNookEmbeddedNcoreSourceFileSize,
            nook::server::kNookEmbeddedNcoreSourceSha256,
            nook::server::kNookEmbeddedNcoreSourcePath,
            nook::server::kNookEmbeddedNcoreSourceLastWriteUtc);
    } else {
        NOOK_SERVER_MAIN_LOGI("embedded ncore blob disabled");
    }
}

bool LooksLikeDynamicLinkerPath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    const std::string::size_type slash = path.find_last_of("/\\");
    const std::string file_name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    return file_name == "linker64" ||
           file_name == "linker" ||
           file_name == "ld-android.so";
}

enum class ZygoteControlLaunchMode {
    kDefault = 0,
    kEnabled,
    kDisabled,
};

ZygoteControlLaunchMode ParseZygoteControlLaunchMode(int argc, char** argv) {
    if (argc <= 0 || argv == nullptr) {
        return ZygoteControlLaunchMode::kDefault;
    }

    ZygoteControlLaunchMode mode = ZygoteControlLaunchMode::kDefault;
    for (int index = 1; index < argc; ++index) {
        const char* arg = argv[index];
        if (arg == nullptr || arg[0] == '\0') {
            continue;
        }
        if (std::strcmp(arg, kEnableZygoteControlFlag) == 0) {
            mode = ZygoteControlLaunchMode::kEnabled;
            continue;
        }
        if (std::strcmp(arg, kDisableZygoteControlFlag) == 0) {
            mode = ZygoteControlLaunchMode::kDisabled;
            continue;
        }
    }
    return mode;
}

void ApplyZygoteControlLaunchMode(ZygoteControlLaunchMode mode) {
    switch (mode) {
    case ZygoteControlLaunchMode::kEnabled:
        setenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "1", 1);
        NOOK_SERVER_MAIN_LOGI("zygote-control launch mode=enabled");
        break;
    case ZygoteControlLaunchMode::kDisabled:
        setenv("NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL", "0", 1);
        NOOK_SERVER_MAIN_LOGI("zygote-control launch mode=disabled");
        break;
    case ZygoteControlLaunchMode::kDefault:
    default:
        break;
    }
}

#if !defined(_WIN32)
volatile sig_atomic_t g_nook_server_running = 1;

void HandleStopSignal(int) {
    g_nook_server_running = 0;
}

void WaitForShutdownSignal() {
    struct sigaction action {};
    action.sa_handler = HandleStopSignal;
    sigemptyset(&action.sa_mask);

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    while (g_nook_server_running != 0) {
        pause();
    }
}
#endif

bool ReleaseSpawnGate(SessionRegistry* registry, int pid) {
    if (registry == nullptr || pid <= 0) {
        return false;
    }

    auto send_resume = [pid](Session* agent, const char* role) -> bool {
        if (agent == nullptr) {
            return false;
        }

        ResumeRequest request;
        request.pid = static_cast<uint32_t>(pid);

        Frame response_frame;
        Frame request_frame(MessageType::kResumeRequest,
                            agent->NextMsgId(),
                            EncodeResumeRequest(request));
        if (!agent->SendRequest(request_frame, &response_frame, 5000)) {
            NOOK_SERVER_MAIN_LOGE("release gate failed pid=%d role=%s error=request timeout",
                                  pid,
                                  role != nullptr ? role : "unknown");
            return false;
        }

        ResumeResponse response;
        if (response_frame.GetType() != MessageType::kResumeResponse ||
            !DecodeResumeResponse(response_frame.GetPayload().data(),
                                  response_frame.GetPayload().size(),
                                  &response)) {
            NOOK_SERVER_MAIN_LOGE("release gate failed pid=%d role=%s error=decode response",
                                  pid,
                                  role != nullptr ? role : "unknown");
            return false;
        }

        if (response.error.code != 0) {
            NOOK_SERVER_MAIN_LOGE("release gate failed pid=%d role=%s error=%s",
                                  pid,
                                  role != nullptr ? role : "unknown",
                                  response.error.message.c_str());
            return false;
        }

        NOOK_SERVER_MAIN_LOGI("release gate ok pid=%d role=%s",
                              pid,
                              role != nullptr ? role : "unknown");
        return true;
    };

    Session* control_agent = registry->FindControlReadyAgentSessionByPid(pid);
    Session* authoritative_agent = registry->FindAuthoritativeAgentSessionByPid(pid);
    if (control_agent != nullptr &&
        authoritative_agent != nullptr &&
        control_agent != authoritative_agent) {
        // Strict helper-only zygote-control late-promotes a full runtime agent from a
        // separate DSO. The spawned process gate is still owned by the helper-side
        // control session that installed the original wait, so releasing through the
        // late runtime session would only wake that DSO's local state and leave the
        // real app bootstrap waiter blocked.
        NOOK_SERVER_MAIN_LOGI("release gate target pid=%d session=control-owner split-runtime=1", pid);
        return send_resume(control_agent, "control-owner");
    }

    Session* agent = ResolveSpawnGateAgentSession(registry, pid);
    if (agent != nullptr) {
        NOOK_SERVER_MAIN_LOGI("release gate target pid=%d session=identity-aware", pid);
    }
    if (agent == nullptr) {
        NOOK_SERVER_MAIN_LOGE("release gate failed pid=%d error=no authoritative-or-control-ready agent session", pid);
        return false;
    }

    return send_resume(agent, "identity-aware");
}

int FindProcessPidByName(ProcessManager* process_manager, const std::string& process_name) {
    if (process_manager == nullptr || process_name.empty()) {
        return -1;
    }

    const std::vector<ProcessInfo> processes = process_manager->EnumerateProcesses();
    for (const ProcessInfo& process : processes) {
        if (process.name == process_name) {
            return process.pid;
        }
    }
    return -1;
}

bool HasOwnedZygoteControlTarget(SessionRegistry* registry,
                                 int pid,
                                 const std::string& process_name) {
    return registry != nullptr &&
           !process_name.empty() &&
           registry->IsOwnedZygoteControlTarget(pid, process_name);
}

void CleanupStaleZygoteHelpersAtStartup(ProcessManager* process_manager) {
    if (process_manager == nullptr) {
        return;
    }

    const char* const kProcessNames[] = {
        "zygote64",
        "zygote",
        "usap64",
        "usap32",
    };

    for (const char* process_name : kProcessNames) {
        if (process_name == nullptr || process_name[0] == '\0') {
            continue;
        }

        const int pid = FindProcessPidByName(process_manager, process_name);
        if (pid <= 0) {
            continue;
        }

        if (!nook::server::ninjector::HasEmbeddedZygoteControlResidue(pid)) {
            continue;
        }

        NOOK_SERVER_MAIN_LOGI("zygote-control startup stale-clean begin process=%s pid=%d",
                              process_name,
                              pid);
        if (nook::server::ninjector::UninstallEmbeddedZygoteControlHooksByPid(pid)) {
            NOOK_SERVER_MAIN_LOGI("zygote-control startup stale-clean ok process=%s pid=%d",
                                  process_name,
                                  pid);
        } else {
            NOOK_SERVER_MAIN_LOGI("zygote-control startup stale-clean skip process=%s pid=%d error=%s",
                                  process_name,
                                  pid,
                                  nook::server::ninjector::GetLastInjectError().empty()
                                      ? "unavailable"
                                      : nook::server::ninjector::GetLastInjectError().c_str());
        }
    }
}

void CleanupZygoteControlBeforeShutdown(SessionRegistry* registry, ProcessManager* process_manager) {
    if (registry == nullptr) {
        return;
    }

    const char* const kProcessNames[] = {
        "zygote64",
        "zygote",
        "usap64",
        "usap32",
    };

    for (const char* process_name : kProcessNames) {
        if (process_name == nullptr || process_name[0] == '\0') {
            continue;
        }

        std::string error_message;
        const int pid = FindProcessPidByName(process_manager, process_name);
        if (!HasOwnedZygoteControlTarget(registry, pid, process_name)) {
            NOOK_SERVER_MAIN_LOGI("zygote-control shutdown cleanup skip-inactive process=%s pid=%d",
                                  process_name,
                                  pid);
            continue;
        }
        if (UninstallZygoteForkHook(registry, pid, process_name, &error_message)) {
            NOOK_SERVER_MAIN_LOGI("zygote-control shutdown cleanup ok process=%s pid=%d",
                                  process_name,
                                  pid);
        } else {
            NOOK_SERVER_MAIN_LOGI("zygote-control shutdown cleanup skip process=%s pid=%d error=%s",
                                  process_name,
                                  pid,
                                  error_message.empty() ? "not-active-or-unavailable"
                                                        : error_message.c_str());
        }
    }
}

std::string DescribeCurrentException() {
    try {
        const std::exception_ptr current = std::current_exception();
        if (current == nullptr) {
            return "no active exception";
        }
        std::rethrow_exception(current);
    } catch (const std::exception& ex) {
        return ex.what();
    } catch (...) {
        return "non-std exception";
    }
}

void InstallTerminateLogger() {
    std::set_terminate([]() {
        const std::string detail = DescribeCurrentException();
        NOOK_SERVER_MAIN_LOGE("server terminate called detail=%s", detail.c_str());
        std::abort();
    });
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return 0;
#else
    if (geteuid() != 0) {
        NOOK_SERVER_MAIN_LOGE("server requires root privileges for ptrace-based spawn/attach; uid=%d", geteuid());
        return 3;
    }

    InstallTerminateLogger();

    try {
        LogEmbeddedArtifactInfo();

        IoLoop io_loop;
        SessionManager host_sessions;
        SessionManager agent_sessions;
        SessionRegistry registry;
        MessageDispatcher dispatcher;
        ProcessManager process_manager;
        const char* env_agent_path = std::getenv("NOOK_AGENT_PATH");
        const char* env_runtime_dir = std::getenv("NOOK_RUNTIME_DIR");
        const char* env_ncore_path = std::getenv("NOOK_NCORE_PATH");
        std::string executable_path;
        const ZygoteControlLaunchMode zygote_control_launch_mode =
            ParseZygoteControlLaunchMode(argc, argv);
        if (argv != nullptr) {
            if (argc > 1 &&
                argv[1] != nullptr &&
                argv[1][0] != '\0' &&
                argc > 0 &&
                argv[0] != nullptr &&
                LooksLikeDynamicLinkerPath(argv[0])) {
                executable_path = argv[1];
            } else if (argc > 0 && argv[0] != nullptr && argv[0][0] != '\0') {
                executable_path = argv[0];
            }
        }
        if (executable_path.empty()) {
            executable_path = DetectCurrentExecutablePath();
        }
        ApplyZygoteControlLaunchMode(zygote_control_launch_mode);
        const std::string runtime_dir = ResolveRuntimeDirectoryFromEnvironmentAgentAndExecutable(
            env_runtime_dir,
            env_agent_path,
            executable_path.empty() ? nullptr : executable_path.c_str());
        const std::string socket_path = BuildSocketPathFromRuntimeDirectory(runtime_dir);
        std::string ncore_path =
            ResolveNcorePathFromEnvironmentAndRuntimeDirectory(env_ncore_path, runtime_dir);
        std::string agent_path = ResolveAgentPathFromEnvironmentAndExecutable(
            env_agent_path,
            executable_path.empty() ? nullptr : executable_path.c_str());
        if (!runtime_dir.empty()) {
            setenv("NOOK_RUNTIME_DIR", runtime_dir.c_str(), 1);
        }
        CleanupStaleZygoteHelpersAtStartup(&process_manager);
        if (env_ncore_path != nullptr && env_ncore_path[0] != '\0') {
            NOOK_SERVER_MAIN_LOGI("explicit ncore path selected path=%s", ncore_path.c_str());
        } else if (nook::server::kNookEmbeddedNcoreBlobSize == 0) {
            const bool sidecar_ncore_exists = std::ifstream(ncore_path, std::ios::binary).good();
            if (sidecar_ncore_exists) {
                setenv("NOOK_NCORE_PATH", ncore_path.c_str(), 1);
                NOOK_SERVER_MAIN_LOGI("sidecar ncore fallback path=%s", ncore_path.c_str());
            }
        }
        NinjectorSpawnInjector injector(
            NinjectorSpawnInjector::DefaultConfig(),
            NinjectorSpawnInjector::MakeDefaultOps(),
            [&registry](int zygote_pid,
                        const std::string& process_name,
                        const std::string& agent_path,
                        const std::string& target_package,
                        const std::string& spawn_token,
                        std::string* error_message) {
                return InstallZygoteForkHook(&registry,
                                             zygote_pid,
                                             process_name,
                                             agent_path,
                                             target_package,
                                             spawn_token,
                                             error_message);
            },
            [&registry](int zygote_pid,
                        const std::string& process_name,
                        std::string* error_message) {
                return UninstallZygoteForkHook(&registry,
                                               zygote_pid,
                                               process_name,
                                               error_message);
            },
            [&registry](int zygote_pid, const std::string& process_name) {
                return HasOwnedZygoteControlTarget(&registry, zygote_pid, process_name);
            },
            [&process_manager]() {
                return process_manager.EnumerateProcesses();
            });
        if (env_agent_path != nullptr && env_agent_path[0] != '\0') {
            NOOK_SERVER_MAIN_LOGI("explicit agent path selected path=%s", agent_path.c_str());
        } else if (nook::server::kNookEmbeddedAgentBlobSize > 0) {
            agent_path = kEmbeddedAgentSentinel;
            NOOK_SERVER_MAIN_LOGI("embedded agent selected mode=memfd runtime_dir=%s",
                                  runtime_dir.empty() ? "(empty)" : runtime_dir.c_str());
        } else {
            const bool sidecar_agent_exists = std::ifstream(agent_path, std::ios::binary).good();
            if (sidecar_agent_exists) {
                setenv("NOOK_AGENT_PATH", agent_path.c_str(), 1);
                NOOK_SERVER_MAIN_LOGI("sidecar agent fallback path=%s", agent_path.c_str());
            }
        }

        RegisterServerHandlers(&dispatcher,
                               &registry,
                               &injector,
                               ServerHandlerConfig{
                                   .agent_path = agent_path,
                                   .enumerate_processes = [&process_manager]() {
                                       return process_manager.EnumerateProcesses();
                                   },
                                   .enumerate_apps = [&process_manager]() {
                                       return process_manager.EnumerateApps();
                                   },
                                   .suspend_process = {},
                                   .resume_process = [&registry](int pid) {
                                       return ReleaseSpawnGate(&registry, pid);
                                   },
                               });

        TcpListener host_listener(27042);
        if (!host_listener.Listen()) {
            NOOK_SERVER_MAIN_LOGE("host listener start failed port=27042");
            return 1;
        }

        UnixListener agent_listener(socket_path);
        if (!agent_listener.Listen()) {
            NOOK_SERVER_MAIN_LOGE("agent listener start failed path=%s", socket_path.c_str());
            return 2;
        }

        NOOK_SERVER_MAIN_LOGI("server started tcp=27042 unix=%s agent=%s runtime=%s",
                              socket_path.c_str(),
                              agent_path.c_str(),
                              runtime_dir.c_str());

        io_loop.AddWatch(host_listener.GetFd(), 0x001, [&](int, uint32_t) {
            auto transport = host_listener.Accept(0);
            if (!transport) {
                return;
            }

            Session* session = host_sessions.CreateSession(std::move(transport));
            if (session == nullptr) {
                return;
            }

            NOOK_SERVER_MAIN_LOGI("host connected session=%u", session->GetId());
            registry.RegisterHostSession(session);
            session->SetCloseCallback([&io_loop, &registry, &host_sessions](uint32_t session_id, int peer_pid) {
                NOOK_SERVER_MAIN_LOGI("host session closing session=%u peer_pid=%d io_running=%d",
                                      session_id,
                                      peer_pid,
                                      io_loop.IsRunning() ? 1 : 0);
                (void)peer_pid;
                if (!io_loop.IsRunning()) {
                    registry.RemoveHostSession(session_id);
                    host_sessions.RemoveSession(session_id);
                    return;
                }
                io_loop.Post([&registry, &host_sessions, session_id]() {
                    registry.RemoveHostSession(session_id);
                    host_sessions.RemoveSession(session_id);
                });
            });
            session->SetMessageCallback([&, session](const Frame& frame) {
                NOOK_SERVER_MAIN_LOGI("host frame session=%u type=0x%04x msg=%u",
                                      session->GetId(),
                                      static_cast<unsigned>(frame.GetType()),
                                      frame.GetMsgId());
                dispatcher.Dispatch(*session, frame);
            });
            session->Start();
        });

        io_loop.AddWatch(agent_listener.GetFd(), 0x001, [&](int, uint32_t) {
            auto transport = agent_listener.Accept(0);
            if (!transport) {
                return;
            }

            int pid = -1;
            auto* unix_transport = static_cast<UnixTransport*>(transport.get());
            pid_t peer_pid = -1;
            if (unix_transport->GetPeerCredentials(&peer_pid, nullptr, nullptr)) {
                pid = static_cast<int>(peer_pid);
            }

            Session* session = agent_sessions.CreateSession(std::move(transport));
            if (session == nullptr) {
                return;
            }
            session->SetPeerPid(pid);
            if (pid > 0) {
                registry.RegisterAgentSession(pid, session);
            }
            NOOK_SERVER_MAIN_LOGI("agent socket connected session=%u peer_pid=%d", session->GetId(), pid);
            session->SetCloseCallback([&io_loop, &registry, &agent_sessions, session](uint32_t session_id, int peer_pid) {
                NOOK_SERVER_MAIN_LOGI("agent session closing session=%u peer_pid=%d io_running=%d",
                                      session_id,
                                      peer_pid,
                                      io_loop.IsRunning() ? 1 : 0);
                if (!io_loop.IsRunning()) {
                    if (peer_pid > 0) {
                        (void) registry.RemoveAgentSessionByPidIfMatches(peer_pid, session);
                    }
                    agent_sessions.RemoveSession(session_id);
                    return;
                }
                io_loop.Post([&registry, &agent_sessions, session_id, peer_pid, session]() {
                    if (peer_pid > 0) {
                        (void) registry.RemoveAgentSessionByPidIfMatches(peer_pid, session);
                    }
                    agent_sessions.RemoveSession(session_id);
                });
            });
            session->SetMessageCallback([&, session](const Frame& frame) {
                NOOK_SERVER_MAIN_LOGI("agent frame session=%u peer_pid=%d type=0x%04x msg=%u",
                                      session->GetId(),
                                      session->GetPeerPid(),
                                      static_cast<unsigned>(frame.GetType()),
                                      frame.GetMsgId());
                dispatcher.Dispatch(*session, frame);
            });
            session->Start();
        });

        if (!io_loop.Start()) {
            NOOK_SERVER_MAIN_LOGE("io loop start failed");
            return 3;
        }

        WaitForShutdownSignal();
        CleanupZygoteControlBeforeShutdown(&registry, &process_manager);
        registry.Shutdown();
        io_loop.Stop();
        agent_sessions.Clear();
        host_sessions.Clear();

        return 0;
    } catch (const std::exception& ex) {
        NOOK_SERVER_MAIN_LOGE("server fatal exception detail=%s", ex.what());
        return 100;
    } catch (...) {
        NOOK_SERVER_MAIN_LOGE("server fatal exception detail=non-std exception");
        return 101;
    }
#endif
}
