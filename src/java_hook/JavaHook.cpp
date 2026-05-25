#include "JavaHook.h"
#include "deferred/java_hook_loader_resolver.h"
#include "router/hook_engine.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include "../common/ArtStructDetector.h"
#include <sys/system_properties.h>

#define TRAMPOLINE_SIZE 0x10
static constexpr uint64_t kPacStripMask = 0x0000FFFFFFFFFFFFULL;

// ArtInternals 实现
namespace ArtInternals {
    DecodeMethodIdFn DecodeFunc = nullptr;
    ArtMethodInvoke Invoke = nullptr;
    CurrentFromGDB GetCurrentThread = nullptr;
    DecodeJObjectFn DecodeJObject = nullptr;
    ScopedGCSection SGCFn = nullptr;
    destroyScopedGCSection DestroyGCFn = nullptr;
    ScopedSuspendAll ScopedSuspendAllFn = nullptr;
    destroyScopedSuspendAll destroyScopedSuspendAllFn = nullptr;
    newlocalref newlocalrefFn = nullptr;

    uintptr_t RuntimeInstance = 0;
    void* jniIDManager = nullptr;
    ArtMethodSpec ArtMethodLayout = {0};
    ArtRuntimeSpecOffsets RunTimeSpec = {0};
    ClassLinkerSpecOffsets ClassLinkerSpec = {0};

    // 从 libart.so 获取符号地址
    static void* get_art_symbol(const char* symbol) {
        void* handle = dlopen("libart.so", RTLD_NOW);
        if (!handle) {
            LOGE("Failed to dlopen libart.so");
            return nullptr;
        }
        void* addr = dlsym(handle, symbol);
        dlclose(handle);
        return addr;
    }

    // 初始化 Art 内部函数和结构
    bool Init() {
        JavaEnv jenv;
        JavaVM* vm = jenv.getJVM();

        // 1. 查找 Runtime 实例和符号
        const char* libart_path = tool::find_path_from_maps("libart.so");
        if (!libart_path) {
            LOGE("Failed to find libart.so path");
            return false;
        }

        DecodeFunc = (DecodeMethodIdFn)tool::get_address_from_module(
            libart_path, "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID", true);
        if (!DecodeFunc) {
            LOGE("Failed to find DecodeMethodId");
            return false;
        }
        LOGI("DecodeFunc: %p", DecodeFunc);

        RuntimeInstance = reinterpret_cast<uintptr_t>(tool::get_address_from_module(
            libart_path, "_ZN3art7Runtime9instance_E", false));
        if (!RuntimeInstance) {
            LOGE("Failed to find Runtime instance");
            return false;
        }
        RuntimeInstance = *(uintptr_t*)RuntimeInstance;
        LOGI("RuntimeInstance: %p", (void*)RuntimeInstance);

        // 2. 探测 Runtime 偏移
        if (!ArtStructDetector::getArtRuntimeSpec((void*)RuntimeInstance, vm, &RunTimeSpec)) {
            LOGE("Failed to detect Runtime spec");
            return false;
        }

        LOGI("Runtime offsets: heap=0x%lx threadList=0x%lx internTable=0x%lx classLinker=0x%lx jniIdManager=0x%lx",
             RunTimeSpec.heap, RunTimeSpec.threadList, RunTimeSpec.internTable,
             RunTimeSpec.classLinker, RunTimeSpec.jniIdManager);

        jniIDManager = *(void**)(RuntimeInstance + RunTimeSpec.jniIdManager);
        LOGI("jniIDManager: %p", jniIDManager);

        // 3. 探测 ClassLinker 偏移
        if (!ArtStructDetector::tryGetArtClassLinkerSpec((void*)RuntimeInstance, &RunTimeSpec, &ClassLinkerSpec)) {
            LOGE("Failed to detect ClassLinker spec");
            return false;
        }

        LOGI("ClassLinker offsets: quickGenericJniTrampoline=0x%lx", ClassLinkerSpec.quickGenericJniTrampoline);

        // 4. 探测 ArtMethod 布局
        if (!ArtStructDetector::detect_artmethod_layout(jenv.get(), &ArtMethodLayout)) {
            LOGE("Failed to detect ArtMethod layout");
            return false;
        }

        // 5. 查找其他关键函数
        Invoke = (ArtMethodInvoke)tool::get_address_from_module(
            libart_path, "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", true);
        LOGI("Invoke: %p", Invoke);

        GetCurrentThread = (CurrentFromGDB)tool::get_address_from_module(
            libart_path, "_ZN3art6Thread14CurrentFromGdbEv", true);
        LOGI("GetCurrentThread: %p", GetCurrentThread);

        DecodeJObject = (DecodeJObjectFn)tool::get_address_from_module(
            libart_path, "_ZNK3art6Thread13DecodeJObjectEP8_jobject", false);
        LOGI("DecodeJObject: %p", DecodeJObject);

        SGCFn = (ScopedGCSection)tool::get_address_from_module(
            libart_path, "_ZN3art2gc23ScopedGCCriticalSectionC2EPNS_6ThreadENS0_7GcCauseENS0_13CollectorTypeE", true);
        LOGI("SGCFn: %p", SGCFn);

        DestroyGCFn = (destroyScopedGCSection)tool::get_address_from_module(
            libart_path, "_ZN3art2gc23ScopedGCCriticalSectionD2Ev", true);
        LOGI("DestroyGCFn: %p", DestroyGCFn);

        ScopedSuspendAllFn = (ScopedSuspendAll)tool::get_address_from_module(
            libart_path, "_ZN3art16ScopedSuspendAllC2EPKcb", true);
        LOGI("ScopedSuspendAllFn: %p", ScopedSuspendAllFn);

        destroyScopedSuspendAllFn = (destroyScopedSuspendAll)tool::get_address_from_module(
            libart_path, "_ZN3art16ScopedSuspendAllD2Ev", true);
        LOGI("destroyScopedSuspendAllFn: %p", destroyScopedSuspendAllFn);

        newlocalrefFn = (newlocalref)tool::get_address_from_module(
            libart_path, "_ZN3art9JNIEnvExt11NewLocalRefEPNS_6mirror6ObjectE", true);
        if (!newlocalrefFn) {
            newlocalrefFn = (newlocalref)tool::get_address_from_module(
                libart_path, "_ZN3art6JNIEnv11NewLocalRefEPNS_6mirror6ObjectE", true);
        }
        LOGI("newlocalrefFn: %p", newlocalrefFn);

        return true;
    }
}

static void write_hooked_artmethod(void* artMethod,
                                   const HookInfo& hookInfo,
                                   uint32_t hookedFlag,
                                   uint64_t hookedJniEntry,
                                   uint64_t hookedQuickEntry);

namespace {

struct InstrumentationSpec {
    size_t runtime_instrumentation_offset = 0;
    size_t force_interpret_only_offset = 4;
    bool is_pointer_mode = false;
};

struct ForcedInterpretOnlyState {
    bool active = false;
    uintptr_t instrumentation_base = 0;
    uint8_t saved_force_interpret_only = 0;
    size_t ref_count = 0;
};

struct StaticReplacementHookHandle {
    void* routerTrampoline = nullptr;
    void* routerHookedTarget = nullptr;
    void* routerStub = nullptr;
    void* nativeThunk = nullptr;
    void* replacementArtMethod = nullptr;
    bool ownsRouterHook = false;
};

constexpr size_t kJitScanStart = 0x100;
constexpr size_t kJitScanEnd = 0x1000;
constexpr uint64_t kLikelyHeapPointerFloor = 0x70000000ULL;

std::mutex g_forced_interpret_only_mutex;
ForcedInterpretOnlyState g_forced_interpret_only_state;
std::mutex g_java_hook_init_mutex;
std::atomic<int> g_java_hook_init_once_state{0};
std::atomic<bool> g_jit_code_cache_invalidated{false};
std::mutex g_java_hook_install_mutex;
std::mutex g_deopt_diagnostics_mutex;
DeoptDiagnostics g_last_deopt_diagnostics = {};
std::mutex g_hook_engine_mutex;
std::mutex g_shared_art_router_mutex;
std::mutex g_do_call_hook_mutex;
std::mutex g_art_maintenance_hook_mutex;
void* g_hook_engine_exec_mem = nullptr;
size_t g_hook_engine_exec_mem_size = 0;
bool g_shared_art_router_hooks_installed = false;
bool g_do_call_hooks_installed = false;
bool g_art_maintenance_hooks_installed = false;
std::atomic<bool> g_logged_do_call_replacement{false};
std::atomic<bool> g_logged_oat_replacement{false};
std::atomic<bool> g_logged_fixup_sync{false};

DeoptDiagnostics MakeDefaultDeoptDiagnostics() {
    DeoptDiagnostics diagnostics = {};
    diagnostics.reason = "not-run";
    diagnostics.scanStart = kJitScanStart;
    diagnostics.scanEnd = kJitScanEnd;
    diagnostics.runtimeAddress = ArtInternals::RuntimeInstance & kPacStripMask;
    return diagnostics;
}

void StoreLastDeoptDiagnostics(const DeoptDiagnostics& diagnostics) {
    std::lock_guard<std::mutex> lock(g_deopt_diagnostics_mutex);
    g_last_deopt_diagnostics = diagnostics;
}

int GetDeviceApiLevelCompat() {
    static int api_level = 0;
    if (api_level != 0) {
        return api_level;
    }

    char prop_value[PROP_VALUE_MAX] = {0};
    if (__system_property_get("ro.build.version.sdk", prop_value) > 0) {
        api_level = atoi(prop_value);
    }
    return api_level;
}

uint32_t GetCompileDontBotherFlagCompat() {
    const int api_level = GetDeviceApiLevelCompat();
    if (api_level >= 27) {
        return kAccCompileDontBother;
    }
    if (api_level >= 24) {
        return 0x01000000u;
    }
    return 0;
}

uint32_t GetPreCompiledFlagCompat() {
    const int api_level = GetDeviceApiLevelCompat();
    if (api_level == 30) {
        return 0x00200000u;
    }
    if (api_level >= 31) {
        return 0x00800000u;
    }
    return 0;
}

uint64_t GetArtApexVersionCompat() {
    static uint64_t version = 0;
    if (version != 0) {
        return version;
    }

    std::ifstream mountinfo("/proc/self/mountinfo");
    if (mountinfo.is_open()) {
        std::string line;
        std::unordered_map<std::string, uint64_t> source_versions;
        std::string art_source;
        while (std::getline(mountinfo, line)) {
            std::istringstream stream(line);
            std::vector<std::string> parts;
            std::string part;
            while (stream >> part) {
                parts.push_back(part);
            }
            if (parts.size() < 11 || parts[4].find("/apex/com.android.art") != 0) {
                continue;
            }

            const std::string& mount_root = parts[4];
            const std::string& mount_source = parts[10];
            const size_t at_pos = mount_root.find('@');
            if (at_pos != std::string::npos) {
                std::string digits;
                for (size_t i = at_pos + 1; i < mount_root.size(); ++i) {
                    const char ch = mount_root[i];
                    if (ch < '0' || ch > '9') {
                        break;
                    }
                    digits.push_back(ch);
                }
                if (!digits.empty()) {
                    source_versions[mount_source] = strtoull(digits.c_str(), nullptr, 10);
                }
            } else {
                art_source = mount_source;
            }
        }

        if (!art_source.empty()) {
            const auto it = source_versions.find(art_source);
            if (it != source_versions.end()) {
                version = it->second;
                return version;
            }
        }
    }

    version = static_cast<uint64_t>(GetDeviceApiLevelCompat()) * 10000000ULL;
    return version;
}

void HookEngineLog(const char* msg) {
    if (msg != nullptr) {
        LOGI("[hook_engine] %s", msg);
    }
}

bool EnsureHookEngineInitialized() {
    std::lock_guard<std::mutex> lock(g_hook_engine_mutex);
    if (g_hook_engine_exec_mem != nullptr) {
        return true;
    }

    constexpr size_t kHookEnginePoolSize = 1u << 20;
    g_hook_engine_exec_mem = tool::allocate_exec_mem(kHookEnginePoolSize);
    if (g_hook_engine_exec_mem == nullptr) {
        LOGE("EnsureHookEngineInitialized: exec pool allocation failed");
        return false;
    }

    if (hook_engine_init(g_hook_engine_exec_mem, kHookEnginePoolSize) != 0) {
        LOGE("EnsureHookEngineInitialized: hook_engine_init failed");
        tool::free_exec_mem(g_hook_engine_exec_mem, kHookEnginePoolSize);
        g_hook_engine_exec_mem = nullptr;
        return false;
    }

    hook_engine_set_log_fn(HookEngineLog);
    g_hook_engine_exec_mem_size = kHookEnginePoolSize;
    LOGI("EnsureHookEngineInitialized: ok pool=%p size=0x%zx",
         g_hook_engine_exec_mem,
         g_hook_engine_exec_mem_size);
    return true;
}

void* ResolveArtTrampolineTargetCompat(void* target, JNIEnv* env) {
    if (target == nullptr || env == nullptr) {
        return target;
    }

    uint8_t buf[8] = {0};
    std::memcpy(buf, target, sizeof(buf));
    const uint32_t insn0 = *reinterpret_cast<uint32_t*>(buf);
    const uint32_t insn1 = *reinterpret_cast<uint32_t*>(buf + 4);

    if ((insn0 & 0xFFC003E0u) != 0xF9400260u) {
        return target;
    }

    const uint32_t rt_ldr = insn0 & 0x1Fu;
    const uint32_t rn_br = (insn1 >> 5) & 0x1Fu;
    if ((insn1 & 0xFFFFFC1Fu) != 0xD61F0000u || rt_ldr != rn_br) {
        return target;
    }

    const uint32_t imm12 = (insn0 >> 10) & 0xFFFu;
    const uint64_t offset = static_cast<uint64_t>(imm12) * 8u;
    const uint64_t jni_env_addr = reinterpret_cast<uint64_t>(env);
    const uint64_t thread = *reinterpret_cast<uint64_t*>(jni_env_addr + 8) & kPacStripMask;
    if (thread == 0) {
        return target;
    }

    const uint64_t resolved = *reinterpret_cast<uint64_t*>(thread + offset) & kPacStripMask;
    return resolved != 0 ? reinterpret_cast<void*>(resolved) : target;
}

bool EnsureSharedArtRouterHooksInstalled(JNIEnv* env) {
    if (env == nullptr) {
        return false;
    }
    if (!EnsureHookEngineInitialized()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_shared_art_router_mutex);
    if (g_shared_art_router_hooks_installed) {
        return true;
    }

    void* classLinker = *(void**)(ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
    if (classLinker == nullptr) {
        LOGE("EnsureSharedArtRouterHooksInstalled: classLinker unavailable");
        return false;
    }

    struct SharedRouteTarget {
        const char* name;
        void* address;
    };

    SharedRouteTarget targets[] = {
        {
            "quick_generic_jni_trampoline",
            *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickGenericJniTrampoline)
        },
        {
            "quick_to_interpreter_bridge",
            *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickToInterpreterBridgeTrampoline)
        },
        {
            "quick_resolution_trampoline",
            *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickResolutionTrampoline)
        },
    };

    for (const auto& target : targets) {
        if (target.address == nullptr) {
            LOGI("EnsureSharedArtRouterHooksInstalled: skip %s (null)", target.name);
            continue;
        }

        void* hooked_target = nullptr;
        void* trampoline = hook_install_art_router(
            target.address,
            static_cast<uint32_t>(ArtInternals::ArtMethodLayout.offset_entry_quick),
            0,
            env,
            &hooked_target);
        if (trampoline == nullptr) {
            LOGE("EnsureSharedArtRouterHooksInstalled: hook_install_art_router failed for %s (%p)",
                 target.name,
                 target.address);
            return false;
        }

        LOGI("EnsureSharedArtRouterHooksInstalled: %s hooked=%p trampoline=%p",
             target.name,
             hooked_target != nullptr ? hooked_target : target.address,
             trampoline);
    }

    g_shared_art_router_hooks_installed = true;
    return true;
}

