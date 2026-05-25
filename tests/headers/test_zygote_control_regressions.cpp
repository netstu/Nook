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
    const std::string source = ReadFile("src/framework/nook_zygote_control.cpp",
                                        "../../src/framework/nook_zygote_control.cpp");
    Require(!source.empty(), "failed to read src/framework/nook_zygote_control.cpp");
    Require(!Contains(source, "pthread_atfork"),
            "zygote control must not use pthread_atfork as the main spawn path");
    Require(Contains(source, "nativeForkAndSpecialize"),
            "zygote control must hook nativeForkAndSpecialize");
    Require(Contains(source, "nativeSpecializeAppProcess"),
            "zygote control must hook nativeSpecializeAppProcess");
    Require(Contains(source, "CallOriginalNow("),
            "zygote control must invoke original zygote native methods explicitly");
    Require(Contains(source, "IsZygoteJavaNativeHooksEnabled"),
            "zygote control must gate risky Java native zygote hooks behind an explicit opt-in");
    Require(Contains(source,
                     "zygote java hook env native=%s wrapper=%s process=%s"),
            "zygote control should log the injected zygote Java-hook mode so real-device runs can confirm the bootstrap configuration");
    Require(Contains(source,
                     "return value != nullptr && std::strcmp(value, \"1\") == 0;"),
            "zygote control wrapper hooks must require an explicit opt-in");
    Require(Contains(source,
                     "if (!enable_java_native_hooks && !enable_java_wrapper_hooks)"),
            "zygote control must skip JavaHook init entirely unless a Java zygote hook mode is explicitly enabled");
    Require(Contains(source,
                     "zygote java specialize hooks disabled by default; skip JavaHook init"),
            "zygote control should log that JavaHook init is skipped on the default native-only path");
    Require(Contains(source, "NookInlineHookSymbol(\n        kLibcModule,\n        \"fork\""),
            "zygote control must install native fork monitoring through libc fork");
    Require(Contains(source, "NookInlineHookSymbol(\n        kLibcModule,\n        \"vfork\""),
            "zygote control must install native fork monitoring through libc vfork");
    Require(Contains(source, "kAndroidRuntimeModule"),
            "zygote control must resolve libandroid_runtime for child activation triggers");
    Require(Contains(source, "kProcessSetArgSymbol"),
            "zygote control must track android_os_Process_setArgV0 as a child activation trigger");
    Require(Contains(source, "bool ShouldInstallParentNativeSpecializeHooks()"),
            "strict helper-only zygote-control must expose a dedicated parent-side specialize hook policy");
    Require(Contains(source,
                     "if (ShouldUseHelperOnlyLocalZygoteControl()) {\n        return true;"),
            "strict helper-only zygote-control must force-enable parent-side specialize activation hooks");
    Require(Contains(source, "reinterpret_cast<void*>(&NativeProcessSetArgHook)"),
            "zygote control must install android_os_Process_setArgV0 monitoring as a child activation trigger");
    Require(Contains(source, "if (ShouldInstallParentNativeSpecializeHooks()) {"),
            "native zygote monitor must install parent-side specialize activation hooks when strict helper-only mode is active");
    Require(Contains(source,
                     "zygote java wrapper hooks disabled by default; native fork/vfork monitor active"),
            "zygote control should prefer the native fork/vfork monitor when wrapper hooks are not explicitly enabled");
    Require(Contains(source,
                     "nativeForkAndSpecialize child forked current=%s; defer activation to safer hooks"),
            "zygote control native fork callback must defer child activation away from direct JNI string decoding");
    Require(Contains(source,
                     "specializeAppProcess post-original current=%s; defer activation to safer hooks"),
            "zygote control specialize callback must defer child activation away from direct JNI string decoding");
    Require(Contains(source, "InstallChildNativeSpecializeHooks();"),
            "zygote control native fork/vfork child path must stage child-local specialize hooks after fork");
    Require(Contains(source,
                     "child native specialize hooks installed fork=%d vfork=%d setArgV0=%d selinux=%d current=%s"),
            "zygote control native child path must log child-local specialize hook install results");
    Require(Contains(source, "install fork hook stable mode keeps native zygote monitor path"),
            "zygote control stable spawn install must keep the native monitor path instead of forcing Java zygote hook init");
    Require(!Contains(source, "setenv(\"NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS\", \"1\", 1);"),
            "zygote control stable spawn install must not force-enable Java native zygote hooks from the RPC thread");
    Require(!Contains(source, "response.error.message = \"install java native zygote hooks failed\";"),
            "zygote control stable spawn install must not depend on Java zygote hook install in the RPC path");
    Require(Contains(source, "setArgV0 child matched nice=%s"),
            "zygote control must treat setArgV0 as an observable child match point");
    Require(Contains(source, "setArgV0 child ignored nice=%s"),
            "zygote control must report when setArgV0 does not match the armed child");
    Require(Contains(source,
                     "selinux_android_setcontext child pre-matched nice=%s activation=observe-only"),
            "helper-only zygote-control must only observe the armed child before the original setcontext path returns");
    Require(Contains(source,
                     "selinux_android_setcontext child matched nice=%s activation=deferred-post"),
            "helper-only zygote-control must defer child activation until after the original setcontext path returns");
    Require(!Contains(source,
                      "PrepareInheritedChildAgentActivation(nice_name, spawn_token, true);"),
            "helper-only zygote-control must not reset inherited child comm state inside selinux_android_setcontext before the original call returns");
    Require(Contains(source, "TryConsumeOrMatchSpawnForNiceName(nice_name, &spawn_token, &match_error)"),
            "native zygote child activation triggers must consume controller-owned spawn state before compatibility matching");
    Require(Contains(source,
                     "const std::string matched_process_name = ExtractNiceNameFromForkArgs(env, args, arg_count);"),
            "zygote control JNI fork callbacks must retain a guarded nice-name fallback for devices where safer hooks never fire");
    Require(Contains(source,
                     "const std::string matched_process_name =\n        ExtractNiceNameFromSpecializeArgs(env, args, arg_count);"),
            "zygote control JNI specialize callbacks must retain a guarded nice-name fallback for devices where safer hooks never fire");
    Require(Contains(source,
                     "TryActivateChildFromNiceName(\"nativeSpecializeAppProcess-pre\""),
            "strict helper-only zygote-control must probe child activation before nativeSpecializeAppProcess callOriginal");
    Require(Contains(source,
                     "TryActivateChildFromNiceName(\"specializeAppProcess-pre\""),
            "strict helper-only zygote-control must probe child activation before specializeAppProcess callOriginal");
    Require(Contains(source, "TryActivateChildFromNiceName(\"nativeForkAndSpecialize\""),
            "zygote control native fork callback must feed the guarded fallback child activation path");
    Require(Contains(source, "TryActivateChildFromNiceName(\"nativeSpecializeAppProcess\""),
            "zygote control native specialize callback must feed the guarded fallback child activation path");
    Require(!Contains(source, "ReadJStringUtf8(env, arg, &identifier)"),
            "zygote control setArgV0 callback must not decode jstring arguments inside zygote");
    Require(Contains(source, "PrepareInheritedChildAgentActivation("),
            "zygote child path must defer inherited child activation to a safer post-specialization stage");
    Require(Contains(source, "ResetInheritedForkChildConnectionState();"),
            "zygote native fork/vfork child path must clear inherited control state immediately after fork");
    Require(!Contains(source,
                      "const NookStatus status = EnsureFullAgentReadyForCurrentProcess();"),
            "zygote child path must not perform full agent initialization directly inside selinux specialization callbacks");
    Require(Contains(source, "NotifyZygoteControlReadyToServer"),
            "zygote control must notify the server as soon as local control readiness is established");
    Require(Contains(source, "ResetInheritedConnectionStateForChild"),
            "zygote child path must rebuild inherited comm state");
    Require(Contains(source, "UninstallZygoteHooksLocked();"),
            "zygote control uninstall must tear down installed hooks");

    const std::string comm_source = ReadFile("src/framework/NookComm.cpp",
                                             "../../src/framework/NookComm.cpp");
    Require(!comm_source.empty(), "failed to read src/framework/NookComm.cpp");
    Require(!Contains(comm_source,
                      "const NookStatus notify_status = nook::framework::NotifyZygoteControlReadyToServer();"),
            "NookAgentInitializeForZygoteControl must not duplicate zygote control-ready notify after monitor init");
    Require(Contains(comm_source, "JavaHookLoaderResolver::ResetInheritedApplicationLoaderState();"),
            "child activation must clear inherited application loader state");
    Require(Contains(comm_source, "PendingJavaHookRegistry::Instance().ResetInheritedStateForChild();"),
            "child activation must clear inherited deferred Java-hook registry state before re-priming bootstrap hooks");
    Require(Contains(comm_source, "JavaHookClassObserver::ResetInheritedStateForChild();"),
            "child activation must rebuild inherited deferred Java-hook observer state before scheduling retries");
    Require(Contains(comm_source, "void ResetInheritedForkChildConnectionState() {"),
            "NookComm must expose a dedicated helper for resetting inherited fork-child comm state");
    Require(Contains(comm_source, "NookStatus PrimeActivatedSpawnChildBootstrap()"),
            "spawned child activation must provide a synchronous bootstrap-prime path");
    Require(Contains(comm_source, "bool ShouldPrimeActivatedSpawnChildBootstrap(const std::string& process_name)"),
            "spawned child activation must expose an explicit policy helper deciding whether inherited bootstrap prime is allowed");
    Require(Contains(comm_source, "if (!ShouldPrimeActivatedSpawnChildBootstrap(process_name)) {"),
            "child activation must gate inherited bootstrap priming through the explicit policy helper");
    Require(Contains(comm_source, "skip synchronous child bootstrap prime for child-owned spawn process=%s"),
            "child-owned spawn must skip inherited bootstrap priming so the injected runtime agent owns the authoritative connection");
    Require(Contains(comm_source, "const NookStatus prime_status = PrimeActivatedSpawnChildBootstrap();"),
            "strict zygote-control child activation must still attempt synchronous bootstrap prime when the policy helper allows it");
    Require(Contains(comm_source, "const NookStatus java_status = NookJavaHookInitialize();"),
            "spawned child bootstrap prime must initialize JavaHook before comm init so spawn-gate lifecycle hooks can install before app bootstrap races ahead");
    Require(Contains(comm_source,
                     "const NookStatus early_bootstrap_status = InstallSpawnGateBootstrapHookIfNeededLocked();"),
            "spawned child bootstrap prime must attempt an early spawn-gate hook install before comm init completes");
    Require(Contains(comm_source, "const NookStatus ready_status = EnsureRuntimeBridgeAndReady();"),
            "spawned child activation must eagerly prepare runtime bridge during synchronous bootstrap prime");
    Require(Contains(comm_source, "synchronous child bootstrap prime runtime ready failed status=%d process=%s"),
            "spawned child activation must log runtime-ready failures during synchronous bootstrap prime");
    Require(Contains(comm_source, "const NookStatus control_ready_status = nook::framework::NotifyZygoteControlReadyToServer();"),
            "helper-only spawned child bootstrap prime must notify control-ready before waiting on the spawn gate");
    Require(Contains(comm_source, "synchronous child bootstrap prime control-ready failed status=%d process=%s"),
            "helper-only spawned child bootstrap prime must surface control-ready failures explicitly");
    Require(Contains(comm_source,
                     "synchronous child bootstrap prime zygote-control promoted-child fast path process=%s"),
            "strict zygote-control child activation must provide a dedicated promoted-child fast path");
    Require(Contains(comm_source,
                     "synchronous child bootstrap prime zygote-control promoted-child java hook init failed status=%d process=%s"),
            "strict zygote-control promoted-child fast path must surface JavaHook init failures before startup races ahead");
    Require(Contains(comm_source,
                     "synchronous child bootstrap prime zygote-control promoted-child hook install failed status=%d process=%s"),
            "strict zygote-control promoted-child fast path must surface early spawn-gate hook install failures explicitly");
    Require(Contains(comm_source,
                     "synchronous child bootstrap prime zygote-control promoted-child bootstrap hooks installed process=%s"),
            "strict zygote-control promoted-child fast path must install spawn-gate bootstrap hooks before control-ready");
    Require(Contains(comm_source,
                     "synchronous child bootstrap prime zygote-control promoted-child control-ready failed status=%d process=%s"),
            "strict zygote-control child activation fast path must surface control-ready failures explicitly");
    Require(Contains(comm_source,
                     "synchronous child bootstrap prime zygote-control promoted-child fast path ok process=%s"),
            "strict zygote-control child activation fast path must log successful control-ready priming");
    Require(Contains(comm_source,
                     "strict zygote-control promoted-child fast path must not block native bootstrap before ActivityThread attach completes"),
            "strict zygote-control regression doc must explain why the promoted-child fast path no longer waits on the native gate");
    Require(Contains(comm_source, "strict child native gate init process=%s armed=%d released=%d spawn_gate=%d"),
            "strict zygote-control child native gate must expose init-state logging for real-device timing diagnosis");
    Require(Contains(comm_source, "primed child agent bootstrap synchronously process=%s"),
            "spawned child activation must log successful synchronous bootstrap priming");
    Require(Contains(comm_source, "NOOK_AGENT_EXPORT NookStatus NookAgentInitializeForSpawnChild(void)"),
            "spawn child full-agent injection must expose a dedicated exported init entry");
    Require(Contains(comm_source, "NookAgentInitializeForSpawnChild runtime-ready failed status=%d process=%s"),
            "spawn child full-agent init must fail fast when runtime-ready bootstrap cannot complete");
    Require(Contains(comm_source,
                     "NookAgentInitializeForSpawnChild defer runtime-ready for early process=%s"),
            "spawn child full-agent init must not publish runtime-ready while the process is still in an early zygote identity");
    Require(Contains(comm_source,
                     "skip spawn gate re-arm for promoted zygote-control child process=%s"),
            "strict zygote-control promoted child full-agent promotion must not re-arm the spawn gate after the helper-owned gate has already been released");
    Require(Contains(comm_source,
                     "NookAgentInitializeForSpawnChild skip bootstrap hook reinstall for promoted zygote-control child process=%s"),
            "strict zygote-control promoted child full-agent promotion must not reinstall Instrumentation bootstrap hooks after helper-owned bootstrap release");
    Require(Contains(comm_source, "void EnsurePromotedStrictRuntimeLifecycleSyncThreadStarted()"),
            "strict zygote-control promoted child runtime path must expose a delayed lifecycle sync retry helper");
    Require(Contains(comm_source,
                     "schedule promoted strict runtime lifecycle sync retry process=%s"),
            "strict zygote-control promoted child runtime path must log when delayed lifecycle sync retry is armed");
    Require(Contains(comm_source,
                     "promoted strict runtime lifecycle sync retry ok process=%s attempt=%d"),
            "strict zygote-control promoted child runtime path must log when delayed lifecycle sync finally observes currentApplication");
    Require(Contains(comm_source,
                     "promoted strict runtime lifecycle sync retry timed out process=%s"),
            "strict zygote-control promoted child runtime path must surface lifecycle sync retry exhaustion explicitly");
    Require(Contains(comm_source,
                     "TrySyncApplicationLifecycleStateFromCurrentApplication(\n                    \"promoted-strict-spawn-child-runtime\")"),
            "strict zygote-control promoted child runtime path must probe currentApplication immediately before falling back to retry");
    Require(Contains(comm_source, "EnsurePromotedStrictRuntimeLifecycleSyncThreadStarted();"),
            "strict zygote-control promoted child runtime path must arm delayed lifecycle sync retry when the initial probe races ActivityThread attach");
    Require(Contains(comm_source,
                     "promote zygote-control child process identity observed=%s target=%s"),
            "strict zygote-control full-agent promotion must elevate early zygote identities to the armed target package before normal child init continues");
    Require(Contains(comm_source,
                     "const char* strict_request = std::getenv(\"NOOK_STRICT_ZYGOTE_REQUEST\");"),
            "strict zygote-control promoted child detection must key off the explicit strict request env instead of helper-local control mode");
    Require(Contains(comm_source, "bool HasStrictZygoteSpawnEnvironment()"),
            "strict zygote-control native gate must key off the inherited strict spawn environment instead of target-process auto-init policy");
    Require(Contains(comm_source,
                     "const bool arm_strict_child_native_gate =\n            arm_spawn_gate && HasStrictZygoteSpawnEnvironment();"),
            "child native gate reset must only arm the strict lifecycle gate when the inherited strict spawn environment is actually present");
    Require(Contains(comm_source,
                     "g_strict_child_native_gate_armed = arm_strict_child_native_gate;"),
            "strict zygote-control child native gate reset must not mirror the generic spawn gate onto default legacy spawn children");
    Require(Contains(comm_source,
                     "const bool inherited_strict_child_native_gate_armed ="),
            "strict zygote-control comm init must preserve the native gate armed by child activation");
    Require(Contains(comm_source,
                     "g_strict_child_native_gate_armed = inherited_strict_child_native_gate_armed;"),
            "strict zygote-control comm init must not clear the helper-side native gate before the control-ready wait");
    Require(Contains(comm_source, "bool g_strict_spawn_resume_requested = false;"),
            "strict zygote-control must track whether host resume already arrived before the strict activity-stage gate can release");
    Require(Contains(comm_source, "void ReleaseStrictSpawnGateAtActivityOnCreate(const char* stage,"),
            "strict zygote-control must expose an activity-stage gate release helper instead of releasing from Application callbacks");
    Require(Contains(comm_source,
                     "spawn gate resume handler defer strict release request_pid=%u"),
            "strict zygote-control resume handling must record the host resume and defer final release until the activity-stage gate");
    Require(Contains(comm_source,
                     "spawn gate resume handler defer strict release request_pid=%u\", request.pid);\n            g_spawn_gate_cv.notify_all();"),
            "strict zygote-control deferred resume handling must wake the waiting activity-stage gate or the app will white-screen after Process resumed");
    Require(Contains(comm_source,
                     "strict spawn gate activity-stage release stage=%s pid=%u activity=%s newApplication=%d callApplicationOnCreate=%d callActivityOnCreate=%d"),
            "strict zygote-control must log the activity-stage gate release that actually unblocks app startup");
    Require(Contains(comm_source,
                     "blocking strict activity bootstrap on resume request pid=%u"),
            "strict zygote-control must wait for host resume at the Activity-stage gate instead of blocking forever in Application bootstrap");
    Require(Contains(comm_source,
                     "skip newApplication gate wait for strict activity-stage release pid=%u"),
            "strict zygote-control must not block app bootstrap at Instrumentation.newApplication once activity-stage release owns the gate");
    Require(Contains(comm_source,
                     "skip callApplicationOnCreate gate wait for strict activity-stage release pid=%u"),
            "strict zygote-control must not block app bootstrap at callApplicationOnCreate once activity-stage release owns the gate");
    Require(Contains(comm_source,
                     "strict activity-stage install pending hooks activity=%s"),
            "strict zygote-control must synchronously install pending Java hooks for the concrete Activity before releasing the gate");
    Require(!Contains(comm_source,
                      "const char* strict_value = std::getenv(\"NOOK_STRICT_ZYGOTE_CONTROL\");"),
            "strict zygote-control promoted child detection must not reuse helper-local control env and pollute the default spawn path");
    Require(Contains(comm_source, "bool IsCurrentProcessSpawnGateHeld()"),
            "NookComm must expose a spawn-gate state probe for script-load synchronization");
    Require(Contains(comm_source,
                     "return (g_spawn_gate_armed && !g_spawn_gate_released) ||\n           (g_strict_child_native_gate_armed && !g_strict_child_native_gate_released);"),
            "spawn-gate state probe must treat strict helper-only native gate as held so strict script load cannot bypass Java-ready synchronization");
    Require(Contains(comm_source, "skip spawn gate probe for early process=%s"),
            "NookComm must not probe spawn-gate Java readiness inside zygote/early processes");
    Require(Contains(comm_source, "void ClearSpawnGateBootstrapHooks("),
            "spawn-gate lifecycle must expose a dedicated cleanup helper for bootstrap hooks");
    Require(Contains(comm_source, "ClearSpawnGateBootstrapHooks(new_application_hook_id,") &&
                Contains(comm_source, "call_application_on_create_hook_id,") &&
                Contains(comm_source, "call_activity_on_create_hook_id);"),
            "spawn-gate resume path must clear deferred bootstrap hooks once the gate is released");
    Require(Contains(comm_source, "JavaHookLoaderResolver::SetRequireApplicationLifecycleReady(false);"),
            "spawn-gate bootstrap cleanup must drop the lifecycle-ready requirement after resume or stale reset");
    Require(Contains(comm_source, "skip spawn gate arming for force-early process=%s"),
            "NookComm force-early zygote-control init must skip spawn-gate arming entirely");
    Require(!Contains(comm_source,
                      "return !LooksLikeEarlySpawnProcessNameLocal(process_name);"),
            "helper-only strict child must not bypass spawn-gate bootstrap hooks once it leaves the early zygote process");

    const std::string loader_source = ReadFile("src/java_hook/deferred/java_hook_loader_resolver.cpp",
                                               "../../src/java_hook/deferred/java_hook_loader_resolver.cpp");
    Require(!loader_source.empty(),
            "failed to read src/java_hook/deferred/java_hook_loader_resolver.cpp");
    Require(!Contains(loader_source, "NOOK_APP_CLASS_LOADER_PTR"),
            "application loader cache must not pass raw jobject pointers through environment variables");
    Require(Contains(loader_source, "DeleteCachedApplicationClassLoaderGlobalRefLocked"),
            "inherited application loader reset must release the cached global ref when possible");

    const std::string bridge_source =
        ReadFile("src/agent_runtime/nook_script_runtime_bridge.cpp",
                 "../../src/agent_runtime/nook_script_runtime_bridge.cpp");
    Require(!bridge_source.empty(),
            "failed to read src/agent_runtime/nook_script_runtime_bridge.cpp");
    Require(Contains(bridge_source, "framework::IsCurrentProcessStrictLifecycleSpawnGateHeld()"),
            "script load path must only block on lifecycle-ready when the strict lifecycle-stage spawn gate is still held");
    Require(Contains(bridge_source,
                     "script load synchronized java-ready while spawn gate held script_id=%u"),
            "spawn-gated script load must synchronously dispatch Java.ready callbacks before acknowledging script load");
    Require(Contains(bridge_source,
                     "script load deferred java-ready while spawn gate held script_id=%u"),
            "spawn-gated script load must explicitly log when lifecycle readiness is still unavailable at load time");
    Require(Contains(bridge_source, "WaitForSpawnGateLifecycleReadyAndSync"),
            "spawn-gated script load must centralize lifecycle-ready waiting so strict spawn cannot acknowledge load before Java.perform work is installable");
    Require(Contains(bridge_source,
                     "script load timed out waiting for java-ready while spawn gate held script_id=%u"),
            "spawn-gated script load must fail loudly if lifecycle readiness never arrives before the bounded strict wait expires");
    Require(Contains(bridge_source,
                     "script load lifecycle ready after wait while spawn gate held script_id=%u attempt=%d"),
            "spawn-gated script load must log when bounded lifecycle waiting succeeds so strict real-device timing can be diagnosed");

    const std::string pending_registry_source =
        ReadFile("src/java_hook/deferred/pending_java_hook_registry.cpp",
                 "../../src/java_hook/deferred/pending_java_hook_registry.cpp");
    Require(!pending_registry_source.empty(),
            "failed to read src/java_hook/deferred/pending_java_hook_registry.cpp");
    Require(Contains(pending_registry_source, "void PendingJavaHookRegistry::ResetInheritedStateForChild()"),
            "pending Java-hook registry must expose a fork-safe child reset entry");
    Require(Contains(pending_registry_source, "new (&mutex_) std::mutex();"),
            "pending Java-hook registry child reset must rebuild the inherited mutex instead of locking fork-stale state");

    const std::string observer_source =
        ReadFile("src/java_hook/deferred/java_hook_class_observer.cpp",
                 "../../src/java_hook/deferred/java_hook_class_observer.cpp");
    Require(!observer_source.empty(),
            "failed to read src/java_hook/deferred/java_hook_class_observer.cpp");
    Require(Contains(observer_source, "void ResetInheritedStateForChild()"),
            "Java hook deferred observer must expose a fork-safe child reset entry");
    Require(Contains(observer_source, "new (&g_retry_worker) std::thread();"),
            "Java hook deferred observer child reset must rebuild the inherited retry worker handle");

    const std::string unix_source = ReadFile("src/communication/transport/unix_transport.cpp",
                                             "../../src/communication/transport/unix_transport.cpp");
    Require(!unix_source.empty(),
            "failed to read src/communication/transport/unix_transport.cpp");
    Require(Contains(unix_source, "SetCloseOnExec"),
            "unix transport must set close-on-exec on zygote-control sockets");
    Require(Contains(unix_source, "AcceptUnixSocket"),
            "unix transport accept path must preserve close-on-exec on accepted sockets");

    const std::string jvm_source = ReadFile("src/java_hook/JVM.cpp",
                                            "../../src/java_hook/JVM.cpp");
    Require(!jvm_source.empty(), "failed to read src/java_hook/JVM.cpp");
    Require(Contains(jvm_source, "LOGE(\"Failed to attach thread ret=%d\", attach_ret);"),
            "JavaEnv must log the AttachCurrentThread failure code in zygote-control debugging paths");
    Require(Contains(jvm_source, "AttachCurrentThreadAsDaemon"),
            "JavaEnv must attempt AttachCurrentThreadAsDaemon as a fallback for zygote-control threads");

    const std::string injector_source = ReadFile("server/ninjector_compat.cpp",
                                                 "../../server/ninjector_compat.cpp");
    Require(!injector_source.empty(), "failed to read server/ninjector_compat.cpp");
    Require(Contains(injector_source, "\"NookAgentInitializeForSpawnChild\""),
            "spawn child embedded full-agent injection must use the dedicated spawn-child init symbol");
    Require(Contains(injector_source,
                     "ok = RemoteUnsetEnv(static_cast<pid_t>(zygote_pid), \"NOOK_STRICT_ZYGOTE_REQUEST\") && ok;"),
            "zygote spawn-control cleanup must always clear the explicit strict-request env to prevent strict state from leaking into subsequent default spawns");
    const std::string spawn_injector_source = ReadFile("server/ninjector_spawn_injector.cpp",
                                                       "../../server/ninjector_spawn_injector.cpp");
    Require(!spawn_injector_source.empty(), "failed to read server/ninjector_spawn_injector.cpp");
    Require(Contains(spawn_injector_source, "bool NinjectorSpawnInjector::InjectSpawnChildAgent("),
            "spawn child promotion must keep a dedicated injector entry");
    Require(Contains(spawn_injector_source, "InjectEmbeddedAgentByPidSuspended("),
            "spawn child promotion must use suspended-child embedded injection");
    Require(!Contains(spawn_injector_source, "IsEnvEnabled(\"NOOK_STRICT_ZYGOTE_CONTROL\")"),
            "server spawn routing must not treat NOOK_STRICT_ZYGOTE_CONTROL as a public strict-route selector");
    Require(Contains(spawn_injector_source,
                     "const bool strict_zygote_control_requested =\n        config_.enable_zygote_control &&\n        IsStrictZygoteControlRequested(request);"),
            "zygote-control transaction arming must key off the explicit strict request instead of ambient server env");
    Require(Contains(spawn_injector_source,
                     "policy.strict_zygote_control = strict_zygote_requested;"),
            "public spawn routing policy must derive strict mode only from the explicit request");
    const std::string spawn_controller_source = ReadFile("server/spawn_controller.cpp",
                                                         "../../server/spawn_controller.cpp");
    Require(!spawn_controller_source.empty(), "failed to read server/spawn_controller.cpp");
    Require(Contains(spawn_controller_source, "InjectSpawnChildAgent(pid, agent_path, &inject_error)"),
            "spawn controller must route spawned child promotion through the dedicated injector entry");
    return 0;
}
