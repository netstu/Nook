#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string ReadFileWithFallback(const char* primary_path, const char* fallback_path) {
    std::ifstream input(primary_path);
    if (!input.is_open() && fallback_path != nullptr) {
        input.open(fallback_path);
    }
    if (!input.is_open()) {
        return std::string();
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool Contains(const std::string& contents, const char* needle) {
    return needle != nullptr && contents.find(needle) != std::string::npos;
}

bool VerifyDeferredInitAttemptsImmediately() {
    const std::string contents = ReadFileWithFallback("src/framework/NookJavaHook.cpp",
                                                      "../../src/framework/NookJavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const char* delayed_init_block =
        "    // Avoid touching ART internals immediately at process start.\n"
        "    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));\n"
        "\n"
        "    for (int attempt = 0; attempt < max_attempts; ++attempt) {";
    return !Contains(contents, delayed_init_block);
}

bool VerifyObjectArgsUseJniRefs() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents,
                    "static jobject create_local_ref_from_stack_ref(JNIEnv* env, uint64_t stackRef)") &&
           Contains(contents,
                    "ownedLocalRefs[i] = create_local_ref_from_stack_ref(env, obj_ptr);") &&
           !Contains(contents,
                     "ownedLocalRefs[i] = env->NewLocalRef(reinterpret_cast<jobject>(obj_ptr));");
}

bool VerifyStackReferenceDecodeGuardsUnreadableAddresses() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "if (!TryReadU32(aligned, &compressed_ref)) {") &&
           !Contains(contents, "return *reinterpret_cast<uint32_t*>(aligned);");
}

bool VerifyDeferredObserverAvoidsLoadClassHook() {
    const std::string contents = ReadFileWithFallback("src/java_hook/deferred/java_hook_class_observer.cpp",
                                                      "../../src/java_hook/deferred/java_hook_class_observer.cpp");
    if (contents.empty()) {
        return false;
    }

    return !Contains(contents, "\"java/lang/ClassLoader\"") &&
           !Contains(contents, "\"loadClass\"") &&
           !Contains(contents, "}).detach();");
}

bool VerifyJavaWildcardMethodResolutionExists() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "methodSignature == \"*\"") &&
           Contains(contents, "getDeclaredMethods") &&
           Contains(contents, "FromReflectedMethod");
}

bool VerifyJavaJsBridgeResolvesWildcardSignatureBeforeDispatch() {
    const std::string contents = ReadFileWithFallback("src/agent_runtime/nook_java_js_bridge.cpp",
                                                      "../../src/agent_runtime/nook_java_js_bridge.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "ResolveWildcardJavaMethodSignature") &&
           Contains(contents, "resolved_request.signature == \"*\"") &&
           Contains(contents, "out_record = MakeRecordFromRequest(resolved_request)");
}

bool VerifyJavaJsBridgeActiveInvocationTracksPerThreadStack() {
    const std::string contents = ReadFileWithFallback("src/agent_runtime/nook_java_js_bridge.cpp",
                                                      "../../src/agent_runtime/nook_java_js_bridge.cpp");
    if (contents.empty()) {
        return false;
    }

    return !Contains(contents, "thread_local ActiveJavaJsInvocation g_active_java_js_invocation = {};") &&
           Contains(contents, "std::mutex g_active_java_js_invocations_mutex;") &&
           Contains(contents, "using ActiveJavaJsInvocationStack = std::vector<ActiveJavaJsInvocation>;") &&
           Contains(contents, "std::unordered_map<uint64_t, ActiveJavaJsInvocationStack> g_active_java_js_invocations;") &&
           Contains(contents, "uint64_t GetCurrentThreadIdForJavaJsInvocation()") &&
           Contains(contents, "bool TryGetActiveJavaJsInvocation(") &&
           Contains(contents, "void PushActiveJavaJsInvocationForCurrentThread(") &&
           Contains(contents, "void PopActiveJavaJsInvocationForCurrentThread()") &&
           Contains(contents, "it->second.back()") &&
           Contains(contents, "stack.push_back(invocation);") &&
           Contains(contents, "stack.pop_back();");
}

bool VerifyJavaJsBridgeMethodResolutionTraversesSuperclasses() {
    const std::string contents = ReadFileWithFallback("src/agent_runtime/nook_java_js_bridge.cpp",
                                                      "../../src/agent_runtime/nook_java_js_bridge.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "\"getSuperclass\", \"()Ljava/lang/Class;\"") &&
           Contains(contents, "jclass current_class = clazz;") &&
           Contains(contents, "while (current_class != nullptr)") &&
           Contains(contents, "env->CallObjectMethod(current_class, get_declared_methods)") &&
           Contains(contents, "env->CallObjectMethod(current_class, get_superclass)");
}

bool VerifyJavaOriginalInvokeUsesFreshArtMethodClone() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const std::string begin = "bool JavaHook::InvokeOriginalMethod(";
    const std::string end = "int JavaHook::HookMethod(";
    const std::size_t start = contents.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = contents.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = contents.substr(start, finish - start);
    return !Contains(body, "invokeArtMethod = liveHookInfo.backupArtMethod;") &&
           Contains(contents, "static void* allocate_invoke_artmethod(HookInfo& hookInfo)") &&
           Contains(contents, "sync_backup_artmethod(hookInfo);") &&
           Contains(contents, "recover_artmethod(invokeArtMethod, hookInfo, true);") &&
           Contains(body, "invokeArtMethod = allocate_invoke_artmethod(liveHookInfo);");
}