static std::vector<void*> FindDoCallTargets() {
    std::vector<void*> targets;
    const char* libart_path = tool::find_path_from_maps("libart.so");
    if (libart_path == nullptr) {
        return targets;
    }

    const int api_level = GetDeviceApiLevelCompat();
    std::vector<std::string> symbols;
    if (api_level <= 22) {
        for (const char* b0 : {"0", "1"}) {
            for (const char* b1 : {"0", "1"}) {
                symbols.emplace_back(
                    std::string("_ZN3art11interpreter6DoCallILb") + b0 +
                    "ELb" + b1 +
                    "EEEbPNS_6mirror9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtPNS_6JValueE");
            }
        }
    } else if (api_level <= 33) {
        for (const char* b0 : {"0", "1"}) {
            for (const char* b1 : {"0", "1"}) {
                symbols.emplace_back(
                    std::string("_ZN3art11interpreter6DoCallILb") + b0 +
                    "ELb" + b1 +
                    "EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtPNS_6JValueE");
            }
        }
    } else {
        for (const char* b0 : {"0", "1"}) {
            symbols.emplace_back(
                std::string("_ZN3art11interpreter6DoCallILb") + b0 +
                "EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtbPNS_6JValueE");
        }
    }

    std::unordered_set<void*> dedup;
    for (const auto& symbol : symbols) {
        void* target = tool::get_address_from_module(libart_path, symbol.c_str(), true);
        if (target != nullptr && dedup.insert(target).second) {
            targets.push_back(target);
        }
    }
    return targets;
}

static void DoCallEnterCallback(HookContext* ctx, void* user_data) {
    (void)user_data;
    if (ctx == nullptr) {
        return;
    }

    const uint64_t original_method = ctx->x[0];
    if (original_method == 0) {
        return;
    }

    const uint64_t replacement_method = hook_art_router_lookup(original_method);
    if (replacement_method == 0) {
        return;
    }

    bool expected = false;
    if (g_logged_do_call_replacement.compare_exchange_strong(expected, true)) {
        LOGI("DoCallEnterCallback: original=%p replacement=%p",
             reinterpret_cast<void*>(original_method),
             reinterpret_cast<void*>(replacement_method));
    }

    const uint32_t declaring_class = *reinterpret_cast<volatile uint32_t*>(original_method);
    *reinterpret_cast<volatile uint32_t*>(replacement_method) = declaring_class;
    ctx->x[0] = replacement_method;
}

static bool EnsureDoCallHooksInstalled() {
    if (!EnsureHookEngineInitialized()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_do_call_hook_mutex);
    if (g_do_call_hooks_installed) {
        return true;
    }

    const auto targets = FindDoCallTargets();
    for (void* target : targets) {
        const int ret = hook_attach(target, DoCallEnterCallback, nullptr, nullptr, 0);
        if (ret == HOOK_OK || ret == HOOK_ERROR_ALREADY_HOOKED) {
            LOGI("EnsureDoCallHooksInstalled: hooked DoCall target=%p ret=%d", target, ret);
            continue;
        }
        LOGE("EnsureDoCallHooksInstalled: hook_attach failed target=%p ret=%d", target, ret);
        return false;
    }

    LOGI("EnsureDoCallHooksInstalled: installed=%zu", targets.size());
    g_do_call_hooks_installed = true;
    return true;
}

bool IsReadableAddress(uintptr_t address) {
    address &= kPacStripMask;
    if (address == 0) {
        return false;
    }

    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        char perms[5] = {0};
        if (sscanf(line.c_str(), "%lx-%lx %4s", &start, &end, perms) != 3) {
            continue;
        }
        if (address >= start && address < end) {
            return perms[0] == 'r';
        }
    }
    return false;
}

bool TryReadU32(uintptr_t address, uint32_t* out_value) {
    address &= kPacStripMask;
    if (out_value == nullptr || !IsReadableAddress(address)) {
        return false;
    }
    *out_value = *reinterpret_cast<const uint32_t*>(address);
    return true;
}

bool TryReadU64(uintptr_t address, uint64_t* out_value) {
    address &= kPacStripMask;
    if (out_value == nullptr || !IsReadableAddress(address)) {
        return false;
    }
    *out_value = *reinterpret_cast<const uint64_t*>(address);
    return true;
}

bool ProbeInstrumentationSpecForMode(bool is_pointer_mode, InstrumentationSpec* out_spec) {
    if (out_spec == nullptr) {
        return false;
    }

    const char* libart_path = tool::find_path_from_maps("libart.so");
    if (libart_path == nullptr) {
        LOGE("ProbeInstrumentationSpec: libart.so path not found");
        return false;
    }

    void* symbol = tool::get_address_from_module(
        libart_path, "_ZN3art7Runtime19DeoptimizeBootImageEv", true);
    if (symbol == nullptr) {
        LOGE("ProbeInstrumentationSpec: DeoptimizeBootImage symbol not found");
        return false;
    }

    const uintptr_t func_addr = reinterpret_cast<uintptr_t>(symbol);
    for (uint64_t i = 0; i < 30; ++i) {
        uint32_t insn = 0;
        if (!TryReadU32(func_addr + i * 4, &insn)) {
            continue;
        }

        if (is_pointer_mode) {
            if ((insn & 0xFFC00000u) != 0xF9400000u) {
                continue;
            }
            const uint32_t rt = insn & 0x1Fu;
            const uint32_t rn = (insn >> 5) & 0x1Fu;
            const size_t offset = static_cast<size_t>((insn >> 10) & 0xFFFu) * 8u;
            if (rt == 0 || rn != 0 || offset < 0x100 || offset > 0x400) {
                continue;
            }

            out_spec->runtime_instrumentation_offset = offset;
            out_spec->force_interpret_only_offset = 4;
            out_spec->is_pointer_mode = true;
            LOGI("ProbeInstrumentationSpecForMode: pointer mode offset=0x%zx", offset);
            return true;
        }

        const uint32_t masked = insn & 0xFF800000u;
        if (masked != 0x91000000u && masked != 0x91400000u) {
            continue;
        }
        const uint32_t rd = insn & 0x1Fu;
        const uint32_t rn = (insn >> 5) & 0x1Fu;
        const size_t imm12 = static_cast<size_t>((insn >> 10) & 0xFFFu);
        const size_t shift = static_cast<size_t>((insn >> 22) & 0x3u);
        const size_t offset = shift == 1 ? (imm12 << 12u) : imm12;
        if (rd == 31 || rn == 31 || offset < 0x100 || offset > 0x400) {
            continue;
        }

        out_spec->runtime_instrumentation_offset = offset;
        out_spec->force_interpret_only_offset = 4;
        out_spec->is_pointer_mode = false;
        LOGI("ProbeInstrumentationSpecForMode: embedded mode offset=0x%zx", offset);
        return true;
    }

    LOGE("ProbeInstrumentationSpecForMode: instrumentation offset not detected");
    return false;
}

bool ProbeInstrumentationSpec(InstrumentationSpec* out_spec) {
    if (out_spec == nullptr) {
        return false;
    }

    const bool prefer_pointer_mode = GetArtApexVersionCompat() >= 360000000ULL;
    if (ProbeInstrumentationSpecForMode(prefer_pointer_mode, out_spec)) {
        return true;
    }
    if (ProbeInstrumentationSpecForMode(!prefer_pointer_mode, out_spec)) {
        return true;
    }
    return false;
}

bool ResolveInstrumentationBase(const InstrumentationSpec& spec, uintptr_t* out_base) {
    if (out_base == nullptr || ArtInternals::RuntimeInstance == 0) {
        return false;
    }

    const uintptr_t runtime_addr = ArtInternals::RuntimeInstance & kPacStripMask;
    const uintptr_t direct_base = runtime_addr + spec.runtime_instrumentation_offset;
    uint64_t pointer_raw = 0;
    const bool pointer_read_ok =
        TryReadU64(runtime_addr + spec.runtime_instrumentation_offset, &pointer_raw);
    const uintptr_t pointer_base =
        static_cast<uintptr_t>(pointer_raw & kPacStripMask);

    auto can_use_base = [&](uintptr_t candidate) -> bool {
        return candidate != 0 &&
               IsReadableAddress(candidate) &&
               IsReadableAddress(candidate + spec.force_interpret_only_offset);
    };

    if (spec.is_pointer_mode) {
        if (can_use_base(pointer_base)) {
            *out_base = pointer_base;
            return true;
        }
        if (can_use_base(direct_base)) {
            LOGI("ResolveInstrumentationBase: pointer probe fallback to embedded base=%p raw=%p",
                 reinterpret_cast<void*>(direct_base),
                 reinterpret_cast<void*>(pointer_base));
            *out_base = direct_base;
            return true;
        }
    } else {
        if (can_use_base(direct_base)) {
            *out_base = direct_base;
            return true;
        }
        if (can_use_base(pointer_base)) {
            LOGI("ResolveInstrumentationBase: embedded probe fallback to pointer base=%p raw=%p",
                 reinterpret_cast<void*>(pointer_base),
                 reinterpret_cast<void*>(pointer_base));
            *out_base = pointer_base;
            return true;
        }
    }

    LOGE("ResolveInstrumentationBase: failed mode=%s runtime=%p offset=0x%zx direct=%p direct_r=%d ptr_read=%d ptr_raw=%p ptr_r=%d",
         spec.is_pointer_mode ? "pointer" : "embedded",
         reinterpret_cast<void*>(runtime_addr),
         spec.runtime_instrumentation_offset,
         reinterpret_cast<void*>(direct_base),
         can_use_base(direct_base) ? 1 : 0,
         pointer_read_ok ? 1 : 0,
         reinterpret_cast<void*>(pointer_base),
         can_use_base(pointer_base) ? 1 : 0);
    return false;
}

static bool TrySetForcedInterpretOnly(bool* out_changed) {
    if (out_changed != nullptr) {
        *out_changed = false;
    }

    std::lock_guard<std::mutex> lock(g_forced_interpret_only_mutex);
    if (g_forced_interpret_only_state.ref_count > 0) {
        ++g_forced_interpret_only_state.ref_count;
        return true;
    }

    InstrumentationSpec spec;
    if (!ProbeInstrumentationSpec(&spec)) {
        return false;
    }

    uintptr_t instrumentation_base = 0;
    if (!ResolveInstrumentationBase(spec, &instrumentation_base)) {
        InstrumentationSpec fallback_spec = spec;
        fallback_spec.is_pointer_mode = !spec.is_pointer_mode;
        if (!ProbeInstrumentationSpecForMode(fallback_spec.is_pointer_mode, &fallback_spec) ||
            !ResolveInstrumentationBase(fallback_spec, &instrumentation_base)) {
            LOGE("TrySetForcedInterpretOnly: instrumentation base unresolved");
            return false;
        }
        spec = fallback_spec;
    }

    auto* field = reinterpret_cast<uint8_t*>(instrumentation_base + spec.force_interpret_only_offset);
    const uint8_t previous = *field;
    *field = 1;

    g_forced_interpret_only_state.active = true;
    g_forced_interpret_only_state.instrumentation_base = instrumentation_base;
    g_forced_interpret_only_state.saved_force_interpret_only = previous;
    g_forced_interpret_only_state.ref_count = 1;
    if (out_changed != nullptr) {
        *out_changed = previous != 1;
    }

    LOGI("TrySetForcedInterpretOnly: instrumentation=%p old=%u new=%u",
         reinterpret_cast<void*>(instrumentation_base),
         static_cast<unsigned>(previous),
         1u);
    return true;
}

static bool IsReplacementMethodPointer(uint64_t method_ptr) {
    if (method_ptr == 0) {
        return false;
    }

    std::mutex& store_mtx = HookStore<HookInfo>::Instance().GetMutex();
    std::lock_guard<std::mutex> lock(store_mtx);
    const size_t count = HookStore<HookInfo>::Instance().SizeUnsafe();
    for (size_t i = 0; i < count; ++i) {
        HookInfo& info = HookStore<HookInfo>::Instance().GetUnsafe(i);
        if (!info.valid || !info.usesStaticReplacement) {
            continue;
        }
        auto* handle = reinterpret_cast<StaticReplacementHookHandle*>(info.staticReplacementHookHandle);
        if (handle != nullptr &&
            reinterpret_cast<uint64_t>(handle->replacementArtMethod) == method_ptr) {
            return true;
        }
    }
    return false;
}

static void SynchronizeStaticReplacementHooks() {
    std::mutex& store_mtx = HookStore<HookInfo>::Instance().GetMutex();
    std::lock_guard<std::mutex> lock(store_mtx);
    const size_t count = HookStore<HookInfo>::Instance().SizeUnsafe();
    for (size_t i = 0; i < count; ++i) {
        HookInfo& info = HookStore<HookInfo>::Instance().GetUnsafe(i);
        if (!info.valid || !info.usesStaticReplacement) {
            continue;
        }

        auto* handle = reinterpret_cast<StaticReplacementHookHandle*>(info.staticReplacementHookHandle);
        if (handle == nullptr || handle->replacementArtMethod == nullptr) {
            continue;
        }

        const uint32_t declaring_class =
            *reinterpret_cast<volatile uint32_t*>(info.artMethod);
        *reinterpret_cast<volatile uint32_t*>(handle->replacementArtMethod) = declaring_class;

        write_hooked_artmethod(info.artMethod,
                               info,
                               info.hookedFlag,
                               info.hookedJNIEntry,
                               info.hookedEntryPoint);
    }
}

