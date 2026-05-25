#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string ReadFile(const char* primary, const char* fallback = nullptr) {
    std::ifstream input(primary, std::ios::binary);
    if (!input && fallback != nullptr) {
        input.open(fallback, std::ios::binary);
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::string internal_header = ReadFile("src/framework/NookCommInternal.h",
                                                 "../../src/framework/NookCommInternal.h");
    const std::string comm_source = ReadFile("src/framework/NookComm.cpp",
                                             "../../src/framework/NookComm.cpp");
    const std::string zygote_source = ReadFile("src/framework/nook_zygote_control.cpp",
                                               "../../src/framework/nook_zygote_control.cpp");

    Require(!internal_header.empty(), "failed to read src/framework/NookCommInternal.h");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(!zygote_source.empty(), "failed to read src/framework/nook_zygote_control.cpp");

    Require(Contains(comm_source,
                     "NookStatus NotifyZygoteControlReadyToServer() {"),
            "NookComm must expose zygote control-ready notification support");
    Require(Contains(comm_source,
                     "int g_highest_agent_ready_stage_sent = -1;"),
            "NookComm must track the highest AGENT_READY stage sent so child promotion can avoid duplicate control/runtime ready notifications");
    Require(Contains(comm_source,
                     "const int stage_value =\n"
                     "        stage == nook::comm::AgentReadyStage::kControl ? 0 : 1;"),
            "SendAgentReadyLocked must normalize AgentReadyStage into a monotonic stage value");
    Require(Contains(comm_source,
                     "if (g_highest_agent_ready_stage_sent >= stage_value) {"),
            "SendAgentReadyLocked must suppress duplicate or regressive AGENT_READY stage sends");
    Require(Contains(comm_source,
                     "g_highest_agent_ready_stage_sent = stage_value;"),
            "SendAgentReadyLocked must record the highest emitted AGENT_READY stage");
    Require(Contains(comm_source,
                     "NookComm resetting stale connection process=%s"),
            "NookComm must drop stale zygote control connections before re-initializing");
    Require(Contains(comm_source,
                     "ResetZygoteControlConnectionStateForReinit"),
            "NookComm must expose an explicit zygote-control connection reset helper for reinitialization");
    Require(Contains(comm_source,
                     "EnsureControlChannelReadyForCurrentProcess()"),
            "zygote-control forced reinit must rebuild the control channel before monitor reinitialize");
    Require(Contains(comm_source,
                     "NOOK_ZYGOTE_FORCE_REINIT"),
            "zygote-control reattach must use an explicit env gate to force the exported init entry onto the reinit path");
    Require(!Contains(zygote_source,
                      "RequestControlChannelDisconnectAfterCurrentReply(\"zygote-install-complete\");"),
            "zygote install path must keep the control channel alive after install");
    Require(!Contains(zygote_source,
                      "ScheduleControlChannelHardCloseAfterDelay(\"zygote-install-complete\""),
            "zygote install path must not schedule a hard close after install");
    Require(Contains(zygote_source,
                     "fork hook bypass suspend/resume current=%s javaHooks=1"),
            "native fork hook must bypass early reconnect when Java specialize hooks own the fork lifecycle");
    Require(Contains(zygote_source,
                     "vfork hook bypass suspend/resume current=%s javaHooks=1"),
            "native vfork hook must bypass early reconnect when Java specialize hooks own the fork lifecycle");
    Require(Contains(zygote_source,
                     "const bool bypass_suspend_resume = AreJavaZygoteSpecializeHooksInstalled();"),
            "native fork/vfork monitor must derive an explicit bypass flag before touching the control connection");
    Require(Contains(zygote_source,
                     "const bool suspended = bypass_suspend_resume\n"
                     "                               ? false\n"
                     "                               : nook::framework::SuspendAgentConnectionForFork();"),
            "native fork/vfork monitor must not suspend the control channel when Java specialize hooks own fork lifecycle");
    Require(Contains(zygote_source,
                     "nook::framework::ResetInheritedForkChildConnectionState();"),
            "forked zygote child must drop inherited control-channel state before child-local specialize handling");
    const std::string injector_source = ReadFile("server/ninjector_compat.cpp",
                                                 "../../server/ninjector_compat.cpp");
    Require(!injector_source.empty(), "failed to read server/ninjector_compat.cpp");
    Require(Contains(injector_source,
                     "reuse already-loaded zygote agent"),
            "embedded zygote inject path must reuse an already-loaded zygote agent instead of dlopen-ing a new memfd copy");
    Require(Contains(injector_source,
                     "return InjectEmbeddedSoByPidAtomic(static_cast<pid_t>(pid),"),
            "embedded zygote inject path must keep an atomic embedded memfd injection path for first-load zygote bootstrap");
    Require(Contains(injector_source,
                     "NookAgentReinitializeForZygoteControl"),
            "embedded zygote inject reuse path must target the explicit zygote reinitialize entry");
    Require(Contains(injector_source,
                     "bool ReinitializeEmbeddedZygoteAgentByPid(int pid, const char* runtime_dir)"),
            "embedded zygote injector must keep a dedicated explicit reinitialize helper");
    Require(Contains(injector_source,
                     "RemoteSetEnv(pid, \"NOOK_ZYGOTE_FORCE_REINIT\", \"1\")"),
            "embedded zygote inject path must force reinit through the exported zygote init entry instead of loading another memfd agent");
    Require(Contains(injector_source,
                     "std::strcmp(so_path, \"__embedded_agent__\") == 0"),
            "zygote agent injector must treat the embedded agent sentinel as a real embedded runtime artifact");
    Require(Contains(injector_source,
                     "return InjectEmbeddedZygoteAgentByPid(pid, runtime_dir);"),
            "zygote agent injector must forward embedded agent reinit through NOOK_RUNTIME_DIR instead of deriving a fake dirname from the sentinel");
    Require(Contains(injector_source,
                     "std::strstr(so_path, \".so\") == nullptr"),
            "zygote agent injector must treat absolute runtime directories as authoritative and avoid trimming /data/local/tmp to /data/local");
    Require(!Contains(injector_source,
                      "BuildTemporaryEmbeddedZygoteAgentPath("),
            "embedded zygote inject path must not fall back to temporary zygote agent files");
    Require(!Contains(injector_source,
                      "EnsureEmbeddedFileAtPath(temp_agent_path.c_str()"),
            "embedded zygote inject path must not materialize temporary zygote agent files to disk");
    Require(Contains(injector_source,
                     "NOOK_ZYGOTE_REINIT_CAPABLE"),
            "embedded zygote inject path must only reuse a previously loaded zygote agent when that image advertises reinit capability");
    return 0;
}
