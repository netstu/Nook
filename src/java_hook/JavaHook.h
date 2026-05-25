#ifndef JAVAHOOK_H
#define JAVAHOOK_H

#include <jni.h>
#include <functional>
#include <string>
#include "../common/JavaHookLog.h"
#include "../framework/HookStore.h"
#include "JVM.h"

static constexpr uint32_t kAccPublic = 0x0001;
static constexpr uint32_t kAccPrivate = 0x0002;
static constexpr uint32_t kAccProtected = 0x0004;
static constexpr uint32_t kAccStatic = 0x0008;
static constexpr uint32_t kAccFinal = 0x0010;
static constexpr uint32_t kAccNative = 0x0100;
static constexpr uint32_t kAccFastNative = 0x00080000;
static constexpr uint32_t kAccCriticalNative = 0x00200000;
static constexpr uint32_t kAccCompileDontBother = 0x02000000;
static constexpr uint32_t kAccNterpEntryPointFastPathFlag = 0x00100000;
static constexpr uint32_t kAccSingleImplementation = 0x08000000;
static constexpr uint32_t kAccFastInterpreterToInterpreterInvoke = 0x40000000;

enum GcCause {
    kGcCauseDebugger,
};

enum CollectorType {
    kCollectorTypeDebugger,
};

typedef struct {
    intptr_t heap;
    intptr_t threadList;
    intptr_t internTable;
    intptr_t classLinker;
    intptr_t jniIdManager;
} ArtRuntimeSpecOffsets;

typedef struct {
    intptr_t quickResolutionTrampoline;
    intptr_t quickImtConflictTrampoline;
    intptr_t quickGenericJniTrampoline;
    intptr_t quickToInterpreterBridgeTrampoline;
} ClassLinkerSpecOffsets;

struct ArtMethodSpec {
    size_t offset_access_flags;
    size_t offset_entry_jni;
    size_t offset_entry_quick;
    size_t art_method_size;
    size_t interpreterCode;
};

union HookValue {
    uint64_t u;
    int64_t i;
    double d;
    float f;
    bool z;
    uint8_t b;
    uint16_t c;
    uint16_t s;
    int64_t j;
    void* l;
};

using HookCallback = std::function<bool(JNIEnv*, jobject, HookValue*, size_t, HookValue*)>;

struct HookInfo {
    std::string className;
    std::string methodName;
    std::string signature;
    std::string shorty;
    bool isStatic;
    void* artMethod;
    void* backupArtMethod;
    void* trampoline;
    uint64_t orgEntryPoint;
    uint64_t orgJNIEntry;
    uint32_t orgFlag;
    uint64_t hookedEntryPoint;
    uint64_t hookedJNIEntry;
    uint32_t hookedFlag;
    bool backupValid;
    bool usesStaticReplacement;
    void* staticReplacementHookHandle;
    void* staticReplacementOriginalEntry;
    ArtMethodSpec layout;
    jmethodID methodID;
    HookCallback callback;
    bool valid;
};

struct ResolvedJavaMethod {
    jmethodID method_id = nullptr;
    std::string signature;
    std::string shorty;
};

struct DeoptDiagnostics {
    bool symbolsAvailable;
    bool runtimeAvailable;
    bool invalidated;
    uint64_t runtimeAddress;
    uint64_t codeCacheAddress;
    uint64_t runtimeOffset;
    size_t scanStart;
    size_t scanEnd;
    size_t candidatesSeen;
    size_t readableCandidates;
    std::string reason;
};

class JavaHook {
public:
    static bool Init();

    static int HookMethod(const char* className,
                          const char* methodName,
                          const char* shorty,
                          bool isStatic,
                          HookCallback callback);
    static int HookMethodWithLoader(const char* className,
                                    jobject loader,
                                    const char* methodName,
                                    const char* shorty,
                                    bool isStatic,
                                    HookCallback callback);

    static bool InvokeOriginalMethod(int hookId,
                                     JNIEnv* env,
                                     jobject thiz,
                                     HookValue* args,
                                     size_t arg_count,
                                     HookValue* result);

    static bool Unhook(int hookId);
    static void UnhookAll();
    static bool DeoptimizeJit(bool* invalidated);
    static bool GetLastDeoptDiagnostics(DeoptDiagnostics* out);
    static bool SetForcedInterpretOnly(bool enable, bool* changed);
    static void GetArtRouterDebug(uint64_t* last_x0, uint64_t* miss_count);

    static jclass FindClass(JNIEnv* env, const char* className);
    static jclass FindClassWithLoader(JNIEnv* env, jobject loader, const char* className);

    static ResolvedJavaMethod FindMethod(JNIEnv* env,
                                         jclass clazz,
                                         const char* methodName,
                                         const char* shorty,
                                         bool isStatic);
    static bool GetHookSignature(int hookId, std::string* signature);

private:
    static bool InitArtInternals();
    static void* DecodeMethodID(jmethodID methodID);
    static void* GetCurrentThread();
    static void* ScopedGCCritical(void* thread);
    static void DestroyGCCritical(void* self);
    static void InvokeMethod(void* artMethod, void* thread, uint32_t* args,
                             uint32_t argsize, jvalue* result, const char* shorty);
};

namespace ArtInternals {
    using DecodeMethodIdFn = void*(*)(void*, jmethodID);
    extern DecodeMethodIdFn DecodeFunc;

    using ArtMethodInvoke = void(*)(void*, void*, uint32_t*, uint32_t, jvalue*, const char*);
    extern ArtMethodInvoke Invoke;

    using CurrentFromGDB = void*(*)();
    extern CurrentFromGDB GetCurrentThread;

    using DecodeJObjectFn = void*(*)(void*, jobject);
    extern DecodeJObjectFn DecodeJObject;

    using ScopedGCSection = int64_t(*)(void*, void*, GcCause, CollectorType);
    extern ScopedGCSection SGCFn;

    using destroyScopedGCSection = void(*)(void*);
    extern destroyScopedGCSection DestroyGCFn;

    using ScopedSuspendAll = int64_t(*)(void*, const char*, bool);
    extern ScopedSuspendAll ScopedSuspendAllFn;

    using destroyScopedSuspendAll = void(*)(void*);
    extern destroyScopedSuspendAll destroyScopedSuspendAllFn;

    using newlocalref = int64_t(*)(void*, void*);
    extern newlocalref newlocalrefFn;

    extern uintptr_t RuntimeInstance;
    extern void* jniIDManager;
    extern ArtMethodSpec ArtMethodLayout;
    extern ArtRuntimeSpecOffsets RunTimeSpec;
    extern ClassLinkerSpecOffsets ClassLinkerSpec;

    bool Init();
}

namespace tool {
    void* allocate_exec_mem(size_t size);
    bool free_exec_mem(void* addr, size_t size);
    const char* find_path_from_maps(const char* soname);
    void* get_address_from_module(const char* module_path, const char* symbol_name, bool isFunction = true);
    bool is_in_module(void* ptr, const char* module_name);
}

#endif // JAVAHOOK_H