bool VerifyJavaOriginalInvokeHasReentryBypassGuard() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    return !Contains(contents, "thread_local std::unordered_map<uint32_t, uint32_t> g_original_invoke_bypass_depths;") &&
           Contains(contents, "uint64_t GetCurrentThreadIdForBypassState()") &&
           Contains(contents, "std::mutex g_original_invoke_bypass_mutex;") &&
           Contains(contents, "std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint32_t>> g_original_invoke_bypass_depths;") &&
           Contains(contents, "bool IsOriginalInvokeBypassActive(uint32_t hook_id)") &&
           Contains(contents, "ScopedOriginalInvokeBypass bypass_scope(") &&
           Contains(contents, "const bool bypass_callback = IsOriginalInvokeBypassActive(hookID);") &&
           Contains(contents, "hook_handler: bypass callback for hook=%u") &&
           Contains(contents, "during callOriginal");
}

bool VerifyJsRuntimeUsesRecursiveMutexForReentrantJavaHooks() {
    const std::string contents = ReadFileWithFallback("src/agent_runtime/js_runtime.cpp",
                                                      "../../src/agent_runtime/js_runtime.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "std::recursive_mutex runtime_mutex;") &&
           Contains(contents, "std::lock_guard<std::recursive_mutex> lock(state.runtime_mutex);");
}

bool VerifyJsRuntimeCachesReadableMappingsAndPreservesPointerMetadataForMemoryReadUtf8() {
    const std::string contents = ReadFileWithFallback("src/agent_runtime/js_runtime.cpp",
                                                      "../../src/agent_runtime/js_runtime.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "struct ReadableMappingRecord") &&
           Contains(contents, "std::mutex& GetReadableMappingSnapshotMutex()") &&
           Contains(contents, "std::vector<ReadableMappingRecord>& GetReadableMappingSnapshot()") &&
           Contains(contents, "bool RefreshReadableMappingSnapshot()") &&
           Contains(contents, "FindReadableMappingEndInSnapshot(") &&
           Contains(contents, "pointer = JS_DupValue(ctx, argv[0]);") &&
           Contains(contents, "JSValue cached_utf8 = JS_GetPropertyStr(ctx, this_val, \"$utf8\");");
}

bool VerifyJavaOriginalInvokeAvoidsRelockingHookStoreDuringSizeCheck() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const std::string begin = "bool JavaHook::InvokeOriginalMethod(";
    const std::string end = "int JavaHook::HookMethod(";
    const std::size_t start = contents.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = contents.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = contents.substr(start, finish - start);
    return Contains(body, "SizeUnsafe()") && !Contains(body, "Instance().Size())");
}

bool VerifyJavaHookForcesInterpreterAndInvalidatesJitOnInstall() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const std::string begin = "int JavaHook::HookMethod(";
    const std::string end = "bool JavaHook::Unhook(";
    const std::size_t start = contents.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = contents.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = contents.substr(start, finish - start);
    return Contains(contents, "struct InstrumentationSpec") &&
           Contains(contents, "static bool TrySetForcedInterpretOnly(") &&
           Contains(contents, "static bool TryInvalidateJitCodeCache(") &&
           Contains(body, "TrySetForcedInterpretOnly(") &&
           Contains(body, "TryInvalidateJitCodeCache(");
}

bool VerifyJavaUnhookDoesNotHoldStoreMutexAcrossSuspendAll() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const std::string begin = "bool JavaHook::Unhook(int hookId) {";
    const std::string end = "void JavaHook::UnhookAll() {";
    const std::size_t start = contents.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = contents.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = contents.substr(start, finish - start);
    const std::size_t store_lock = body.find("std::lock_guard<std::mutex> storeLock(storeMtx);");
    const std::size_t snapshot = body.find("has_remaining_hooks = HasAnyValidHookUnsafe();");
    const std::size_t suspend = body.find("ArtInternals::ScopedSuspendAllFn(SSA, \"Unhook\", false);");

    if (store_lock == std::string::npos ||
        snapshot == std::string::npos ||
        suspend == std::string::npos) {
        return false;
    }

    return store_lock < snapshot &&
           snapshot < suspend &&
           !Contains(body, "const bool has_remaining_hooks = HasAnyValidHookUnsafe();");
}