static void OatQuickMethodHeaderReplaceCallback(HookContext* ctx, void* user_data) {
    (void)user_data;
    if (ctx == nullptr) {
        return;
    }

    const uint64_t method = ctx->x[0];
    if (IsReplacementMethodPointer(method)) {
        bool expected = false;
        if (g_logged_oat_replacement.compare_exchange_strong(expected, true)) {
            LOGI("OatQuickMethodHeaderReplaceCallback: replacement=%p -> null",
                 reinterpret_cast<void*>(method));
        }
        ctx->x[0] = 0;
        return;
    }

    if (ctx->trampoline != nullptr) {
        ctx->x[0] = hook_invoke_trampoline(ctx, ctx->trampoline);
    }
}

static void FixupStaticTrampolinesLeaveCallback(HookContext* ctx, void* user_data) {
    (void)ctx;
    (void)user_data;
    bool expected = false;
    if (g_logged_fixup_sync.compare_exchange_strong(expected, true)) {
        LOGI("FixupStaticTrampolinesLeaveCallback: synchronize static replacements");
    }
    SynchronizeStaticReplacementHooks();
}

static bool EnsureArtMaintenanceHooksInstalled() {
    if (!EnsureHookEngineInitialized()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_art_maintenance_hook_mutex);
    if (g_art_maintenance_hooks_installed) {
        return true;
    }

    const char* libart_path = tool::find_path_from_maps("libart.so");
    if (libart_path == nullptr) {
        return false;
    }

    const char* oat_header_symbols[] = {
        "_ZN3art9ArtMethod23GetOatQuickMethodHeaderEm",
        "_ZN3art9ArtMethod23GetOatQuickMethodHeaderEj",
    };
    const char* fixup_static_symbols[] = {
        "_ZN3art11ClassLinker40MakeInitializedClassesVisiblyInitializedEPNS_6ThreadEb",
        "_ZN3art11ClassLinker22FixupStaticTrampolinesEPNS_6ThreadENS_6ObjPtrINS_6mirror5ClassEEE",
        "_ZN3art11ClassLinker22FixupStaticTrampolinesEPNS_6ObjPtrINS_6mirror5ClassEEE",
        "_ZN3art11ClassLinker22FixupStaticTrampolinesEPNS_6mirror5ClassE",
    };

    for (const char* symbol : oat_header_symbols) {
        void* target = tool::get_address_from_module(libart_path, symbol, true);
        if (target == nullptr) {
            continue;
        }
        void* trampoline = hook_replace(target, OatQuickMethodHeaderReplaceCallback, nullptr, 0);
        LOGI("EnsureArtMaintenanceHooksInstalled: GetOatQuickMethodHeader target=%p trampoline=%p",
             target,
             trampoline);
        break;
    }

    for (const char* symbol : fixup_static_symbols) {
        void* target = tool::get_address_from_module(libart_path, symbol, true);
        if (target == nullptr) {
            continue;
        }
        const int ret = hook_attach(target, nullptr, FixupStaticTrampolinesLeaveCallback, nullptr, 0);
        LOGI("EnsureArtMaintenanceHooksInstalled: FixupStaticTrampolines target=%p ret=%d",
             target,
             ret);
        if (ret == HOOK_OK || ret == HOOK_ERROR_ALREADY_HOOKED) {
            break;
        }
    }

    g_art_maintenance_hooks_installed = true;
    return true;
}

void ReleaseForcedInterpretOnly() {
    std::lock_guard<std::mutex> lock(g_forced_interpret_only_mutex);
    if (g_forced_interpret_only_state.ref_count == 0) {
        return;
    }

    --g_forced_interpret_only_state.ref_count;
    if (g_forced_interpret_only_state.ref_count != 0 || !g_forced_interpret_only_state.active) {
        return;
    }

    auto* field = reinterpret_cast<uint8_t*>(
        g_forced_interpret_only_state.instrumentation_base + 4);
    if (IsReadableAddress(reinterpret_cast<uintptr_t>(field))) {
        *field = g_forced_interpret_only_state.saved_force_interpret_only;
        LOGI("ReleaseForcedInterpretOnly: restore=%u",
             static_cast<unsigned>(g_forced_interpret_only_state.saved_force_interpret_only));
    }

    g_forced_interpret_only_state = ForcedInterpretOnlyState{};
}

static bool TryInvalidateJitCodeCache(bool* out_invalidated) {
    if (out_invalidated != nullptr) {
        *out_invalidated = false;
    }

    if (g_jit_code_cache_invalidated.load(std::memory_order_acquire)) {
        if (out_invalidated != nullptr) {
            *out_invalidated = true;
        }
        return true;
    }

    DeoptDiagnostics diagnostics = MakeDefaultDeoptDiagnostics();
    const char* libart_path = tool::find_path_from_maps("libart.so");
    if (libart_path == nullptr) {
        diagnostics.reason = "libart-not-found";
        StoreLastDeoptDiagnostics(diagnostics);
        return false;
    }

    using InvalidateAllMethodsFn = void(*)(uint64_t);

    auto invalidate = reinterpret_cast<InvalidateAllMethodsFn>(tool::get_address_from_module(
        libart_path, "_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv", true));
    const uint64_t jit_vtable_raw = reinterpret_cast<uint64_t>(tool::get_address_from_module(
        libart_path, "_ZTVN3art3jit3JitE", false));
    diagnostics.symbolsAvailable = invalidate != nullptr && jit_vtable_raw != 0;
    diagnostics.runtimeAvailable = ArtInternals::RuntimeInstance != 0;

    if (invalidate == nullptr || jit_vtable_raw == 0 || ArtInternals::RuntimeInstance == 0) {
        diagnostics.reason = ArtInternals::RuntimeInstance == 0
            ? "runtime-unavailable"
            : (invalidate == nullptr
                ? "invalidate-symbol-unavailable"
                : "jit-vtable-unavailable");
        StoreLastDeoptDiagnostics(diagnostics);
        LOGE("TryInvalidateJitCodeCache: required symbols unavailable invalidate=%d jit_vtable=%p runtime=%p",
             invalidate != nullptr ? 1 : 0,
             reinterpret_cast<void*>(jit_vtable_raw),
             reinterpret_cast<void*>(ArtInternals::RuntimeInstance));
        return false;
    }

    // Itanium C++ ABI: object vptr points to the address point, not the start of the vtable.
    const uint64_t expected_jit_vtable = (jit_vtable_raw & kPacStripMask) + (2 * sizeof(uint64_t));
    bool saw_jit_like_candidate = false;
    for (size_t offset = diagnostics.scanStart; offset < diagnostics.scanEnd; offset += sizeof(uint64_t)) {
        uint64_t candidate_raw = 0;
        if (!TryReadU64(ArtInternals::RuntimeInstance + offset, &candidate_raw)) {
            continue;
        }
        ++diagnostics.candidatesSeen;

        const uint64_t candidate = candidate_raw & kPacStripMask;
        if (candidate < kLikelyHeapPointerFloor ||
            !IsReadableAddress(static_cast<uintptr_t>(candidate))) {
            continue;
        }
        ++diagnostics.readableCandidates;

        uint64_t first_word = 0;
        if (!TryReadU64(static_cast<uintptr_t>(candidate), &first_word) || first_word == 0) {
            continue;
        }
        if (first_word != expected_jit_vtable) {
            continue;
        }
        saw_jit_like_candidate = true;

        uint64_t code_cache_raw = 0;
        if (!TryReadU64(static_cast<uintptr_t>(candidate + sizeof(uint64_t)), &code_cache_raw)) {
            diagnostics.runtimeOffset = offset;
            diagnostics.reason = "jit-found-code-cache-read-failed";
            break;
        }

        const uint64_t code_cache = code_cache_raw & kPacStripMask;
        if (code_cache < kLikelyHeapPointerFloor ||
            !IsReadableAddress(static_cast<uintptr_t>(code_cache))) {
            diagnostics.runtimeOffset = offset;
            diagnostics.reason = "jit-found-code-cache-unreadable";
            break;
        }

        diagnostics.codeCacheAddress = code_cache;
        diagnostics.runtimeOffset = offset;
        invalidate(code_cache);
        g_jit_code_cache_invalidated.store(true, std::memory_order_release);
        if (out_invalidated != nullptr) {
            *out_invalidated = true;
        }
        diagnostics.invalidated = true;
        diagnostics.reason = "invalidated";
        StoreLastDeoptDiagnostics(diagnostics);
        LOGI("TryInvalidateJitCodeCache: code_cache=%p runtime_offset=0x%zx via_jit_vtable=%p",
             reinterpret_cast<void*>(code_cache),
             offset,
             reinterpret_cast<void*>(expected_jit_vtable));
        return true;
    }

    diagnostics.reason = saw_jit_like_candidate
        ? diagnostics.reason
        : (diagnostics.candidatesSeen == 0
            ? "runtime-scan-empty"
            : (diagnostics.readableCandidates == 0
                ? "no-readable-jit-candidates"
                : "jit-pointer-not-found"));
    StoreLastDeoptDiagnostics(diagnostics);
    LOGE("TryInvalidateJitCodeCache: skipped unsafe invalidation runtime=%p scan=[0x%zx,0x%zx) candidates=%zu readable=%zu reason=%s",
         reinterpret_cast<void*>(diagnostics.runtimeAddress),
         diagnostics.scanStart,
         diagnostics.scanEnd,
         diagnostics.candidatesSeen,
         diagnostics.readableCandidates,
         diagnostics.reason.c_str());
    return false;
}

bool HasAnyValidHookUnsafe() {
    auto& store = HookStore<HookInfo>::Instance();
    const size_t count = store.SizeUnsafe();
    for (size_t i = 0; i < count; ++i) {
        if (store.GetUnsafe(i).valid) {
            return true;
        }
    }
    return false;
}

static void StaticNativeHookCallback(HookContext* ctx, void* user_data);

static bool InstallStaticReplacementHook(HookInfo* hook_info, uint32_t hook_id) {
    if (hook_info == nullptr || hook_info->artMethod == nullptr) {
        return false;
    }
    if (!EnsureHookEngineInitialized()) {
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        LOGE("InstallStaticReplacementHook: JNIEnv unavailable");
        return false;
    }

    if (!EnsureSharedArtRouterHooksInstalled(jenv.get())) {
        LOGE("InstallStaticReplacementHook: EnsureSharedArtRouterHooksInstalled failed");
        return false;
    }
    if (!EnsureDoCallHooksInstalled()) {
        LOGE("InstallStaticReplacementHook: EnsureDoCallHooksInstalled failed");
        return false;
    }
    if (!EnsureArtMaintenanceHooksInstalled()) {
        LOGE("InstallStaticReplacementHook: EnsureArtMaintenanceHooksInstalled failed");
    }

    void* classLinker = *(void**)(ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
    void* quickGenericJniTrampoline =
        *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickGenericJniTrampoline);
    void* quickToInterpreterBridge =
        *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickToInterpreterBridgeTrampoline);
    void* quickResolutionTrampoline =
        *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickResolutionTrampoline);
    void* resolvedQuickGenericJniTrampoline =
        ResolveArtTrampolineTargetCompat(quickGenericJniTrampoline, jenv.get());
    void* resolvedQuickToInterpreterBridge =
        ResolveArtTrampolineTargetCompat(quickToInterpreterBridge, jenv.get());
    void* resolvedQuickResolutionTrampoline =
        ResolveArtTrampolineTargetCompat(quickResolutionTrampoline, jenv.get());
    if (quickGenericJniTrampoline == nullptr) {
        LOGE("InstallStaticReplacementHook: quickGenericJniTrampoline unavailable");
        return false;
    }

    auto* handle = new (std::nothrow) StaticReplacementHookHandle();
    if (handle == nullptr) {
        return false;
    }

    handle->nativeThunk = hook_create_native_trampoline(
        reinterpret_cast<uint64_t>(hook_info->artMethod),
        StaticNativeHookCallback,
        reinterpret_cast<void*>(static_cast<uintptr_t>(hook_id)));
    if (handle->nativeThunk == nullptr) {
        delete handle;
        LOGE("InstallStaticReplacementHook: hook_create_native_trampoline failed");
        return false;
    }

    auto* replacement = new (std::nothrow) uint8_t[hook_info->layout.art_method_size];
    if (replacement == nullptr) {
        hook_remove_redirect(reinterpret_cast<uint64_t>(hook_info->artMethod));
        delete handle;
        return false;
    }

    std::memcpy(replacement, hook_info->artMethod, hook_info->layout.art_method_size);
    handle->replacementArtMethod = replacement;

    *reinterpret_cast<uint32_t*>(replacement + hook_info->layout.offset_access_flags) =
        hook_info->hookedFlag;
    *reinterpret_cast<uint64_t*>(replacement + hook_info->layout.offset_entry_jni) =
        reinterpret_cast<uint64_t>(handle->nativeThunk);
    *reinterpret_cast<uint64_t*>(replacement + hook_info->layout.offset_entry_quick) =
        reinterpret_cast<uint64_t>(quickGenericJniTrampoline);

    if (hook_art_router_table_add(reinterpret_cast<uint64_t>(hook_info->artMethod),
                                  reinterpret_cast<uint64_t>(handle->replacementArtMethod)) != 0) {
        hook_remove_redirect(reinterpret_cast<uint64_t>(hook_info->artMethod));
        delete[] replacement;
        delete handle;
        LOGE("InstallStaticReplacementHook: hook_art_router_table_add failed");
        return false;
    }

    const void* const original_entry = reinterpret_cast<void*>(hook_info->orgEntryPoint);
    const bool uses_shared_art_stub =
        original_entry == quickGenericJniTrampoline ||
        original_entry == quickToInterpreterBridge ||
        original_entry == quickResolutionTrampoline ||
        original_entry == resolvedQuickGenericJniTrampoline ||
        original_entry == resolvedQuickToInterpreterBridge ||
        original_entry == resolvedQuickResolutionTrampoline;
    LOGI("InstallStaticReplacementHook: hook=%u art=%p org_flag=0x%x org_quick=%p org_jni=%p shared_stub=%d qgj=%p qti=%p qrt=%p rqgj=%p rqti=%p rqrt=%p",
         hook_id,
         hook_info->artMethod,
         hook_info->orgFlag,
         reinterpret_cast<void*>(hook_info->orgEntryPoint),
         reinterpret_cast<void*>(hook_info->orgJNIEntry),
         uses_shared_art_stub ? 1 : 0,
         quickGenericJniTrampoline,
         quickToInterpreterBridge,
         quickResolutionTrampoline,
         resolvedQuickGenericJniTrampoline,
         resolvedQuickToInterpreterBridge,
         resolvedQuickResolutionTrampoline);

    if (!uses_shared_art_stub) {
        void* hooked_target = nullptr;
        handle->routerTrampoline = hook_install_art_router(
            reinterpret_cast<void*>(hook_info->orgEntryPoint),
            static_cast<uint32_t>(hook_info->layout.offset_entry_quick),
            0,
            jenv.get(),
            &hooked_target);
        handle->routerHookedTarget = hooked_target != nullptr
            ? hooked_target
            : reinterpret_cast<void*>(hook_info->orgEntryPoint);
        if (handle->routerTrampoline == nullptr) {
            hook_art_router_table_remove(reinterpret_cast<uint64_t>(hook_info->artMethod));
            hook_remove_redirect(reinterpret_cast<uint64_t>(hook_info->artMethod));
            delete[] replacement;
            delete handle;
            LOGE("InstallStaticReplacementHook: hook_install_art_router failed");
            return false;
        }
        handle->ownsRouterHook = true;

        handle->routerStub = hook_create_art_router_stub(
            hook_info->orgEntryPoint,
            static_cast<uint32_t>(hook_info->layout.offset_entry_quick));
        if (handle->routerStub == nullptr) {
            if (handle->routerHookedTarget != nullptr) {
                hook_remove(handle->routerHookedTarget);
            }
            hook_art_router_table_remove(reinterpret_cast<uint64_t>(hook_info->artMethod));
            hook_remove_redirect(reinterpret_cast<uint64_t>(hook_info->artMethod));
            delete[] replacement;
            delete handle;
            LOGE("InstallStaticReplacementHook: hook_create_art_router_stub failed");
            return false;
        }
    } else {
        handle->routerHookedTarget = reinterpret_cast<void*>(hook_info->orgEntryPoint);
        LOGI("InstallStaticReplacementHook: hook=%u uses shared art stub entry=%p",
             hook_id,
             reinterpret_cast<void*>(hook_info->orgEntryPoint));
    }

    hook_info->usesStaticReplacement = true;
    hook_info->hookedEntryPoint = uses_shared_art_stub
        ? hook_info->orgEntryPoint
        : reinterpret_cast<uint64_t>(handle->routerStub);
    hook_info->staticReplacementHookHandle = handle;
    hook_info->staticReplacementOriginalEntry = reinterpret_cast<void*>(hook_info->orgEntryPoint);
    LOGI("InstallStaticReplacementHook: hook=%u target=%p replacement=%p thunk=%p trampoline=%p stub=%p",
         hook_id,
         handle->routerHookedTarget,
         handle->replacementArtMethod,
         handle->nativeThunk,
         handle->routerTrampoline,
         handle->routerStub);
    return true;
}

