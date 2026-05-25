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
    const std::string source = ReadFile("server/server_main.cpp",
                                        "../../server/server_main.cpp");
    Require(!source.empty(), "failed to read server/server_main.cpp");
    const std::string helper_source = ReadFile("server/zygote_control_rpc.cpp",
                                               "../../server/zygote_control_rpc.cpp");
    Require(!helper_source.empty(), "failed to read server/zygote_control_rpc.cpp");

    Require(Contains(source, "#include \"zygote_control_rpc.h\""),
            "server_main must route zygote control through zygote_control_rpc helper");
    Require(Contains(source, "--enable-zygote-control"),
            "server_main must expose an explicit enable-zygote-control launch flag");
    Require(Contains(source, "--disable-zygote-control"),
            "server_main must expose an explicit disable-zygote-control launch flag");
    Require(Contains(source, "NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL"),
            "server_main must wire explicit zygote-control launch flags through the environment");
    Require(Contains(helper_source, "CallZygoteControlRpc("),
            "zygote_control_rpc helper must keep zygote control routed through rpc helper");
    Require(Contains(helper_source, "DispatchZygoteControlRpc("),
            "zygote_control_rpc helper must centralize install/uninstall rpc dispatch");
    Require(Contains(helper_source, "WaitForControlReadyAgentSessionByIdentity("),
            "zygote_control_rpc helper must gate rpc dispatch on control-ready AGENT_READY");
    Require(Contains(helper_source, "FindControlReadyAgentSessionByIdentity("),
            "zygote_control_rpc helper must probe immediate control-ready state through identity-aware lookup");
    Require(!Contains(helper_source, "FindControlReadyAgentSessionByProcessName(process_name)"),
            "zygote_control_rpc helper must not probe immediate control-ready state through a generic process-name fallback");
    Require(Contains(helper_source, "nook.spawn.installForkHook"),
            "zygote_control_rpc helper must install zygote spawn control through nook.spawn.installForkHook rpc");
    Require(Contains(helper_source, "nook.spawn.uninstallForkHook"),
            "zygote_control_rpc helper must uninstall zygote spawn control through nook.spawn.uninstallForkHook rpc");
    Require(Contains(helper_source, "HasOwnedImmediateControlSession(registry, zygote_pid, process_name)"),
            "zygote_control_rpc helper must only use dedicated spawn uninstall when the zygote-control target is explicitly owned");
    Require(Contains(helper_source, "IsOwnedZygoteControlTarget(process_pid, process_name)"),
            "zygote_control_rpc helper must bind owned zygote-control teardown to the current target identity, not just process name");
    Require(Contains(helper_source, "WaitForControlReadyAgentSessionByIdentity("),
            "zygote_control_rpc helper must wait for zygote agent session identity readiness before rpc dispatch");
    Require(!Contains(source, "SetZygoteSpawnControl("),
            "server_main must not directly call SetZygoteSpawnControl in the default zygote-control path");
    Require(!Contains(source, "ClearZygoteSpawnControl("),
            "server_main must not directly call ClearZygoteSpawnControl in the default zygote-control path");
    Require(Contains(source, "HasOwnedZygoteControlTarget(&registry, zygote_pid, process_name)"),
            "server_main must treat owned zygote-control targets as the authoritative reusable monitor source");
    Require(Contains(source, "FindAuthoritativeAgentSessionByPid(pid)"),
            "server_main spawn gate release must prefer the authoritative/runtime agent session");
    Require(!Contains(source, "send_resume(control_agent, \"control\")"),
            "server_main spawn gate release must not resume a separate control session once identity-aware release is available");
    Require(!Contains(source, "send_resume(authoritative_agent, \"runtime\")"),
            "server_main spawn gate release must not double-resume runtime and control sessions for the same pid");
    Require(Contains(source, "release gate target pid=%d session=control-owner split-runtime=1"),
            "server_main spawn gate release must route strict helper/runtime split sessions back to the original control-side gate owner");
    Require(Contains(source, "return send_resume(control_agent, \"control-owner\");"),
            "server_main spawn gate release must wake the helper-side control session when strict helper/runtime DSOs split gate state");
    Require(!Contains(source, "FindControlReadyAgentSessionByPid(zygote_pid) != nullptr"),
            "server_main must not treat any control-ready pid session as an owned zygote-control target");
    Require(!Contains(source, "FindControlReadyAgentSessionByProcessName(process_name) != nullptr"),
            "server_main must not treat any control-ready process-name session as an owned zygote-control target");
    Require(Contains(source, "HasEmbeddedZygoteControlResidue(pid)"),
            "server_main startup stale-clean must detect embedded zygote-control residue, not just monitor-ready env state");
    Require(!Contains(source, "if (!nook::server::ninjector::IsZygoteMonitorReady(pid)) {\n            continue;\n        }"),
            "server_main startup stale-clean must not skip cleanup solely because NOOK_ZYGOTE_MONITOR_READY is absent");

    return 0;
}