bool VerifyJavaHookUsesReplacementRoutingPath() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const std::string begin = "int JavaHook::HookMethod(";
    const std::string end = "bool JavaHook::Unhook(";
    const std::size_t start = contents.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = contents.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = contents.substr(start, finish - start);
    return Contains(contents, "struct StaticReplacementHookHandle") &&
           Contains(contents, "static bool InstallStaticReplacementHook(") &&
           Contains(contents, "static void UninstallStaticReplacementHook(") &&
           Contains(body, "InstallStaticReplacementHook(");
}

bool VerifyJavaStaticHookCallbackExecutesCallbackAndWritesReturn() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const std::string begin = "static void StaticNativeHookCallback(";
    const std::string end = "// JavaHook";
    const std::size_t start = contents.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = contents.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = contents.substr(start, finish - start);
    return Contains(body, "JavaHook::InvokeOriginalMethod(") &&
           Contains(body, "ctx->x[0]") &&
           Contains(body, "hookInfo.callback(");
}

bool VerifyJavaStaticHookRoutesOriginalQuickEntryThroughRouterStub() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    const std::string begin = "static bool InstallStaticReplacementHook(";
    const std::string end = "static void UninstallStaticReplacementHook(";
    const std::size_t start = contents.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = contents.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = contents.substr(start, finish - start);
    return Contains(body, "hook_create_art_router_stub(") &&
           Contains(body, "const bool uses_shared_art_stub =") &&
           Contains(body, "hook_info->hookedEntryPoint = uses_shared_art_stub") &&
           Contains(body, "? hook_info->orgEntryPoint") &&
           Contains(body, ": reinterpret_cast<uint64_t>(handle->routerStub);");
}

bool VerifyJavaStaticHookInstallsSharedArtRouters() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "bool EnsureSharedArtRouterHooksInstalled(") &&
           Contains(contents, "\"quick_generic_jni_trampoline\"") &&
           Contains(contents, "\"quick_to_interpreter_bridge\"") &&
           Contains(contents, "\"quick_resolution_trampoline\"") &&
           Contains(contents, "EnsureSharedArtRouterHooksInstalled(jenv.get())");
}

bool VerifyJavaStaticHookInstallsDoCallRouterHooks() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "static std::vector<void*> FindDoCallTargets()") &&
           Contains(contents, "static void DoCallEnterCallback(HookContext* ctx, void* user_data)") &&
           Contains(contents, "static bool EnsureDoCallHooksInstalled()") &&
           Contains(contents, "hook_attach(target, DoCallEnterCallback, nullptr, nullptr, 0)") &&
           Contains(contents, "EnsureDoCallHooksInstalled()");
}

bool VerifyJavaStaticHookInstallsArtMaintenanceHooks() {
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (contents.empty()) {
        return false;
    }

    return Contains(contents, "static bool IsReplacementMethodPointer(") &&
           Contains(contents, "static void SynchronizeStaticReplacementHooks()") &&
           Contains(contents, "static void OatQuickMethodHeaderReplaceCallback(") &&
           Contains(contents, "static bool EnsureArtMaintenanceHooksInstalled()") &&
           Contains(contents, "hook_replace(target, OatQuickMethodHeaderReplaceCallback, nullptr, 0)") &&
           Contains(contents, "hook_attach(target, nullptr, FixupStaticTrampolinesLeaveCallback, nullptr, 0)") &&
           Contains(contents, "EnsureArtMaintenanceHooksInstalled()");
}

bool VerifyJavaStaticHookNormalizesAccessFlagsLikeFrida() {
    const std::string header = ReadFileWithFallback("src/java_hook/JavaHook.h",
                                                    "../../src/java_hook/JavaHook.h");
    const std::string contents = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    if (header.empty() || contents.empty()) {
        return false;
    }

    return Contains(header, "static constexpr uint32_t kAccCriticalNative = 0x00200000;") &&
           Contains(header, "static constexpr uint32_t kAccFastInterpreterToInterpreterInvoke = 0x40000000;") &&
           Contains(header, "static constexpr uint32_t kAccSingleImplementation = 0x08000000;") &&
           Contains(contents, "kAccFastInterpreterToInterpreterInvoke") &&
           Contains(contents, "kAccSingleImplementation") &&
           Contains(contents, "kAccCompileDontBother") &&
           Contains(contents, "kAccCriticalNative | kAccFastNative |") &&
           Contains(contents, "kAccNterpEntryPointFastPathFlag |") &&
           Contains(contents, "kAccFastInterpreterToInterpreterInvoke |") &&
           Contains(contents, "kAccSingleImplementation");
}