static void UninstallStaticReplacementHook(HookInfo* hook_info) {
    if (hook_info == nullptr || !hook_info->usesStaticReplacement) {
        return;
    }

    auto* handle = reinterpret_cast<StaticReplacementHookHandle*>(hook_info->staticReplacementHookHandle);
    if (handle != nullptr) {
        if (handle->ownsRouterHook && handle->routerHookedTarget != nullptr) {
            hook_remove(handle->routerHookedTarget);
        }
        hook_art_router_table_remove(reinterpret_cast<uint64_t>(hook_info->artMethod));
        hook_remove_redirect(reinterpret_cast<uint64_t>(hook_info->artMethod));
        delete[] reinterpret_cast<uint8_t*>(handle->replacementArtMethod);
        delete handle;
    }

    hook_info->usesStaticReplacement = false;
    hook_info->staticReplacementHookHandle = nullptr;
    hook_info->staticReplacementOriginalEntry = nullptr;
}

}  // namespace

// 修改后的标志
static uint32_t getModifiedFlag(uint32_t orgFlag) {
    uint32_t removeFlags = (kAccCriticalNative | kAccFastNative |
                            kAccNterpEntryPointFastPathFlag |
                            kAccFastInterpreterToInterpreterInvoke |
                            kAccSingleImplementation);
    removeFlags |= GetPreCompiledFlagCompat();
    uint32_t addFlags = (kAccNative | GetCompileDontBotherFlagCompat());
    return ((orgFlag & ~removeFlags) | addFlags);
}

static uint32_t getTempRecoveredFlag(uint32_t orgFlag) {
    uint32_t removeFlags = (kAccNterpEntryPointFastPathFlag |
                            kAccFastInterpreterToInterpreterInvoke |
                            kAccSingleImplementation);
    removeFlags |= GetPreCompiledFlagCompat();
    uint32_t addFlags = GetCompileDontBotherFlagCompat();
    return ((orgFlag & ~removeFlags) | addFlags);
}

static void write_hooked_artmethod(void* artMethod,
                                   const HookInfo& hookInfo,
                                   uint32_t hookedFlag,
                                   uint64_t hookedJniEntry,
                                   uint64_t hookedQuickEntry) {
    *reinterpret_cast<uint32_t*>((char*)artMethod + hookInfo.layout.offset_access_flags) = hookedFlag;
    *reinterpret_cast<uint64_t*>((char*)artMethod + hookInfo.layout.offset_entry_jni) = hookedJniEntry;
    *reinterpret_cast<uint64_t*>((char*)artMethod + hookInfo.layout.offset_entry_quick) = hookedQuickEntry;
}

// 恢复 ArtMethod
static void recover_artmethod(void* ArtmethodToRecover, HookInfo& hookInfo, bool tempRecover = false) {
    if (tempRecover) {
        *reinterpret_cast<uint32_t*>((char*)ArtmethodToRecover + hookInfo.layout.offset_access_flags)
            = getTempRecoveredFlag(hookInfo.orgFlag);
    } else {
        *reinterpret_cast<uint32_t*>((char*)ArtmethodToRecover + hookInfo.layout.offset_access_flags)
            = hookInfo.orgFlag;
    }
    *reinterpret_cast<uint64_t*>((char*)ArtmethodToRecover + hookInfo.layout.offset_entry_quick)
        = hookInfo.orgEntryPoint;
    *reinterpret_cast<uint64_t*>((char*)ArtmethodToRecover + hookInfo.layout.offset_entry_jni)
        = hookInfo.orgJNIEntry;
}

static void* allocate_backup_artmethod(const HookInfo& hookInfo) {
    auto backup = new uint8_t[hookInfo.layout.art_method_size];
    if (!backup) {
        LOGE("Failed to allocate backup ArtMethod");
        return nullptr;
    }

    memcpy(backup, hookInfo.artMethod, hookInfo.layout.art_method_size);
    return backup;
}

static void sync_backup_artmethod(HookInfo& hookInfo) {
    if (!hookInfo.backupValid || !hookInfo.backupArtMethod) {
        return;
    }

    auto currentFlag = *reinterpret_cast<uint32_t*>((char*)hookInfo.artMethod + hookInfo.layout.offset_access_flags);
    auto currentQuick = *reinterpret_cast<uint64_t*>((char*)hookInfo.artMethod + hookInfo.layout.offset_entry_quick);
    auto currentJni = *reinterpret_cast<uint64_t*>((char*)hookInfo.artMethod + hookInfo.layout.offset_entry_jni);

    if (currentQuick != 0 && currentQuick != hookInfo.hookedEntryPoint) {
        hookInfo.orgEntryPoint = currentQuick;
    }
    if (currentJni != 0 && currentJni != hookInfo.hookedJNIEntry) {
        hookInfo.orgJNIEntry = currentJni;
    }
    if (currentFlag != hookInfo.hookedFlag) {
        hookInfo.orgFlag = currentFlag;
    }

    recover_artmethod(hookInfo.backupArtMethod, hookInfo, true);
}

static void* allocate_invoke_artmethod(HookInfo& hookInfo) {
    sync_backup_artmethod(hookInfo);

    void* invokeArtMethod = allocate_backup_artmethod(hookInfo);
    if (invokeArtMethod == nullptr) {
        return nullptr;
    }

    recover_artmethod(invokeArtMethod, hookInfo, true);
    return invokeArtMethod;
}

std::mutex g_original_invoke_bypass_mutex;
std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint32_t>> g_original_invoke_bypass_depths;

uint64_t GetCurrentThreadIdForBypassState() {
    return static_cast<uint64_t>(gettid());
}

bool IsOriginalInvokeBypassActive(uint32_t hook_id) {
    const uint64_t thread_id = GetCurrentThreadIdForBypassState();
    std::lock_guard<std::mutex> lock(g_original_invoke_bypass_mutex);
    const auto thread_it = g_original_invoke_bypass_depths.find(thread_id);
    if (thread_it == g_original_invoke_bypass_depths.end()) {
        return false;
    }

    const auto hook_it = thread_it->second.find(hook_id);
    return hook_it != thread_it->second.end() && hook_it->second > 0u;
}

class ScopedOriginalInvokeBypass {
public:
    explicit ScopedOriginalInvokeBypass(uint32_t hook_id)
        : hook_id_(hook_id), thread_id_(GetCurrentThreadIdForBypassState()), active_(true) {
        std::lock_guard<std::mutex> lock(g_original_invoke_bypass_mutex);
        ++g_original_invoke_bypass_depths[thread_id_][hook_id_];
    }

    ~ScopedOriginalInvokeBypass() {
        if (!active_) {
            return;
        }
        std::lock_guard<std::mutex> lock(g_original_invoke_bypass_mutex);
        auto thread_it = g_original_invoke_bypass_depths.find(thread_id_);
        if (thread_it == g_original_invoke_bypass_depths.end()) {
            return;
        }

        auto hook_it = thread_it->second.find(hook_id_);
        if (hook_it == thread_it->second.end()) {
            return;
        }

        if (hook_it->second <= 1u) {
            thread_it->second.erase(hook_it);
        } else {
            --hook_it->second;
        }

        if (thread_it->second.empty()) {
            g_original_invoke_bypass_depths.erase(thread_it);
        }
    }

    ScopedOriginalInvokeBypass(const ScopedOriginalInvokeBypass&) = delete;
    ScopedOriginalInvokeBypass& operator=(const ScopedOriginalInvokeBypass&) = delete;

private:
    uint32_t hook_id_;
    uint64_t thread_id_;
    bool active_;
};

static uint32_t read_compressed_reference(uint64_t stackRef) {
    if (stackRef == 0) {
        return 0;
    }

    const uintptr_t aligned = static_cast<uintptr_t>(stackRef & (~static_cast<uint64_t>(1)));
    uint32_t compressed_ref = 0;
    if (!TryReadU32(aligned, &compressed_ref)) {
        return 0;
    }
    return compressed_ref;
}

static void* compressed_reference_to_object(uint32_t compressedRef) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(compressedRef));
}

static jobject create_local_ref_from_stack_ref(JNIEnv* env, uint64_t stackRef) {
    auto compressedRef = read_compressed_reference(stackRef);
    if (compressedRef == 0 || !ArtInternals::newlocalrefFn) {
        return nullptr;
    }
    return reinterpret_cast<jobject>(
        ArtInternals::newlocalrefFn(env, compressed_reference_to_object(compressedRef)));
}

static bool encode_jobject_to_invoke_ref(void* thread, jobject object, uint32_t* outRef) {
    if (!outRef) {
        return false;
    }
    if (object == nullptr) {
        *outRef = 0;
        return true;
    }
    if (!ArtInternals::DecodeJObject || !thread) {
        LOGE("DecodeJObject is unavailable");
        return false;
    }

    void* decodedObject = ArtInternals::DecodeJObject(thread, object);
    *outRef = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(decodedObject));
    return true;
}

static void cleanup_owned_local_refs(JNIEnv* env, jobject* ownedRefs, size_t count) {
    if (!env || !ownedRefs) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        if (ownedRefs[i] != nullptr) {
            env->DeleteLocalRef(ownedRefs[i]);
            ownedRefs[i] = nullptr;
        }
    }
}

static bool decode_native_hook_args(JNIEnv* env,
                                    const HookInfo& hookInfo,
                                    const HookContext* ctx,
                                    HookValue** out_args,
                                    jobject** out_owned_refs,
                                    size_t* out_param_count) {
    if (env == nullptr || ctx == nullptr || out_args == nullptr ||
        out_owned_refs == nullptr || out_param_count == nullptr ||
        hookInfo.shorty.empty()) {
        return false;
    }

    const size_t paramCount = hookInfo.shorty.size() - 1u;
    auto* args = new HookValue[paramCount];
    auto* ownedLocalRefs = new jobject[paramCount];
    memset(args, 0, sizeof(HookValue) * paramCount);
    memset(ownedLocalRefs, 0, sizeof(jobject) * paramCount);

    int x_reg_count = 2;
    int v_reg_count = 0;
    int stack_reg_count = 0;
    const uintptr_t stack_base = static_cast<uintptr_t>(ctx->sp);

    for (size_t i = 0; i < paramCount; ++i) {
        const char type = hookInfo.shorty[i + 1u];
        switch (type) {
            case 'F': {
                float value = 0.0f;
                if (v_reg_count < 8) {
                    memcpy(&value, &ctx->d[v_reg_count], sizeof(float));
                } else {
                    memcpy(&value,
                           reinterpret_cast<const void*>(stack_base + stack_reg_count * sizeof(uint64_t)),
                           sizeof(float));
                    stack_reg_count++;
                }
                args[i].f = value;
                v_reg_count++;
                break;
            }
            case 'D': {
                double value = 0.0;
                if (v_reg_count < 8) {
                    memcpy(&value, &ctx->d[v_reg_count], sizeof(double));
                } else {
                    memcpy(&value,
                           reinterpret_cast<const void*>(stack_base + stack_reg_count * sizeof(uint64_t)),
                           sizeof(double));
                    stack_reg_count++;
                }
                args[i].d = value;
                v_reg_count++;
                break;
            }
            case 'L': {
                uint64_t obj_ptr = 0;
                if (x_reg_count <= 7) {
                    obj_ptr = ctx->x[x_reg_count];
                } else {
                    memcpy(&obj_ptr,
                           reinterpret_cast<const void*>(stack_base + stack_reg_count * sizeof(uint64_t)),
                           sizeof(uint64_t));
                    stack_reg_count++;
                }
                ownedLocalRefs[i] = create_local_ref_from_stack_ref(env, obj_ptr);
                args[i].l = ownedLocalRefs[i];
                x_reg_count++;
                break;
            }
            case 'J': {
                int64_t value = 0;
                if (x_reg_count <= 7) {
                    value = static_cast<int64_t>(ctx->x[x_reg_count]);
                } else {
                    memcpy(&value,
                           reinterpret_cast<const void*>(stack_base + stack_reg_count * sizeof(uint64_t)),
                           sizeof(int64_t));
                    stack_reg_count++;
                }
                args[i].j = value;
                x_reg_count++;
                break;
            }
            default: {
                uint64_t value = 0;
                if (x_reg_count <= 7) {
                    value = ctx->x[x_reg_count];
                } else {
                    memcpy(&value,
                           reinterpret_cast<const void*>(stack_base + stack_reg_count * sizeof(uint64_t)),
                           sizeof(uint64_t));
                    stack_reg_count++;
                }
                args[i].u = value;
                x_reg_count++;
                break;
            }
        }
    }

    *out_args = args;
    *out_owned_refs = ownedLocalRefs;
    *out_param_count = paramCount;
    return true;
}