bool VerifyJavaDebugBindingsAreExposed() {
    const std::string runtime = ReadFileWithFallback("src/agent_runtime/js_runtime.cpp",
                                                     "../../src/agent_runtime/js_runtime.cpp");
    const std::string hook = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                  "../../src/java_hook/JavaHook.cpp");
    const std::string header = ReadFileWithFallback("src/java_hook/JavaHook.h",
                                                    "../../src/java_hook/JavaHook.h");
    if (runtime.empty() || hook.empty() || header.empty()) {
        return false;
    }

    return Contains(runtime, "JSValue JsJavaDeopt(") &&
           Contains(runtime, "JSValue JsJavaSetForcedInterpretOnly(") &&
           Contains(runtime, "JSValue JsJavaArtRouterDebug(") &&
           Contains(runtime, "bool EnsureJavaHookReadyForJs(") &&
           Contains(runtime, "NookJavaHookInitialize()") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, java, \"deopt\", java_deopt)") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, java, \"_setForcedInterpretOnly\", java_set_forced_interpret_only)") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, java, \"_artRouterDebug\", java_art_router_debug)") &&
           Contains(header, "static bool DeoptimizeJit(bool* invalidated);") &&
           Contains(header, "static bool SetForcedInterpretOnly(bool enable, bool* changed);") &&
           Contains(header, "static void GetArtRouterDebug(uint64_t* last_x0, uint64_t* miss_count);") &&
           Contains(hook, "bool JavaHook::DeoptimizeJit(bool* invalidated)") &&
           Contains(hook, "bool JavaHook::SetForcedInterpretOnly(bool enable, bool* changed)") &&
           Contains(hook, "void JavaHook::GetArtRouterDebug(uint64_t* last_x0, uint64_t* miss_count)");
}

bool VerifyJavaDeoptExposesDiagnostics() {
    const std::string runtime = ReadFileWithFallback("src/agent_runtime/js_runtime.cpp",
                                                     "../../src/agent_runtime/js_runtime.cpp");
    const std::string hook = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                  "../../src/java_hook/JavaHook.cpp");
    const std::string header = ReadFileWithFallback("src/java_hook/JavaHook.h",
                                                    "../../src/java_hook/JavaHook.h");
    if (runtime.empty() || hook.empty() || header.empty()) {
        return false;
    }

    return Contains(header, "struct DeoptDiagnostics") &&
           Contains(header, "static bool GetLastDeoptDiagnostics(DeoptDiagnostics* out);") &&
           Contains(hook, "bool JavaHook::GetLastDeoptDiagnostics(DeoptDiagnostics* out)") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, result, \"reason\"") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, result, \"scanStart\"") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, result, \"scanEnd\"") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, result, \"candidatesSeen\"") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, result, \"readableCandidates\"") &&
           Contains(runtime, "JS_SetPropertyStr(ctx, result, \"runtimeOffset\"");
}

bool VerifyJavaDeoptGuardsJitCandidateBeforeCallingGetCodeCache() {
    const std::string hook = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                  "../../src/java_hook/JavaHook.cpp");
    if (hook.empty()) {
        return false;
    }

    const std::string begin = "static bool TryInvalidateJitCodeCache(";
    const std::string end = "bool HasAnyValidHookUnsafe()";
    const std::size_t start = hook.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = hook.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = hook.substr(start, finish - start);
    return Contains(body, "_ZTVN3art3jit3JitE") &&
           Contains(body, "_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv") &&
           Contains(body, "const uint64_t expected_jit_vtable = (jit_vtable_raw & kPacStripMask) + (2 * sizeof(uint64_t));") &&
           Contains(body, "if (first_word != expected_jit_vtable)") &&
           Contains(body, "const uint64_t code_cache = code_cache_raw & kPacStripMask;") &&
           Contains(body, "invalidate(code_cache);");
}

bool VerifyJavaHookInstallIsSerializedToAvoidDuplicateIds() {
    const std::string hook = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                  "../../src/java_hook/JavaHook.cpp");
    if (hook.empty()) {
        return false;
    }

    const std::string begin = "int JavaHook::HookMethod(";
    const std::string end = "bool JavaHook::Unhook(";
    const std::size_t start = hook.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = hook.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = hook.substr(start, finish - start);
    return Contains(hook, "std::mutex g_java_hook_install_mutex;") &&
           Contains(body, "std::lock_guard<std::mutex> install_lock(g_java_hook_install_mutex);") &&
           Contains(body, "uint32_t hookID = (uint32_t)HookStore<HookInfo>::Instance().Size();");
}