static void write_native_hook_return_to_context(const HookInfo& hookInfo,
                                                const HookValue& result,
                                                HookContext* ctx) {
    if (ctx == nullptr || hookInfo.shorty.empty()) {
        return;
    }

    ctx->x[0] = 0;
    switch (hookInfo.shorty[0]) {
        case 'F': {
            memcpy(&ctx->d[0], &result.f, sizeof(float));
            uint32_t bits = 0;
            memcpy(&bits, &result.f, sizeof(float));
            ctx->x[0] = bits;
            break;
        }
        case 'D': {
            memcpy(&ctx->d[0], &result.d, sizeof(double));
            uint64_t bits = 0;
            memcpy(&bits, &result.d, sizeof(double));
            ctx->x[0] = bits;
            break;
        }
        case 'Z':
            ctx->x[0] = result.z ? 1u : 0u;
            break;
        case 'B':
            ctx->x[0] = static_cast<uint8_t>(result.b);
            break;
        case 'C':
            ctx->x[0] = static_cast<uint16_t>(result.c);
            break;
        case 'S':
            ctx->x[0] = static_cast<uint16_t>(result.s);
            break;
        case 'I':
            ctx->x[0] = static_cast<uint32_t>(result.i);
            break;
        case 'J':
            ctx->x[0] = static_cast<uint64_t>(result.j);
            break;
        case 'L':
            ctx->x[0] = reinterpret_cast<uint64_t>(result.l);
            break;
        case 'V':
            break;
        default:
            ctx->x[0] = result.u;
            break;
    }
}

static bool append_shorty_type(const char* signature, size_t length, size_t* index, std::string* outShorty) {
    if (!signature || !index || !outShorty || *index >= length) {
        return false;
    }

    char ch = signature[*index];
    switch (ch) {
        case 'V':
        case 'Z':
        case 'B':
        case 'C':
        case 'S':
        case 'I':
        case 'J':
        case 'F':
        case 'D':
            outShorty->push_back(ch);
            return true;
        case 'L':
            outShorty->push_back('L');
            while (*index < length && signature[*index] != ';') {
                (*index)++;
            }
            return *index < length && signature[*index] == ';';
        case '[':
            outShorty->push_back('L');
            while (*index < length && signature[*index] == '[') {
                (*index)++;
            }
            if (*index >= length) {
                return false;
            }
            if (signature[*index] == 'L') {
                while (*index < length && signature[*index] != ';') {
                    (*index)++;
                }
                return *index < length && signature[*index] == ';';
            }
            return append_shorty_type(signature, length, index, outShorty);
        default:
            return false;
    }
}

static bool signature_to_shorty(const char* signature, std::string* outShorty) {
    if (!signature || !outShorty) {
        return false;
    }

    outShorty->clear();
    size_t length = strlen(signature);
    if (length < 3 || signature[0] != '(') {
        return false;
    }

    size_t index = 1;
    while (index < length && signature[index] != ')') {
        if (!append_shorty_type(signature, length, &index, outShorty)) {
            return false;
        }
        index++;
    }

    if (index >= length || signature[index] != ')') {
        return false;
    }

    index++;
    if (index >= length) {
        return false;
    }

    std::string returnShorty;
    if (!append_shorty_type(signature, length, &index, &returnShorty) || returnShorty.empty()) {
        return false;
    }

    outShorty->insert(outShorty->begin(), returnShorty[0]);
    return true;
}

// 生成 Trampoline
static void clear_jni_exception(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

static bool read_jstring_utf8(JNIEnv* env, jstring value, std::string* outText) {
    if (env == nullptr || outText == nullptr) {
        return false;
    }
    if (value == nullptr) {
        outText->clear();
        return true;
    }

    const char* utf8 = env->GetStringUTFChars(value, nullptr);
    if (utf8 == nullptr) {
        clear_jni_exception(env);
        return false;
    }
    *outText = utf8;
    env->ReleaseStringUTFChars(value, utf8);
    return true;
}

static std::string primitive_type_name_to_descriptor(const std::string& typeName) {
    if (typeName == "void") return "V";
    if (typeName == "boolean") return "Z";
    if (typeName == "byte") return "B";
    if (typeName == "char") return "C";
    if (typeName == "short") return "S";
    if (typeName == "int") return "I";
    if (typeName == "long") return "J";
    if (typeName == "float") return "F";
    if (typeName == "double") return "D";
    return {};
}

static bool class_object_to_descriptor(JNIEnv* env, jobject classObject, std::string* outDescriptor) {
    if (env == nullptr || classObject == nullptr || outDescriptor == nullptr) {
        return false;
    }

    jclass classClass = env->FindClass("java/lang/Class");
    if (classClass == nullptr) {
        clear_jni_exception(env);
        return false;
    }

    jmethodID getNameMethod = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    if (getNameMethod == nullptr) {
        env->DeleteLocalRef(classClass);
        clear_jni_exception(env);
        return false;
    }

    jstring nameString = reinterpret_cast<jstring>(env->CallObjectMethod(classObject, getNameMethod));
    env->DeleteLocalRef(classClass);
    if (nameString == nullptr || env->ExceptionCheck()) {
        clear_jni_exception(env);
        return false;
    }

    std::string typeName;
    const bool ok = read_jstring_utf8(env, nameString, &typeName);
    env->DeleteLocalRef(nameString);
    if (!ok) {
        return false;
    }

    const std::string primitiveDescriptor = primitive_type_name_to_descriptor(typeName);
    if (!primitiveDescriptor.empty()) {
        *outDescriptor = primitiveDescriptor;
        return true;
    }

    if (!typeName.empty() && typeName[0] == '[') {
        std::replace(typeName.begin(), typeName.end(), '.', '/');
        *outDescriptor = typeName;
        return true;
    }

    std::replace(typeName.begin(), typeName.end(), '.', '/');
    *outDescriptor = "L" + typeName + ";";
    return true;
}

static ResolvedJavaMethod find_method_by_reflection(JNIEnv* env,
                                                    jclass clazz,
                                                    const char* methodName,
                                                    bool isStatic) {
    if (env == nullptr || clazz == nullptr || methodName == nullptr) {
        return {};
    }

    jclass classClass = env->FindClass("java/lang/Class");
    jclass methodClass = env->FindClass("java/lang/reflect/Method");
    jclass modifierClass = env->FindClass("java/lang/reflect/Modifier");
    if (classClass == nullptr || methodClass == nullptr || modifierClass == nullptr) {
        if (classClass != nullptr) env->DeleteLocalRef(classClass);
        if (methodClass != nullptr) env->DeleteLocalRef(methodClass);
        if (modifierClass != nullptr) env->DeleteLocalRef(modifierClass);
        clear_jni_exception(env);
        return {};
    }

    jmethodID getDeclaredMethodsMethod =
        env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jmethodID methodGetNameMethod = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
    jmethodID methodGetModifiersMethod = env->GetMethodID(methodClass, "getModifiers", "()I");
    jmethodID methodGetParameterTypesMethod =
        env->GetMethodID(methodClass, "getParameterTypes", "()[Ljava/lang/Class;");
    jmethodID methodGetReturnTypeMethod =
        env->GetMethodID(methodClass, "getReturnType", "()Ljava/lang/Class;");
    jmethodID modifierIsStaticMethod =
        env->GetStaticMethodID(modifierClass, "isStatic", "(I)Z");
    if (getDeclaredMethodsMethod == nullptr ||
        methodGetNameMethod == nullptr ||
        methodGetModifiersMethod == nullptr ||
        methodGetParameterTypesMethod == nullptr ||
        methodGetReturnTypeMethod == nullptr ||
        modifierIsStaticMethod == nullptr) {
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(methodClass);
        env->DeleteLocalRef(modifierClass);
        clear_jni_exception(env);
        return {};
    }

    jobjectArray methods =
        reinterpret_cast<jobjectArray>(env->CallObjectMethod(clazz, getDeclaredMethodsMethod));
    if (methods == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(methodClass);
        env->DeleteLocalRef(modifierClass);
        clear_jni_exception(env);
        return {};
    }

    ResolvedJavaMethod matched = {};
    int matchedCount = 0;
    const jsize methodCount = env->GetArrayLength(methods);
    for (jsize methodIndex = 0; methodIndex < methodCount; ++methodIndex) {
        jobject methodObject = env->GetObjectArrayElement(methods, methodIndex);
        if (methodObject == nullptr) {
            clear_jni_exception(env);
            continue;
        }

        jstring reflectedNameString =
            reinterpret_cast<jstring>(env->CallObjectMethod(methodObject, methodGetNameMethod));
        std::string reflectedName;
        if (reflectedNameString == nullptr || !read_jstring_utf8(env, reflectedNameString, &reflectedName)) {
            if (reflectedNameString != nullptr) {
                env->DeleteLocalRef(reflectedNameString);
            }
            env->DeleteLocalRef(methodObject);
            continue;
        }
        env->DeleteLocalRef(reflectedNameString);

        if (reflectedName != methodName) {
            env->DeleteLocalRef(methodObject);
            continue;
        }

        jint modifiers = env->CallIntMethod(methodObject, methodGetModifiersMethod);
        if (env->ExceptionCheck()) {
            clear_jni_exception(env);
            env->DeleteLocalRef(methodObject);
            continue;
        }
        const bool reflectedIsStatic =
            env->CallStaticBooleanMethod(modifierClass, modifierIsStaticMethod, modifiers) == JNI_TRUE;
        if (env->ExceptionCheck()) {
            clear_jni_exception(env);
            env->DeleteLocalRef(methodObject);
            continue;
        }
        if (reflectedIsStatic != isStatic) {
            env->DeleteLocalRef(methodObject);
            continue;
        }

        jobjectArray parameterTypes = reinterpret_cast<jobjectArray>(
            env->CallObjectMethod(methodObject, methodGetParameterTypesMethod));
        jobject returnType = env->CallObjectMethod(methodObject, methodGetReturnTypeMethod);
        if (parameterTypes == nullptr || returnType == nullptr || env->ExceptionCheck()) {
            if (parameterTypes != nullptr) env->DeleteLocalRef(parameterTypes);
            if (returnType != nullptr) env->DeleteLocalRef(returnType);
            clear_jni_exception(env);
            env->DeleteLocalRef(methodObject);
            continue;
        }

        std::string signature = "(";
        bool signatureOk = true;
        const jsize parameterCount = env->GetArrayLength(parameterTypes);
        for (jsize parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex) {
            jobject parameterType = env->GetObjectArrayElement(parameterTypes, parameterIndex);
            std::string descriptor;
            if (parameterType == nullptr || !class_object_to_descriptor(env, parameterType, &descriptor)) {
                signatureOk = false;
            } else {
                signature += descriptor;
            }
            if (parameterType != nullptr) {
                env->DeleteLocalRef(parameterType);
            }
            if (!signatureOk) {
                break;
            }
        }

        std::string returnDescriptor;
        if (!signatureOk || !class_object_to_descriptor(env, returnType, &returnDescriptor)) {
            signatureOk = false;
        } else {
            signature += ")";
            signature += returnDescriptor;
        }

        env->DeleteLocalRef(parameterTypes);
        env->DeleteLocalRef(returnType);
        if (!signatureOk) {
            env->DeleteLocalRef(methodObject);
            continue;
        }

        std::string detectedShorty;
        if (!signature_to_shorty(signature.c_str(), &detectedShorty)) {
            env->DeleteLocalRef(methodObject);
            continue;
        }

        jmethodID methodId = env->FromReflectedMethod(methodObject);
        env->DeleteLocalRef(methodObject);
        if (methodId == nullptr || env->ExceptionCheck()) {
            clear_jni_exception(env);
            continue;
        }

        matched.method_id = methodId;
        matched.signature = signature;
        matched.shorty = detectedShorty;
        matchedCount++;
        if (matchedCount > 1) {
            matched = {};
            break;
        }
    }

    env->DeleteLocalRef(methods);
    env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(methodClass);
    env->DeleteLocalRef(modifierClass);

    if (matchedCount > 1) {
        LOGE("Wildcard Java method resolution is ambiguous: %s", methodName);
        return {nullptr, ""};
    }
    return matched;
}

static bool DeoptimizeArtMethodInPlace(void* artMethod) {
    if (artMethod == nullptr || ArtInternals::RuntimeInstance == 0) {
        return false;
    }

    void* classLinker = *(void**)(ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
    if (classLinker == nullptr) {
        return false;
    }

    void* quickToInterpreterBridge =
        *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickToInterpreterBridgeTrampoline);
    if (quickToInterpreterBridge == nullptr) {
        return false;
    }

    uint32_t access_flags =
        *reinterpret_cast<uint32_t*>((char*)artMethod + ArtInternals::ArtMethodLayout.offset_access_flags);
    access_flags &= ~(kAccNterpEntryPointFastPathFlag |
                      kAccFastInterpreterToInterpreterInvoke |
                      kAccSingleImplementation |
                      GetPreCompiledFlagCompat());
    access_flags |= GetCompileDontBotherFlagCompat();
    *reinterpret_cast<uint32_t*>((char*)artMethod + ArtInternals::ArtMethodLayout.offset_access_flags) =
        access_flags;
    *reinterpret_cast<uint64_t*>((char*)artMethod + ArtInternals::ArtMethodLayout.offset_entry_quick) =
        reinterpret_cast<uint64_t>(quickToInterpreterBridge);
    return true;
}

static void DeoptimizeDeclaringClassMethods(JNIEnv* env, jclass clazz, jmethodID hooked_method_id) {
    if (env == nullptr || clazz == nullptr) {
        return;
    }

    jclass classClass = env->FindClass("java/lang/Class");
    jclass methodClass = env->FindClass("java/lang/reflect/Method");
    if (classClass == nullptr || methodClass == nullptr) {
        if (classClass != nullptr) env->DeleteLocalRef(classClass);
        if (methodClass != nullptr) env->DeleteLocalRef(methodClass);
        clear_jni_exception(env);
        return;
    }

    jmethodID getDeclaredMethodsMethod =
        env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jmethodID methodGetModifiersMethod = env->GetMethodID(methodClass, "getModifiers", "()I");
    if (getDeclaredMethodsMethod == nullptr || methodGetModifiersMethod == nullptr) {
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(methodClass);
        clear_jni_exception(env);
        return;
    }

    jobjectArray methods =
        reinterpret_cast<jobjectArray>(env->CallObjectMethod(clazz, getDeclaredMethodsMethod));
    if (methods == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(methodClass);
        clear_jni_exception(env);
        return;
    }

    size_t attempted = 0;
    size_t changed = 0;
    const jsize methodCount = env->GetArrayLength(methods);
    for (jsize methodIndex = 0; methodIndex < methodCount; ++methodIndex) {
        jobject methodObject = env->GetObjectArrayElement(methods, methodIndex);
        if (methodObject == nullptr) {
            clear_jni_exception(env);
            continue;
        }

        const jint modifiers = env->CallIntMethod(methodObject, methodGetModifiersMethod);
        if (env->ExceptionCheck()) {
            clear_jni_exception(env);
            env->DeleteLocalRef(methodObject);
            continue;
        }

        if ((modifiers & 0x0100) != 0 || (modifiers & 0x0400) != 0) {
            env->DeleteLocalRef(methodObject);
            continue;
        }

        jmethodID methodId = env->FromReflectedMethod(methodObject);
        env->DeleteLocalRef(methodObject);
        if (methodId == nullptr || env->ExceptionCheck()) {
            clear_jni_exception(env);
            continue;
        }

        if (hooked_method_id != nullptr && methodId == hooked_method_id) {
            continue;
        }

        void* artMethod = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, methodId);
        if (artMethod == nullptr) {
            continue;
        }

        ++attempted;
        if (DeoptimizeArtMethodInPlace(artMethod)) {
            ++changed;
        }
    }

    LOGI("DeoptimizeDeclaringClassMethods: attempted=%zu changed=%zu hooked=%p",
         attempted,
         changed,
         hooked_method_id);
    env->DeleteLocalRef(methods);
    env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(methodClass);
}

static void DeoptimizeDeclaringClassConstructors(JNIEnv* env, jclass clazz) {
    if (env == nullptr || clazz == nullptr) {
        return;
    }

    jclass classClass = env->FindClass("java/lang/Class");
    jclass constructorClass = env->FindClass("java/lang/reflect/Constructor");
    if (classClass == nullptr || constructorClass == nullptr) {
        if (classClass != nullptr) env->DeleteLocalRef(classClass);
        if (constructorClass != nullptr) env->DeleteLocalRef(constructorClass);
        clear_jni_exception(env);
        return;
    }

    jmethodID getDeclaredConstructorsMethod =
        env->GetMethodID(classClass, "getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;");
    if (getDeclaredConstructorsMethod == nullptr) {
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(constructorClass);
        clear_jni_exception(env);
        return;
    }

    jobjectArray constructors =
        reinterpret_cast<jobjectArray>(env->CallObjectMethod(clazz, getDeclaredConstructorsMethod));
    if (constructors == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(classClass);
        env->DeleteLocalRef(constructorClass);
        clear_jni_exception(env);
        return;
    }

    size_t attempted = 0;
    size_t changed = 0;
    const jsize constructorCount = env->GetArrayLength(constructors);
    for (jsize index = 0; index < constructorCount; ++index) {
        jobject constructorObject = env->GetObjectArrayElement(constructors, index);
        if (constructorObject == nullptr) {
            clear_jni_exception(env);
            continue;
        }

        jmethodID methodId = env->FromReflectedMethod(constructorObject);
        env->DeleteLocalRef(constructorObject);
        if (methodId == nullptr || env->ExceptionCheck()) {
            clear_jni_exception(env);
            continue;
        }

        void* artMethod = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, methodId);
        if (artMethod == nullptr) {
            continue;
        }

        ++attempted;
        if (DeoptimizeArtMethodInPlace(artMethod)) {
            ++changed;
        }
    }

    LOGI("DeoptimizeDeclaringClassConstructors: attempted=%zu changed=%zu",
         attempted,
         changed);
    env->DeleteLocalRef(constructors);
    env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(constructorClass);
}

static uint8_t* GenerateTrampoline(uint64_t hook_id, void* handler_addr) {
    uint8_t* code = (uint8_t*)tool::allocate_exec_mem(TRAMPOLINE_SIZE);
    if (!code) {
        LOGE("Failed to allocate trampoline memory");
        return nullptr;
    }

    uint32_t* inst = (uint32_t*)code;
    int i = 0;

    // stp x0, x1, [sp, #-16]!  - 保存参数
    inst[i++] = 0xA9BF07E0;
    // movz x0, #hook_id
    inst[i++] = 0xD2800000 | ((hook_id & 0xFFFF) << 5);
    // ldr x1, #8
    inst[i++] = 0x58000041;
    // br x1
    inst[i++] = 0xD61F0020;
    // handler_addr 字面量
    void** addr_ptr = (void**)&inst[i++];
    *addr_ptr = handler_addr;

    return code;
}

// Trampoline 入口 (汇编)
extern "C" void hook_trampoline_ex();
__attribute__((naked))
void hook_trampoline_ex() {
    asm volatile(
        "mov x15, x0\n"         // 保存 hook_id
        "ldp x0, x1, [sp], #16\n" // 恢复 x0(env), x1(thiz)
        "b hook_handler\n"      // 跳转到 C++ handler
    );
}

// Hook Handler
extern "C" uint64_t hook_handler(JNIEnv* env, jobject thiz,
                                 uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5,
                                 uint64_t x6, uint64_t x7) {
    // 获取 hook_id
    uint64_t tmpHookid;
    asm volatile("mov %0, x15" : "=r"(tmpHookid));
    uint32_t hookID = (uint32_t)tmpHookid;

    // 获取浮点寄存器
    double v0, v1, v2, v3, v4, v5, v6, v7;
    asm volatile(
        "mov %0, v0.d[0]\n"
        "mov %1, v1.d[0]\n"
        "mov %2, v2.d[0]\n"
        "mov %3, v3.d[0]\n"
        "mov %4, v4.d[0]\n"
        "mov %5, v5.d[0]\n"
        "mov %6, v6.d[0]\n"
        "mov %7, v7.d[0]\n"
        : "=r"(v0), "=r"(v1), "=r"(v2), "=r"(v3),
          "=r"(v4), "=r"(v5), "=r"(v6), "=r"(v7)
        :
        :);

    // 获取栈参数
    uint64_t origin_sp;
    asm volatile("mov %0, x29" : "=r"(origin_sp));
    void* args_in = (void*)(origin_sp + 0x20);

    const bool bypass_callback = IsOriginalInvokeBypassActive(hookID);
    std::unique_lock<std::mutex> hookLock;
    if (!bypass_callback) {
        LOGI("hook_handler: waiting for hook mutex hook=%u", hookID);
        hookLock = std::unique_lock<std::mutex>(HookIdLockManager::Instance().GetMutex(hookID));
        LOGI("hook_handler: acquired hook mutex hook=%u", hookID);
    }

    // 获取 Hook 信息 - 使用值拷贝避免 use-after-free
    LOGI("hook_handler: CopyByIndex begin hook=%u", hookID);
    HookInfo hookInfo = HookStore<HookInfo>::Instance().CopyByIndex(hookID);
    LOGI("hook_handler: CopyByIndex end hook=%u valid=%d", hookID, hookInfo.valid ? 1 : 0);

    if (!hookInfo.valid) {
        LOGE("Hook %d is invalid", hookID);
        return 0;
    }

    // 解析参数
    size_t paramCount = hookInfo.shorty.size() - 1; // shorty[0] 是返回值
    HookValue* args = new HookValue[paramCount];
    jobject* ownedLocalRefs = new jobject[paramCount];
    memset(ownedLocalRefs, 0, sizeof(jobject) * paramCount);

    int x_reg_count = 2;  // x0(env), x1(thiz) 已用
    int v_reg_count = 0;
    int stack_reg_count = 0;

    for (size_t i = 0; i < paramCount; i++) {
        char type = hookInfo.shorty[i + 1];

        switch (type) {
            case 'F':  // float
                if (v_reg_count < 8) {
                    double* vregs[] = {&v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7};
                    args[i].f = *(float*)vregs[v_reg_count];
                } else {
                    args[i].f = *(float*)((uint64_t)args_in + stack_reg_count * 8);
                    stack_reg_count++;
                }
                v_reg_count++;
                break;

            case 'D':  // double
                if (v_reg_count < 8) {
                    double* vregs[] = {&v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7};
                    args[i].d = *vregs[v_reg_count];
                } else {
                    args[i].d = *(double*)((uint64_t)args_in + stack_reg_count * 8);
                    stack_reg_count++;
                }
                v_reg_count++;
                break;

            case 'L':  // 对象引用
                {
                    uint64_t obj_ptr;
                    if (x_reg_count <= 7) {
                        uint64_t* xregs[] = {&x2, &x3, &x4, &x5, &x6, &x7};
                        obj_ptr = *xregs[x_reg_count - 2];
                    } else {
                        obj_ptr = *(uint64_t*)((uint64_t)args_in + stack_reg_count * 8);
                        stack_reg_count++;
                    }
                    ownedLocalRefs[i] = create_local_ref_from_stack_ref(env, obj_ptr);
                    args[i].l = ownedLocalRefs[i];
                    x_reg_count++;
                }
                break;

            default:  // 其他整数和指针类型
                if (x_reg_count <= 7) {
                    uint64_t* xregs[] = {&x2, &x3, &x4, &x5, &x6, &x7};
                    args[i].u = *xregs[x_reg_count - 2];
                } else {
                    args[i].u = *(uint64_t*)((uint64_t)args_in + stack_reg_count * 8);
                    stack_reg_count++;
                }
                x_reg_count++;
        }
    }

    // 调用用户回调
    jobject callbackThis = nullptr;
    if (!hookInfo.isStatic && thiz != nullptr) {
        callbackThis = env->NewLocalRef(thiz);
    }

    HookValue directRet = {0};
    bool callOriginal = true;
    if (!bypass_callback) {
        LOGI("hook_handler: invoking callback hook=%u class=%s method=%s sig=%s shorty=%s this=%p argc=%zu",
             hookID,
             hookInfo.className.c_str(),
             hookInfo.methodName.c_str(),
             hookInfo.signature.c_str(),
             hookInfo.shorty.c_str(),
             callbackThis,
             paramCount);
        callOriginal = hookInfo.callback(env, callbackThis, args, paramCount, &directRet);
        LOGI("hook_handler: callback returned hook=%u class=%s method=%s callOriginal=%d",
             hookID,
             hookInfo.className.c_str(),
             hookInfo.methodName.c_str(),
             callOriginal ? 1 : 0);
    } else {
        LOGI("hook_handler: bypass callback for hook=%u class=%s method=%s during callOriginal",
             hookID,
             hookInfo.className.c_str(),
             hookInfo.methodName.c_str());
    }

    if (!callOriginal) {
        jobject objectRet = nullptr;
        if (hookInfo.shorty[0] == 'L' && directRet.l != nullptr) {
            objectRet = env->NewLocalRef(reinterpret_cast<jobject>(directRet.l));
        }

        if (callbackThis != nullptr) {
            env->DeleteLocalRef(callbackThis);
        }
        cleanup_owned_local_refs(env, ownedLocalRefs, paramCount);
        delete[] ownedLocalRefs;
        delete[] args;
        // 不调用原函数，直接返回
        switch (hookInfo.shorty[0]) {
            case 'F': {
                asm volatile("fmov s0, %s0" : : "w"(directRet.f));
                return 0;
            }
            case 'D': {
                asm volatile("fmov d0, %d0" : : "w"(directRet.d));
                return 0;
            }
            case 'L':
                return reinterpret_cast<uint64_t>(objectRet);
            default:
                return directRet.u;
        }
    }

    // 调用原函数
    void* thread = ArtInternals::GetCurrentThread();
    if (!thread) {
        LOGE("Failed to get current thread");
        if (callbackThis != nullptr) {
            env->DeleteLocalRef(callbackThis);
        }
        cleanup_owned_local_refs(env, ownedLocalRefs, paramCount);
        delete[] ownedLocalRefs;
        delete[] args;
        return 0;
    }

    // ScopedGCCriticalSection is a stack-style object, not a returned handle.
    if (!ArtInternals::SGCFn || !ArtInternals::DestroyGCFn) {
        LOGE("ScopedGCCriticalSection helpers are unavailable");
        if (callbackThis != nullptr) {
            env->DeleteLocalRef(callbackThis);
        }
        cleanup_owned_local_refs(env, ownedLocalRefs, paramCount);
        delete[] ownedLocalRefs;
        delete[] args;
        return 0;
    }

    char gcScope[256] = {};
    ArtInternals::SGCFn(gcScope, thread, kGcCauseDebugger, kCollectorTypeDebugger);

    // 构造参数数组
    auto argsArray = new uint32_t[(paramCount + 2) * 8];
    memset(argsArray, 0, sizeof(uint32_t) * (paramCount + 2) * 8);
    uint32_t argsize = 0;

    if (!hookInfo.isStatic) {
        // 非静态方法，第一个参数是 this
        uint32_t compressed_this = 0;
        if (!encode_jobject_to_invoke_ref(thread, callbackThis, &compressed_this)) {
            if (callbackThis != nullptr) {
                env->DeleteLocalRef(callbackThis);
            }
            cleanup_owned_local_refs(env, ownedLocalRefs, paramCount);
            delete[] ownedLocalRefs;
            delete[] args;
            delete[] argsArray;
            ArtInternals::DestroyGCFn(gcScope);
            return 0;
        }
        argsArray[0] = compressed_this;
        argsize += 4;
    }

    // 填充参数
    for (size_t i = 0; i < paramCount; i++) {
        char type = hookInfo.shorty[i + 1];
        switch (type) {
            case 'F':
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].f, sizeof(float));
                argsize += 4;
                break;
            case 'D':
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].d, sizeof(double));
                argsize += 8;
                break;
            case 'J':  // long
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].j, sizeof(int64_t));
                argsize += 8;
                break;
            case 'L':  // 对象引用
                {
                    uint32_t compressed_ref = 0;
                    if (!encode_jobject_to_invoke_ref(
                            thread,
                            reinterpret_cast<jobject>(args[i].l),
                            &compressed_ref)) {
                        if (callbackThis != nullptr) {
                            env->DeleteLocalRef(callbackThis);
                        }
                        cleanup_owned_local_refs(env, ownedLocalRefs, paramCount);
                        delete[] ownedLocalRefs;
                        delete[] args;
                        delete[] argsArray;
                        ArtInternals::DestroyGCFn(gcScope);
                        return 0;
                    }
                    memcpy((void*)((uint64_t)argsArray + argsize), &compressed_ref, sizeof(uint32_t));
                    argsize += 4;
                }
                break;
            default:  // 其他 4 字节类型
                memcpy((void*)((uint64_t)argsArray + argsize), &args[i].i, sizeof(int32_t));
                argsize += 4;
                break;
        }
    }

    if (callbackThis != nullptr) {
        env->DeleteLocalRef(callbackThis);
    }
    cleanup_owned_local_refs(env, ownedLocalRefs, paramCount);
    delete[] ownedLocalRefs;
    delete[] args;

    jvalue result;
    void* invokeArtMethod = nullptr;
    {
        std::mutex& storeMtx = HookStore<HookInfo>::Instance().GetMutex();
        LOGI("hook_handler: waiting for store mutex hook=%u", hookID);
        std::lock_guard<std::mutex> storeLock(storeMtx);
        LOGI("hook_handler: acquired store mutex hook=%u", hookID);
        HookInfo& liveHookInfo = HookStore<HookInfo>::Instance().GetUnsafe(hookID);
        if (!liveHookInfo.valid || !liveHookInfo.backupValid || !liveHookInfo.backupArtMethod) {
            LOGE("Hook %d backup ArtMethod is invalid", hookID);
            delete[] argsArray;
            ArtInternals::DestroyGCFn(gcScope);
            return 0;
        }
        invokeArtMethod = allocate_invoke_artmethod(liveHookInfo);
        if (invokeArtMethod == nullptr) {
            delete[] argsArray;
            ArtInternals::DestroyGCFn(gcScope);
            return 0;
        }
    }
    {
        ScopedOriginalInvokeBypass bypass_scope(hookID);
        LOGI("hook_handler: calling original ArtMethod::Invoke hook=%u", hookID);
        ArtInternals::Invoke(invokeArtMethod, thread, argsArray, argsize, &result, hookInfo.shorty.c_str());
        LOGI("hook_handler: original ArtMethod::Invoke returned hook=%u", hookID);
    }

    delete[] reinterpret_cast<uint8_t*>(invokeArtMethod);
    delete[] argsArray;
    ArtInternals::DestroyGCFn(gcScope);

    // 返回结果
    uint64_t ret = 0;
    switch (hookInfo.shorty[0]) {
        case 'F':
            asm volatile("fmov s0, %s0" : : "w"(result.f));
            return 0;
        case 'D':
            asm volatile("fmov d0, %d0" : : "w"(result.d));
            return 0;
        case 'Z': ret = result.z; break;
        case 'B': ret = result.b; break;
        case 'C': ret = result.c; break;
        case 'S': ret = result.s; break;
        case 'I': ret = result.i; break;
        case 'J': ret = result.j; break;
        case 'L':
            ret = result.l != nullptr ? (uint64_t)ArtInternals::newlocalrefFn(env, result.l) : 0;
            break;
        case 'V':
            return 0;
    }
    return ret;
}