bool VerifyJavaImplementationInstallEnsuresJavaHookReady() {
    const std::string runtime = ReadFileWithFallback("src/agent_runtime/js_runtime.cpp",
                                                     "../../src/agent_runtime/js_runtime.cpp");
    if (runtime.empty()) {
        return false;
    }

    const std::string begin =
        "JSValue JsJavaInstallImplementation(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {";
    const std::string end = "JSValue JsJavaResolveOverloadSignature(";
    const std::size_t start = runtime.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = runtime.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = runtime.substr(start, finish - start);
    return Contains(body, "EnsureJavaHookReadyForJs(&error_message)") &&
           Contains(body, "return JS_ThrowInternalError(ctx, \"%s\", error_message.c_str());");
}

bool VerifyJavaFindClassLoadsAppClassesThroughLoader() {
    const std::string hook = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                  "../../src/java_hook/JavaHook.cpp");
    if (hook.empty()) {
        return false;
    }

    const std::string find_class_begin = "jclass JavaHook::FindClass(";
    const std::string find_class_with_loader_begin = "jclass JavaHook::FindClassWithLoader(";
    const std::string end = "ResolvedJavaMethod JavaHook::FindMethod(";
    const std::size_t find_class_start = hook.find(find_class_begin);
    const std::size_t find_class_with_loader_start = hook.find(find_class_with_loader_begin);
    if (find_class_start == std::string::npos || find_class_with_loader_start == std::string::npos) {
        return false;
    }
    const std::size_t finish = hook.find(end, find_class_start);
    if (finish == std::string::npos || finish <= find_class_start) {
        return false;
    }

    const std::string body = hook.substr(find_class_start, finish - find_class_start);
    return Contains(body, "return FindClassWithLoader(env, nullptr, className);") &&
           Contains(body, "const bool bootstrapClass = IsBootstrapClassName(slashName.c_str());") &&
           Contains(body, "if (bootstrapClass) {") &&
           Contains(body, "JavaHookLoaderResolver::FindLoadedClassWithLoader") &&
           Contains(body, "JavaHookLoaderResolver::LoadClassWithLoader");
}

bool VerifyLoaderAwareJavaHookApiExists() {
    const std::string header = ReadFileWithFallback("include/nook/NookJavaHook.h",
                                                    "../../include/nook/NookJavaHook.h");
    const std::string framework = ReadFileWithFallback("src/framework/NookJavaHook.cpp",
                                                       "../../src/framework/NookJavaHook.cpp");
    const std::string pending_header = ReadFileWithFallback("src/java_hook/deferred/pending_java_hook_registry.h",
                                                            "../../src/java_hook/deferred/pending_java_hook_registry.h");
    const std::string pending_cpp = ReadFileWithFallback("src/java_hook/deferred/pending_java_hook_registry.cpp",
                                                         "../../src/java_hook/deferred/pending_java_hook_registry.cpp");
    const std::string hook_header = ReadFileWithFallback("src/java_hook/JavaHook.h",
                                                         "../../src/java_hook/JavaHook.h");
    const std::string hook_cpp = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                      "../../src/java_hook/JavaHook.cpp");
    const std::string bridge = ReadFileWithFallback("src/agent_runtime/nook_java_js_bridge.cpp",
                                                    "../../src/agent_runtime/nook_java_js_bridge.cpp");
    if (header.empty() || framework.empty() || pending_header.empty() ||
        pending_cpp.empty() || hook_header.empty() || hook_cpp.empty() || bridge.empty()) {
        return false;
    }

    return Contains(header, "int NookJavaHookHookWithLoader(") &&
           Contains(header, "int NookJavaHookHookDeferredWithLoader(") &&
           Contains(header, "jclass NookJavaHookFindClassWithLoader(") &&
           Contains(framework, "int NookJavaHookHookWithLoader(") &&
           Contains(framework, "int NookJavaHookHookDeferredWithLoader(") &&
           Contains(framework, "jclass NookJavaHookFindClassWithLoader(") &&
           Contains(framework, "reinterpret_cast<uint64_t>(loader)") &&
           Contains(framework, "reinterpret_cast<jobject>(request.loader_handle)") &&
           Contains(pending_header, "uint64_t loader_handle = 0u;") &&
           Contains(pending_cpp, "request.loader_handle == loader_handle") &&
           Contains(hook_header, "static int HookMethodWithLoader(") &&
           Contains(hook_header, "static jclass FindClassWithLoader(") &&
           Contains(hook_cpp, "jclass JavaHook::FindClassWithLoader(") &&
           Contains(hook_cpp, "int JavaHook::HookMethodWithLoader(") &&
           Contains(bridge, "NookJavaHookHookDeferredWithLoader(") &&
           Contains(bridge, "resolved_request.loader_handle == 0u");
}