namespace {
static void StaticNativeHookCallback(HookContext* ctx, void* user_data) {
    if (ctx == nullptr) {
        return;
    }
    const uint32_t hook_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user_data));
    auto* env = reinterpret_cast<JNIEnv*>(ctx->x[0]);
    auto thiz = reinterpret_cast<jobject>(ctx->x[1]);
    if (env == nullptr) {
        LOGE("StaticNativeHookCallback: env is null hook=%u", hook_id);
        return;
    }

    const bool bypass_callback = IsOriginalInvokeBypassActive(hook_id);
    std::unique_lock<std::mutex> hookLock;
    if (!bypass_callback) {
        hookLock = std::unique_lock<std::mutex>(HookIdLockManager::Instance().GetMutex(hook_id));
    }

    HookInfo hookInfo = HookStore<HookInfo>::Instance().CopyByIndex(hook_id);
    if (!hookInfo.valid) {
        LOGE("StaticNativeHookCallback: invalid hook=%u", hook_id);
        ctx->x[0] = 0;
        return;
    }

    HookValue* args = nullptr;
    jobject* ownedLocalRefs = nullptr;
    size_t paramCount = 0;
    if (!decode_native_hook_args(env, hookInfo, ctx, &args, &ownedLocalRefs, &paramCount)) {
        LOGE("StaticNativeHookCallback: decode args failed hook=%u", hook_id);
        ctx->x[0] = 0;
        return;
    }

    jobject callbackThis = nullptr;
    if (!hookInfo.isStatic && thiz != nullptr) {
        callbackThis = env->NewLocalRef(thiz);
    }

    HookValue result = {0};
    bool callOriginal = true;
    if (!bypass_callback) {
        callOriginal = hookInfo.callback(env, callbackThis, args, paramCount, &result);
    }

    if (callOriginal) {
        HookValue originalResult = {0};
        if (JavaHook::InvokeOriginalMethod(static_cast<int>(hook_id),
                                           env,
                                           thiz,
                                           args,
                                           paramCount,
                                           &originalResult)) {
            result = originalResult;
        } else {
            memset(&result, 0, sizeof(result));
        }
    } else if (hookInfo.shorty[0] == 'L' && result.l != nullptr) {
        result.l = env->NewLocalRef(reinterpret_cast<jobject>(result.l));
    }

    if (callbackThis != nullptr) {
        env->DeleteLocalRef(callbackThis);
    }
    cleanup_owned_local_refs(env, ownedLocalRefs, paramCount);
    delete[] ownedLocalRefs;
    delete[] args;

    write_native_hook_return_to_context(hookInfo, result, ctx);
    LOGI("StaticNativeHookCallback: hook=%u callOriginal=%d ret_x0=%p",
         hook_id,
         callOriginal ? 1 : 0,
         reinterpret_cast<void*>(ctx->x[0]));
}
}

// JavaHook 公共 API 实现
bool JavaHook::Init() {
    if (g_java_hook_init_once_state.load(std::memory_order_acquire) == 2) {
        return true;
    }

    std::lock_guard<std::mutex> init_lock(g_java_hook_init_mutex);
    if (g_java_hook_init_once_state.load(std::memory_order_relaxed) == 2) {
        return true;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        LOGE("✗ Failed to get JNIEnv");
        g_java_hook_init_once_state.store(0, std::memory_order_release);
        return false;
    }

    if (!ArtInternals::Init()) {
        LOGE("✗ Failed to initialize ArtInternals");
        g_java_hook_init_once_state.store(0, std::memory_order_release);
        return false;
    }

    g_java_hook_init_once_state.store(2, std::memory_order_release);
    return true;
}

namespace {

bool IsBootstrapClassName(const char* slash_name) {
    if (slash_name == nullptr) {
        return false;
    }

    const char* prefixes[] = {
        "android/",
        "java/",
        "javax/",
        "dalvik/",
        "sun/",
        "com/android/",
        "org/apache/"
    };

    for (const char* prefix : prefixes) {
        if (std::strncmp(slash_name, prefix, std::strlen(prefix)) == 0) {
            return true;
        }
    }
    return false;
}

}

jclass JavaHook::FindClass(JNIEnv* env, const char* className) {
    return FindClassWithLoader(env, nullptr, className);
}

jclass JavaHook::FindClassWithLoader(JNIEnv* env, jobject loader, const char* className) {
    if (env == nullptr || className == nullptr) {
        return nullptr;
    }

    const std::string slashName = JavaHookLoaderResolver::NormalizeSlashClassName(className);
    const bool bootstrapClass = IsBootstrapClassName(slashName.c_str());
    if (bootstrapClass) {
        jclass clazz = env->FindClass(slashName.c_str());
        if (clazz != nullptr) {
            return clazz;
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    jobject classLoader = loader;
    if (classLoader == nullptr) {
        classLoader = JavaHookLoaderResolver::GetApplicationClassLoader(env);
    } else {
        classLoader = env->NewLocalRef(classLoader);
    }
    if (classLoader == nullptr) {
        return nullptr;
    }

    jclass loadedClass = JavaHookLoaderResolver::FindLoadedClassWithLoader(env, classLoader, className);
    if (loadedClass != nullptr) {
        env->DeleteLocalRef(classLoader);
        LOGI("FindClass via findLoadedClass success: %s", className);
        return loadedClass;
    }

    loadedClass = JavaHookLoaderResolver::LoadClassWithLoader(env, classLoader, className);
    env->DeleteLocalRef(classLoader);
    if (loadedClass != nullptr) {
        LOGI("FindClass via application class loader success: %s", className);
        return loadedClass;
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    return nullptr;
}


ResolvedJavaMethod JavaHook::FindMethod(JNIEnv* env, jclass clazz,
                                        const char* methodName,
                                        const char* shorty,
                                        bool isStatic) {
    std::string methodSignature = shorty ? shorty : "";
    ResolvedJavaMethod resolved = {};
    std::string detectedShorty;

    if (methodSignature == "*") {
        return find_method_by_reflection(env, clazz, methodName, isStatic);
    }

    if (!signature_to_shorty(methodSignature.c_str(), &detectedShorty)) {
        LOGE("Invalid method signature for shorty conversion: %s", methodSignature.c_str());
        return {};
    }

    jmethodID methodID = isStatic ?
        env->GetStaticMethodID(clazz, methodName, methodSignature.c_str()) :
        env->GetMethodID(clazz, methodName, methodSignature.c_str());

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return {};
    }

    resolved.method_id = methodID;
    resolved.signature = methodSignature;
    resolved.shorty = detectedShorty;
    return resolved;
}

bool JavaHook::InvokeOriginalMethod(int hookId,
                                    JNIEnv* env,
                                    jobject thiz,
                                    HookValue* args,
                                    size_t arg_count,
                                    HookValue* result_out) {
    LOGI("InvokeOriginalMethod: enter hook=%d arg_count=%zu", hookId, arg_count);
    if (hookId < 0 || env == nullptr || result_out == nullptr) {
        return false;
    }

    std::mutex& storeMtx = HookStore<HookInfo>::Instance().GetMutex();
    HookInfo hookInfo;
    {
        LOGI("InvokeOriginalMethod: waiting for store mutex(copy) hook=%d", hookId);
        std::lock_guard<std::mutex> storeLock(storeMtx);
        LOGI("InvokeOriginalMethod: acquired store mutex(copy) hook=%d", hookId);
        if (static_cast<size_t>(hookId) >= HookStore<HookInfo>::Instance().SizeUnsafe()) {
            return false;
        }
        hookInfo = HookStore<HookInfo>::Instance().GetUnsafe(hookId);
    }

    if (!hookInfo.valid || !hookInfo.backupValid || !hookInfo.backupArtMethod) {
        LOGE("InvokeOriginalMethod: hook %d backup ArtMethod is invalid", hookId);
        return false;
    }
    LOGI("InvokeOriginalMethod: hook=%d class=%s method=%s shorty=%s static=%d art=%p backup=%p backupValid=%d",
         hookId,
         hookInfo.className.c_str(),
         hookInfo.methodName.c_str(),
         hookInfo.shorty.c_str(),
         hookInfo.isStatic ? 1 : 0,
         hookInfo.artMethod,
         hookInfo.backupArtMethod,
         hookInfo.backupValid ? 1 : 0);
    if (hookInfo.shorty.size() != arg_count + 1u) {
        LOGE("InvokeOriginalMethod: arg count mismatch, hookId=%d expected=%zu actual=%zu",
             hookId,
             hookInfo.shorty.size() > 0 ? hookInfo.shorty.size() - 1u : 0u,
             arg_count);
        return false;
    }

    void* thread = ArtInternals::GetCurrentThread();
    if (!thread) {
        LOGE("InvokeOriginalMethod: failed to get current thread");
        return false;
    }
    if (!ArtInternals::SGCFn || !ArtInternals::DestroyGCFn) {
        LOGE("InvokeOriginalMethod: ScopedGCCriticalSection helpers are unavailable");
        return false;
    }

    jobject callbackThis = nullptr;
    if (!hookInfo.isStatic && thiz != nullptr) {
        callbackThis = env->NewLocalRef(thiz);
    }

    char gcScope[256] = {};
    ArtInternals::SGCFn(gcScope, thread, kGcCauseDebugger, kCollectorTypeDebugger);

    auto argsArray = new uint32_t[(arg_count + 2u) * 8u];
    memset(argsArray, 0, sizeof(uint32_t) * (arg_count + 2u) * 8u);
    uint32_t argsize = 0u;

    if (!hookInfo.isStatic) {
        uint32_t compressed_this = 0;
        if (!encode_jobject_to_invoke_ref(thread, callbackThis, &compressed_this)) {
            if (callbackThis != nullptr) {
                env->DeleteLocalRef(callbackThis);
            }
            delete[] argsArray;
            ArtInternals::DestroyGCFn(gcScope);
            return false;
        }
        argsArray[0] = compressed_this;
        argsize += 4u;
    }

    for (size_t i = 0; i < arg_count; ++i) {
        const char type = hookInfo.shorty[i + 1u];
        switch (type) {
            case 'F':
                memcpy(reinterpret_cast<void*>(reinterpret_cast<uint64_t>(argsArray) + argsize),
                       &args[i].f,
                       sizeof(float));
                argsize += 4u;
                break;
            case 'D':
                memcpy(reinterpret_cast<void*>(reinterpret_cast<uint64_t>(argsArray) + argsize),
                       &args[i].d,
                       sizeof(double));
                argsize += 8u;
                break;
            case 'J':
                memcpy(reinterpret_cast<void*>(reinterpret_cast<uint64_t>(argsArray) + argsize),
                       &args[i].j,
                       sizeof(int64_t));
                argsize += 8u;
                break;
            case 'L': {
                uint32_t compressed_ref = 0;
                if (!encode_jobject_to_invoke_ref(thread,
                                                  reinterpret_cast<jobject>(args[i].l),
                                                  &compressed_ref)) {
                    if (callbackThis != nullptr) {
                        env->DeleteLocalRef(callbackThis);
                    }
                    delete[] argsArray;
                    ArtInternals::DestroyGCFn(gcScope);
                    return false;
                }
                memcpy(reinterpret_cast<void*>(reinterpret_cast<uint64_t>(argsArray) + argsize),
                       &compressed_ref,
                       sizeof(uint32_t));
                argsize += 4u;
                break;
            }
            default:
                memcpy(reinterpret_cast<void*>(reinterpret_cast<uint64_t>(argsArray) + argsize),
                       &args[i].i,
                       sizeof(int32_t));
                argsize += 4u;
                break;
        }
    }

    if (callbackThis != nullptr) {
        env->DeleteLocalRef(callbackThis);
    }

    jvalue result = {};
    void* invokeArtMethod = nullptr;
    {
        LOGI("InvokeOriginalMethod: waiting for store mutex(invoke clone) hook=%d", hookId);
        std::lock_guard<std::mutex> storeLock(storeMtx);
        LOGI("InvokeOriginalMethod: acquired store mutex(invoke clone) hook=%d", hookId);
        HookInfo& liveHookInfo = HookStore<HookInfo>::Instance().GetUnsafe(hookId);
        if (!liveHookInfo.valid || !liveHookInfo.backupValid || !liveHookInfo.backupArtMethod) {
            LOGE("InvokeOriginalMethod: live hook invalid during invoke clone hook=%d valid=%d backupValid=%d backup=%p",
                 hookId,
                 liveHookInfo.valid ? 1 : 0,
                 liveHookInfo.backupValid ? 1 : 0,
                 liveHookInfo.backupArtMethod);
            delete[] argsArray;
            ArtInternals::DestroyGCFn(gcScope);
            return false;
        }
        invokeArtMethod = allocate_invoke_artmethod(liveHookInfo);
        if (invokeArtMethod == nullptr) {
            delete[] argsArray;
            ArtInternals::DestroyGCFn(gcScope);
            return false;
        }
    }

    {
        ScopedOriginalInvokeBypass bypass_scope(static_cast<uint32_t>(hookId));
        LOGI("InvokeOriginalMethod: calling ArtInternals::Invoke hook=%d invokeArt=%p shorty=%s bypassActive=%d",
             hookId,
             invokeArtMethod,
             hookInfo.shorty.c_str(),
             IsOriginalInvokeBypassActive(static_cast<uint32_t>(hookId)) ? 1 : 0);
        ArtInternals::Invoke(invokeArtMethod, thread, argsArray, argsize, &result, hookInfo.shorty.c_str());
        LOGI("InvokeOriginalMethod: ArtInternals::Invoke returned hook=%d", hookId);
    }

    delete[] reinterpret_cast<uint8_t*>(invokeArtMethod);
    delete[] argsArray;
    ArtInternals::DestroyGCFn(gcScope);

    memset(result_out, 0, sizeof(HookValue));
    switch (hookInfo.shorty[0]) {
        case 'Z': result_out->z = result.z; break;
        case 'B': result_out->b = result.b; break;
        case 'C': result_out->c = result.c; break;
        case 'S': result_out->s = result.s; break;
        case 'I': result_out->i = result.i; break;
        case 'J': result_out->j = result.j; break;
        case 'F': result_out->f = result.f; break;
        case 'D': result_out->d = result.d; break;
        case 'L':
            result_out->l = result.l != nullptr && ArtInternals::newlocalrefFn != nullptr
                ? reinterpret_cast<void*>(ArtInternals::newlocalrefFn(env, result.l))
                : nullptr;
            break;
        case 'V':
            break;
        default:
            result_out->u = result.j;
            break;
    }
    LOGI("InvokeOriginalMethod: exit hook=%d", hookId);
    return true;
}

int JavaHook::HookMethod(const char* className, const char* methodName,
                           const char* shorty, bool isStatic, HookCallback callback) {
    return HookMethodWithLoader(className, nullptr, methodName, shorty, isStatic, callback);
}

int JavaHook::HookMethodWithLoader(const char* className,
                                   jobject loader,
                                   const char* methodName,
                                   const char* shorty,
                                   bool isStatic,
                                   HookCallback callback) {
    std::lock_guard<std::mutex> install_lock(g_java_hook_install_mutex);

    JavaEnv jenv;
    if (jenv.isNull()) {
        LOGE("JNIEnv is null");
        return -1;
    }

    JNIEnv* env = jenv.get();

    // 查找类
    jclass clazz = FindClassWithLoader(env, loader, className);
    if (!clazz) {
        LOGE("FindClass failed: %s", className);
        return -1;
    }

    // 查找方法
    const ResolvedJavaMethod resolved_method = FindMethod(env, clazz, methodName, shorty, isStatic);
    if (!resolved_method.method_id) {
        LOGE("GetMethodID failed: %s", methodName);
        return -1;
    }
    LOGI("HookMethodWithLoader: class=%s method=%s requested_sig=%s detected_shorty=%s static=%d loader=%p methodID=%p",
         className,
         methodName,
         shorty ? shorty : "*",
         resolved_method.shorty.c_str(),
         isStatic ? 1 : 0,
         loader,
         resolved_method.method_id);

    // 获取 ArtMethod
    void* artMethod = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, resolved_method.method_id);
    if (!artMethod) {
        LOGE("Failed to decode method ID");
        return -1;
    }

    LOGI("ArtMethod: %p", artMethod);

    // 备份原始数据
    uint64_t* quickCode = (uint64_t*)((char*)artMethod + ArtInternals::ArtMethodLayout.offset_entry_quick);
    uint32_t orgFlag = *(uint32_t*)((char*)artMethod + ArtInternals::ArtMethodLayout.offset_access_flags);
    uint64_t* jni = (uint64_t*)((char*)artMethod + ArtInternals::ArtMethodLayout.offset_entry_jni);
    LOGI("HookMethodWithLoader: resolved class=%s method=%s art=%p flags=0x%x quick=%p jni=%p private=%d static=%d final=%d native=%d",
         className,
         methodName,
         artMethod,
         orgFlag,
         reinterpret_cast<void*>(*quickCode),
         reinterpret_cast<void*>(*jni),
         (orgFlag & kAccPrivate) != 0 ? 1 : 0,
         (orgFlag & kAccStatic) != 0 ? 1 : 0,
         (orgFlag & kAccFinal) != 0 ? 1 : 0,
         (orgFlag & kAccNative) != 0 ? 1 : 0);

    // 分配 Hook ID
    uint32_t hookID = (uint32_t)HookStore<HookInfo>::Instance().Size();

    // 生成 Trampoline
    void* trampoline = GenerateTrampoline(hookID, (void*)&hook_trampoline_ex);
    if (!trampoline) {
        LOGE("Failed to generate trampoline");
        return -1;
    }

    // 创建 Hook 信息
    HookInfo info = {
        className,
        methodName,
        resolved_method.signature,
        resolved_method.shorty,
        isStatic,
        artMethod,
        nullptr,
        trampoline,
        *quickCode,
        *jni,
        orgFlag,
        0,
        0,
        0,
        false,
        false,
        nullptr,
        nullptr,
        ArtInternals::ArtMethodLayout,
        resolved_method.method_id,
        callback,
        true
    };
    bool forced_interpret_changed = false;
    const bool forced_interpret_ok = TrySetForcedInterpretOnly(&forced_interpret_changed);
    bool jit_invalidated = false;
    const bool jit_invalidate_ok = TryInvalidateJitCodeCache(&jit_invalidated);
    LOGI("HookMethod: forced_interpret_ok=%d changed=%d jit_invalidate_ok=%d invalidated=%d",
         forced_interpret_ok ? 1 : 0,
         forced_interpret_changed ? 1 : 0,
         jit_invalidate_ok ? 1 : 0,
         jit_invalidated ? 1 : 0);

    // 暂停所有线程
    char SSA[128] = {};
    if (ArtInternals::ScopedSuspendAllFn) {
        ArtInternals::ScopedSuspendAllFn(SSA, "Install Hook", false);
    }

    // 获取 ClassLinker 实例和 quickGenericJniTrampoline
    void* classLinker = *(void**)(ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
    void* quickGenericJniTrampoline =
        *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickGenericJniTrampoline);
    void* quickToInterpreterBridge =
        *(void**)((char*)classLinker + ArtInternals::ClassLinkerSpec.quickToInterpreterBridgeTrampoline);

    // Native hook：优先走 quickGenericJniTrampoline，保证进入 entry_jni 的 trampoline
    void* quickEntry = quickGenericJniTrampoline ? quickGenericJniTrampoline : quickToInterpreterBridge;
    if (!quickEntry) {
        if (ArtInternals::destroyScopedSuspendAllFn) {
            ArtInternals::destroyScopedSuspendAllFn(SSA);
        }
        tool::free_exec_mem(trampoline, TRAMPOLINE_SIZE);
        LOGE("Failed to resolve quick entry trampoline");
        return -1;
    }
    info.hookedFlag = getModifiedFlag(orgFlag);
    info.hookedJNIEntry = (uint64_t)trampoline;
    info.hookedEntryPoint = (uint64_t)quickEntry;

    info.backupArtMethod = allocate_backup_artmethod(info);
    if (!info.backupArtMethod) {
        if (ArtInternals::destroyScopedSuspendAllFn) {
            ArtInternals::destroyScopedSuspendAllFn(SSA);
        }
        if (trampoline) {
            tool::free_exec_mem(trampoline, TRAMPOLINE_SIZE);
        }
        return -1;
    }
    info.backupValid = true;
    recover_artmethod(info.backupArtMethod, info, true);

    if (!InstallStaticReplacementHook(&info, hookID)) {
        if (ArtInternals::destroyScopedSuspendAllFn) {
            ArtInternals::destroyScopedSuspendAllFn(SSA);
        }
        delete[] reinterpret_cast<uint8_t*>(info.backupArtMethod);
        if (trampoline) {
            tool::free_exec_mem(trampoline, TRAMPOLINE_SIZE);
        }
        LOGE("InstallStaticReplacementHook failed: %s.%s", className, methodName);
        return -1;
    }

    HookStore<HookInfo>::Instance().Add(info, hookID);
    write_hooked_artmethod(info.artMethod,
                           info,
                           info.hookedFlag,
                           info.hookedJNIEntry,
                           info.hookedEntryPoint);

    LOGI("Hook installed: entry_jni -> trampoline (%p), entry_quick -> %s (%p)",
         trampoline,
         quickGenericJniTrampoline ? "quickGenericJniTrampoline" : "quickToInterpreterBridge",
         quickEntry);
    LOGI("Hook installed detail: hook=%u class=%s method=%s requested_sig=%s shorty=%s static=%d art=%p backup=%p jni=%p quick=%p",
         hookID,
         className,
         methodName,
         shorty ? shorty : "*",
         info.shorty.c_str(),
         isStatic ? 1 : 0,
         info.artMethod,
         info.backupArtMethod,
         reinterpret_cast<void*>(info.hookedJNIEntry),
         reinterpret_cast<void*>(info.hookedEntryPoint));
    hook_art_router_table_dump();

    if (ArtInternals::destroyScopedSuspendAllFn) {
        ArtInternals::destroyScopedSuspendAllFn(SSA);
    }

    DeoptimizeDeclaringClassMethods(env, clazz, resolved_method.method_id);
    DeoptimizeDeclaringClassConstructors(env, clazz);

    LOGI("Hooked successfully: %s.%s (ID: %d)", className, methodName, hookID);
    return (int)hookID;
}

bool JavaHook::GetHookSignature(int hookId, std::string* signature) {
    if (signature == nullptr || hookId < 0) {
        return false;
    }

    std::lock_guard<std::mutex> storeLock(HookStore<HookInfo>::Instance().GetMutex());
    if (static_cast<size_t>(hookId) >= HookStore<HookInfo>::Instance().SizeUnsafe()) {
        return false;
    }

    const HookInfo& info = HookStore<HookInfo>::Instance().GetUnsafe(hookId);
    if (!info.valid || info.signature.empty()) {
        return false;
    }

    *signature = info.signature;
    return true;
}

bool JavaHook::Unhook(int hookId) {
    if (hookId < 0) return false;

    LOGI("Unhook begin: ID %d", hookId);
    std::mutex& mtx = HookIdLockManager::Instance().GetMutex(hookId);
    LOGI("Unhook waiting for hook mutex: ID %d", hookId);
    std::lock_guard<std::mutex> lock(mtx);
    LOGI("Unhook acquired hook mutex: ID %d", hookId);

    std::mutex& storeMtx = HookStore<HookInfo>::Instance().GetMutex();
    LOGI("Unhook waiting for store mutex: ID %d", hookId);
    HookInfo info;
    bool has_remaining_hooks = false;
    {
        std::lock_guard<std::mutex> storeLock(storeMtx);
        LOGI("Unhook acquired store mutex: ID %d", hookId);

        HookInfo& liveInfo = HookStore<HookInfo>::Instance().GetUnsafe(hookId);
        if (!liveInfo.valid) {
            LOGI("Unhook skip invalid hook: ID %d", hookId);
            return false;
        }

        info = liveInfo;
        liveInfo.valid = false;
        LOGI("Unhook marked invalid: ID %d", hookId);
        has_remaining_hooks = HasAnyValidHookUnsafe();
        LOGI("Unhook remaining hooks snapshot: ID %d remaining=%d",
             hookId,
             has_remaining_hooks ? 1 : 0);
    }

    char SSA[128] = {};
    if (ArtInternals::ScopedSuspendAllFn) {
        LOGI("Unhook entering ScopedSuspendAll: ID %d", hookId);
        ArtInternals::ScopedSuspendAllFn(SSA, "Unhook", false);
        LOGI("Unhook entered ScopedSuspendAll: ID %d", hookId);
    }

    LOGI("Unhook recover_artmethod begin: ID %d", hookId);
    recover_artmethod(info.artMethod, info);
    LOGI("Unhook recover_artmethod end: ID %d", hookId);

    if (info.trampoline) {
        LOGI("Unhook free trampoline: ID %d", hookId);
        tool::free_exec_mem(info.trampoline, TRAMPOLINE_SIZE);
        info.trampoline = nullptr;
    }

    LOGI("Unhook static replacement uninstall begin: ID %d", hookId);
    UninstallStaticReplacementHook(&info);
    LOGI("Unhook static replacement uninstall end: ID %d", hookId);

    if (info.backupArtMethod) {
        LOGI("Unhook free backupArtMethod: ID %d", hookId);
        delete[] reinterpret_cast<uint8_t*>(info.backupArtMethod);
        info.backupArtMethod = nullptr;
    }
    info.backupValid = false;

    if (ArtInternals::destroyScopedSuspendAllFn) {
        LOGI("Unhook leaving ScopedSuspendAll: ID %d", hookId);
        ArtInternals::destroyScopedSuspendAllFn(SSA);
        LOGI("Unhook left ScopedSuspendAll: ID %d", hookId);
    }

    LOGI("Unhooked: ID %d", hookId);
    if (!has_remaining_hooks) {
        LOGI("Unhook ReleaseForcedInterpretOnly begin: ID %d", hookId);
        ReleaseForcedInterpretOnly();
        LOGI("Unhook ReleaseForcedInterpretOnly end: ID %d", hookId);
    }
    return true;
}

void JavaHook::UnhookAll() {
    JavaEnv jenv;
    for (size_t i = 0; i < HookStore<HookInfo>::Instance().Size(); i++) {
        Unhook((int)i);
    }
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_forced_interpret_only_mutex);
            if (g_forced_interpret_only_state.ref_count == 0) {
                break;
            }
        }
        ReleaseForcedInterpretOnly();
    }
}

bool JavaHook::DeoptimizeJit(bool* invalidated) {
    return TryInvalidateJitCodeCache(invalidated);
}

bool JavaHook::GetLastDeoptDiagnostics(DeoptDiagnostics* out) {
    if (out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_deopt_diagnostics_mutex);
    *out = g_last_deopt_diagnostics;
    return true;
}

bool JavaHook::SetForcedInterpretOnly(bool enable, bool* changed) {
    if (enable) {
        return TrySetForcedInterpretOnly(changed);
    }

    if (changed != nullptr) {
        *changed = false;
    }

    bool had_active_state = false;
    {
        std::lock_guard<std::mutex> lock(g_forced_interpret_only_mutex);
        had_active_state = g_forced_interpret_only_state.ref_count != 0;
    }
    if (!had_active_state) {
        return true;
    }

    ReleaseForcedInterpretOnly();
    if (changed != nullptr) {
        *changed = true;
    }
    return true;
}

void JavaHook::GetArtRouterDebug(uint64_t* last_x0, uint64_t* miss_count) {
    hook_art_router_get_debug(last_x0, miss_count);
}