bool VerifySpawnedAgentArmsBootstrapHookAfterBridgeInit() {
    const std::string comm = ReadFileWithFallback("src/framework/NookComm.cpp",
                                                  "../../src/framework/NookComm.cpp");
    if (comm.empty()) {
        return false;
    }

    const std::string begin = "NookStatus NookAgentInitialize(void) {";
    const std::string end = "NookStatus NookCommInitialize(void) {";
    const std::size_t start = comm.find(begin);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t finish = comm.find(end, start);
    if (finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = comm.substr(start, finish - start);
    const std::string bridge_line =
        "const NookStatus bridge_status = nook::agent_runtime::NookScriptRuntimeBridgeInitialize();";
    const std::string bootstrap_line =
        "const NookStatus bootstrap_status = InstallSpawnGateBootstrapHookIfNeededLocked();";
    const std::string callback_line = "if (!ReportSpawnGateReadyIfNeeded()) {";
    const std::string return_line = "return NOOK_STATUS_OK;";
    const std::size_t bridge_pos = body.find(bridge_line);
    const std::size_t bootstrap_pos = body.find(bootstrap_line);
    const std::size_t callback_pos = body.find(callback_line);
    const std::size_t return_pos = body.rfind(return_line);
    return bridge_pos != std::string::npos &&
           bootstrap_pos != std::string::npos &&
           callback_pos != std::string::npos &&
           return_pos != std::string::npos &&
           bridge_pos < callback_pos &&
           bridge_pos < bootstrap_pos &&
           bootstrap_pos < callback_pos &&
           !Contains(body, "return NookCommWaitForResumeIfSpawned();");
}

bool VerifyJavaEnvCachesThreadAttachmentPerThread() {
    const std::string header = ReadFileWithFallback("src/java_hook/JVM.h",
                                                    "../../src/java_hook/JVM.h");
    const std::string source = ReadFileWithFallback("src/java_hook/JVM.cpp",
                                                    "../../src/java_hook/JVM.cpp");
    if (header.empty() || source.empty()) {
        return false;
    }

    const std::string destructor_begin = "JavaEnv::~JavaEnv() {";
    const std::string destructor_end = "JNIEnv* JavaEnv::get() const {";
    const std::size_t destructor_start = source.find(destructor_begin);
    const std::size_t destructor_finish = source.find(destructor_end, destructor_start);
    if (destructor_start == std::string::npos || destructor_finish == std::string::npos ||
        destructor_finish <= destructor_start) {
        return false;
    }

    const std::string destructor_body =
        source.substr(destructor_start, destructor_finish - destructor_start);
    return Contains(header, "bool acquired_from_cache = false;") &&
           Contains(source, "struct ThreadJavaEnvState {") &&
           Contains(source, "thread_local ThreadJavaEnvState g_thread_java_env_state;") &&
           Contains(source, "g_thread_java_env_state.env != nullptr") &&
           Contains(source, "g_thread_java_env_state.attached = true;") &&
           Contains(source, "g_thread_java_env_state.attached = false;") &&
           !Contains(destructor_body, "DetachCurrentThread()");
}

bool VerifyJavaHookInitCachesSuccessfulInitialization() {
    const std::string hook = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                  "../../src/java_hook/JavaHook.cpp");
    if (hook.empty()) {
        return false;
    }

    const std::string begin = "bool JavaHook::Init() {";
    const std::string end = "namespace {";
    const std::size_t start = hook.find(begin);
    const std::size_t finish = hook.find(end, start);
    if (start == std::string::npos || finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = hook.substr(start, finish - start);
    return Contains(hook, "std::mutex g_java_hook_init_mutex;") &&
           Contains(hook, "std::atomic<int> g_java_hook_init_once_state{0};") &&
           Contains(body, "if (g_java_hook_init_once_state.load(std::memory_order_acquire) == 2)") &&
           Contains(body, "std::lock_guard<std::mutex> init_lock(g_java_hook_init_mutex);") &&
           Contains(body, "g_java_hook_init_once_state.store(2, std::memory_order_release);") &&
           Contains(body, "g_java_hook_init_once_state.store(0, std::memory_order_release);");
}

bool VerifyJavaJitInvalidationCachesProcessWideSuccess() {
    const std::string hook = ReadFileWithFallback("src/java_hook/JavaHook.cpp",
                                                  "../../src/java_hook/JavaHook.cpp");
    if (hook.empty()) {
        return false;
    }

    const std::string begin = "static bool TryInvalidateJitCodeCache(bool* out_invalidated) {";
    const std::string end = "bool JavaHook::Init() {";
    const std::size_t start = hook.find(begin);
    const std::size_t finish = hook.find(end, start);
    if (start == std::string::npos || finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = hook.substr(start, finish - start);
    return Contains(hook, "std::atomic<bool> g_jit_code_cache_invalidated{false};") &&
           Contains(body, "if (g_jit_code_cache_invalidated.load(std::memory_order_acquire))") &&
           Contains(body, "*out_invalidated = true;") &&
           Contains(body, "g_jit_code_cache_invalidated.store(true, std::memory_order_release);");
}

bool VerifyNativeInlineHookHighFrequencyBypassExists() {
    const std::string bridge = ReadFileWithFallback("src/agent_runtime/nook_native_js_bridge.cpp",
                                                    "../../src/agent_runtime/nook_native_js_bridge.cpp");
    if (bridge.empty()) {
        return false;
    }

    const std::string begin = "uint64_t DispatchInlineHookSlot(size_t slot_index,";
    const std::string end = "template <size_t SlotIndex>";
    const std::size_t start = bridge.find(begin);
    const std::size_t finish = bridge.find(end, start);
    if (start == std::string::npos || finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = bridge.substr(start, finish - start);
    return !Contains(bridge, "g_native_js_inline_hook_throttle_counter") &&
           !Contains(bridge, "kHighFrequencyBlockingHookThrottleWindowNs") &&
           Contains(bridge, "thread_local uint32_t g_native_js_inline_hook_ignore_level = 0u;") &&
           Contains(bridge, "bool IsNativeJsInlineHookIgnoredOnCurrentThread()") &&
           Contains(body, "IsNativeJsInlineHookIgnoredOnCurrentThread()") &&
           Contains(body, "return original(x0, x1, x2, x3, x4, x5, x6, x7);");
}

bool VerifyFindExportByNameIsNotFilteredByInlineHookSafety() {
    const std::string bridge = ReadFileWithFallback("src/agent_runtime/nook_native_js_bridge.cpp",
                                                    "../../src/agent_runtime/nook_native_js_bridge.cpp");
    if (bridge.empty()) {
        return false;
    }

    const std::string begin = "bool FindNativeJsExportByName(";
    const std::string end = "bool UninstallNativeJsHook(";
    const std::size_t start = bridge.find(begin);
    const std::size_t finish = bridge.find(end, start);
    if (start == std::string::npos || finish == std::string::npos || finish <= start) {
        return false;
    }

    const std::string body = bridge.substr(start, finish - start);
    return !Contains(body, "IsResolvedInlineHookSymbolSafe(") &&
           Contains(body, "*target_address = reinterpret_cast<uint64_t>(address);");
}

}  // namespace

int main() {
    if (!VerifyDeferredInitAttemptsImmediately()) {
        std::cerr << "VerifyDeferredInitAttemptsImmediately failed\n";
        return 1;
    }
    if (!VerifyObjectArgsUseJniRefs()) {
        std::cerr << "VerifyObjectArgsUseJniRefs failed\n";
        return 1;
    }
    if (!VerifyStackReferenceDecodeGuardsUnreadableAddresses()) {
        std::cerr << "VerifyStackReferenceDecodeGuardsUnreadableAddresses failed\n";
        return 1;
    }
    if (!VerifyDeferredObserverAvoidsLoadClassHook()) {
        std::cerr << "VerifyDeferredObserverAvoidsLoadClassHook failed\n";
        return 1;
    }
    if (!VerifyJavaWildcardMethodResolutionExists()) {
        std::cerr << "VerifyJavaWildcardMethodResolutionExists failed\n";
        return 1;
    }
    if (!VerifyJavaJsBridgeResolvesWildcardSignatureBeforeDispatch()) {
        std::cerr << "VerifyJavaJsBridgeResolvesWildcardSignatureBeforeDispatch failed\n";
        return 1;
    }
    if (!VerifyJavaJsBridgeActiveInvocationTracksPerThreadStack()) {
        std::cerr << "VerifyJavaJsBridgeActiveInvocationTracksPerThreadStack failed\n";
        return 1;
    }
    if (!VerifyJavaJsBridgeMethodResolutionTraversesSuperclasses()) {
        std::cerr << "VerifyJavaJsBridgeMethodResolutionTraversesSuperclasses failed\n";
        return 1;
    }
    if (!VerifyJavaOriginalInvokeUsesFreshArtMethodClone()) {
        std::cerr << "VerifyJavaOriginalInvokeUsesFreshArtMethodClone failed\n";
        return 1;
    }
    if (!VerifyJavaOriginalInvokeHasReentryBypassGuard()) {
        std::cerr << "VerifyJavaOriginalInvokeHasReentryBypassGuard failed\n";
        return 1;
    }
    if (!VerifyJsRuntimeUsesRecursiveMutexForReentrantJavaHooks()) {
        std::cerr << "VerifyJsRuntimeUsesRecursiveMutexForReentrantJavaHooks failed\n";
        return 1;
    }
    if (!VerifyJsRuntimeCachesReadableMappingsAndPreservesPointerMetadataForMemoryReadUtf8()) {
        std::cerr << "VerifyJsRuntimeCachesReadableMappingsAndPreservesPointerMetadataForMemoryReadUtf8 failed\n";
        return 1;
    }
    if (!VerifyJavaOriginalInvokeAvoidsRelockingHookStoreDuringSizeCheck()) {
        std::cerr << "VerifyJavaOriginalInvokeAvoidsRelockingHookStoreDuringSizeCheck failed\n";
        return 1;
    }
    if (!VerifyJavaHookForcesInterpreterAndInvalidatesJitOnInstall()) {
        std::cerr << "VerifyJavaHookForcesInterpreterAndInvalidatesJitOnInstall failed\n";
        return 1;
    }
    if (!VerifyJavaUnhookDoesNotHoldStoreMutexAcrossSuspendAll()) {
        std::cerr << "VerifyJavaUnhookDoesNotHoldStoreMutexAcrossSuspendAll failed\n";
        return 1;
    }
    if (!VerifyJavaHookUsesReplacementRoutingPath()) {
        std::cerr << "VerifyJavaHookUsesReplacementRoutingPath failed\n";
        return 1;
    }
    if (!VerifyJavaStaticHookCallbackExecutesCallbackAndWritesReturn()) {
        std::cerr << "VerifyJavaStaticHookCallbackExecutesCallbackAndWritesReturn failed\n";
        return 1;
    }
    if (!VerifyJavaStaticHookRoutesOriginalQuickEntryThroughRouterStub()) {
        std::cerr << "VerifyJavaStaticHookRoutesOriginalQuickEntryThroughRouterStub failed\n";
        return 1;
    }
    if (!VerifyJavaStaticHookInstallsSharedArtRouters()) {
        std::cerr << "VerifyJavaStaticHookInstallsSharedArtRouters failed\n";
        return 1;
    }
    if (!VerifyJavaStaticHookInstallsDoCallRouterHooks()) {
        std::cerr << "VerifyJavaStaticHookInstallsDoCallRouterHooks failed\n";
        return 1;
    }
    if (!VerifyJavaStaticHookInstallsArtMaintenanceHooks()) {
        std::cerr << "VerifyJavaStaticHookInstallsArtMaintenanceHooks failed\n";
        return 1;
    }
    if (!VerifyJavaStaticHookNormalizesAccessFlagsLikeFrida()) {
        std::cerr << "VerifyJavaStaticHookNormalizesAccessFlagsLikeFrida failed\n";
        return 1;
    }
    if (!VerifyJavaDebugBindingsAreExposed()) {
        std::cerr << "VerifyJavaDebugBindingsAreExposed failed\n";
        return 1;
    }
    if (!VerifyJavaDeoptExposesDiagnostics()) {
        std::cerr << "VerifyJavaDeoptExposesDiagnostics failed\n";
        return 1;
    }
    if (!VerifyJavaDeoptGuardsJitCandidateBeforeCallingGetCodeCache()) {
        std::cerr << "VerifyJavaDeoptGuardsJitCandidateBeforeCallingGetCodeCache failed\n";
        return 1;
    }
    if (!VerifyJavaHookInstallIsSerializedToAvoidDuplicateIds()) {
        std::cerr << "VerifyJavaHookInstallIsSerializedToAvoidDuplicateIds failed\n";
        return 1;
    }
    if (!VerifyJavaImplementationInstallEnsuresJavaHookReady()) {
        std::cerr << "VerifyJavaImplementationInstallEnsuresJavaHookReady failed\n";
        return 1;
    }
    if (!VerifyJavaFindClassLoadsAppClassesThroughLoader()) {
        std::cerr << "VerifyJavaFindClassLoadsAppClassesThroughLoader failed\n";
        return 1;
    }
    if (!VerifyLoaderAwareJavaHookApiExists()) {
        std::cerr << "VerifyLoaderAwareJavaHookApiExists failed\n";
        return 1;
    }
    if (!VerifySpawnedAgentArmsBootstrapHookAfterBridgeInit()) {
        std::cerr << "VerifySpawnedAgentArmsBootstrapHookAfterBridgeInit failed\n";
        return 1;
    }
    if (!VerifyJavaEnvCachesThreadAttachmentPerThread()) {
        std::cerr << "VerifyJavaEnvCachesThreadAttachmentPerThread failed\n";
        return 1;
    }
    if (!VerifyJavaHookInitCachesSuccessfulInitialization()) {
        std::cerr << "VerifyJavaHookInitCachesSuccessfulInitialization failed\n";
        return 1;
    }
    if (!VerifyJavaJitInvalidationCachesProcessWideSuccess()) {
        std::cerr << "VerifyJavaJitInvalidationCachesProcessWideSuccess failed\n";
        return 1;
    }
    if (!VerifyNativeInlineHookHighFrequencyBypassExists()) {
        std::cerr << "VerifyNativeInlineHookHighFrequencyBypassExists failed\n";
        return 1;
    }
    if (!VerifyFindExportByNameIsNotFilteredByInlineHookSafety()) {
        std::cerr << "VerifyFindExportByNameIsNotFilteredByInlineHookSafety failed\n";
        return 1;
    }
    return 0;
}
