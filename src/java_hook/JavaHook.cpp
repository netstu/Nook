#include "JavaHook.h"
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unordered_map>
#include <fstream>
#include "../common/ArtStructDetector.h"
#include <sys/system_properties.h>

#define TRAMPOLINE_SIZE 0x10

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

// 修改后的标志
static uint32_t getModifiedFlag(uint32_t orgFlag) {
    uint32_t removeFlags = (kAccCriticalNative | kAccFastNative | kAccNterpEntryPointFastPathFlag);
    uint32_t addFlags = (kAccNative | kAccCompileDontBother);
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
            = hookInfo.orgFlag | kAccCompileDontBother;
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

static uint32_t read_compressed_reference(uint64_t stackRef) {
    if (stackRef == 0) {
        return 0;
    }

    auto aligned = stackRef & (~static_cast<uint64_t>(1));
    return *reinterpret_cast<uint32_t*>(aligned);
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

    std::mutex& mtx = HookIdLockManager::Instance().GetMutex(hookID);
    std::lock_guard<std::mutex> lock(mtx);

    // 获取 Hook 信息 - 使用值拷贝避免 use-after-free
    HookInfo hookInfo = HookStore<HookInfo>::Instance().CopyByIndex(hookID);

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
        callbackThis = create_local_ref_from_stack_ref(env, reinterpret_cast<uint64_t>(thiz));
    }

    HookValue directRet = {0};
    bool callOriginal = hookInfo.callback(env, callbackThis, args, paramCount, &directRet);

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

    // GC 保护
    int64_t gcScope = ArtInternals::SGCFn(nullptr, thread, kGcCauseDebugger, kCollectorTypeDebugger);

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
            ArtInternals::DestroyGCFn((void*)gcScope);
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
                        ArtInternals::DestroyGCFn((void*)gcScope);
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
        std::lock_guard<std::mutex> storeLock(storeMtx);
        HookInfo& liveHookInfo = HookStore<HookInfo>::Instance().GetUnsafe(hookID);
        if (!liveHookInfo.valid || !liveHookInfo.backupValid || !liveHookInfo.backupArtMethod) {
            LOGE("Hook %d backup ArtMethod is invalid", hookID);
            delete[] argsArray;
            ArtInternals::DestroyGCFn((void*)gcScope);
            return 0;
        }
        sync_backup_artmethod(liveHookInfo);
        invokeArtMethod = liveHookInfo.backupArtMethod;
    }
    ArtInternals::Invoke(invokeArtMethod, thread, argsArray, argsize, &result, hookInfo.shorty.c_str());

    delete[] argsArray;
    ArtInternals::DestroyGCFn((void*)gcScope);

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

// JavaHook 公共 API 实现
bool JavaHook::Init() {
    LOGI("=== JavaHook::Init() START ===");

    JavaEnv jenv;
    if (jenv.isNull()) {
        LOGE("✗ Failed to get JNIEnv");
        return false;
    }
    LOGI("✓ JNIEnv obtained: %p", jenv.get());

    if (!ArtInternals::Init()) {
        LOGE("✗ Failed to initialize ArtInternals");
        return false;
    }
    LOGI("✓ ArtInternals initialized");

    LOGI("✓✓✓ JavaHook initialized successfully ✓✓✓");
    return true;
}

jclass JavaHook::FindClass(JNIEnv* env, const char* className) {
    // 1) Try normal FindClass
    std::string slashName;
    for (const char* p = className; *p; ++p) {
        slashName += (*p == '.') ? '/' : *p;
    }

    jclass clazz = env->FindClass(slashName.c_str());
    if (clazz) return clazz;
    if (env->ExceptionCheck()) env->ExceptionClear();

    // 2) Injection context: use Application ClassLoader
    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if (!activityThreadClass) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jmethodID currentAppMethod = env->GetStaticMethodID(
        activityThreadClass, "currentApplication", "()Landroid/app/Application;");
    if (!currentAppMethod) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jobject application = env->CallStaticObjectMethod(activityThreadClass, currentAppMethod);
    if (!application || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jclass applicationClass = env->GetObjectClass(application);
    jmethodID getClassLoaderMethod = env->GetMethodID(
        applicationClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(applicationClass);
    if (!getClassLoaderMethod) {
        env->DeleteLocalRef(application);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jobject classLoader = env->CallObjectMethod(application, getClassLoaderMethod);
    env->DeleteLocalRef(application);
    if (!classLoader || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    if (!classLoaderClass) {
        env->DeleteLocalRef(classLoader);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jmethodID loadClassMethod = env->GetMethodID(
        classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClassMethod) {
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(classLoaderClass);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    std::string dotName;
    for (const char* p = className; *p; ++p) {
        dotName += (*p == '/') ? '.' : *p;
    }

    jstring classNameStr = env->NewStringUTF(dotName.c_str());
    jclass loadedClass = (jclass)env->CallObjectMethod(classLoader, loadClassMethod, classNameStr);
    env->DeleteLocalRef(classNameStr);
    env->DeleteLocalRef(classLoader);
    env->DeleteLocalRef(classLoaderClass);

    if (loadedClass && !env->ExceptionCheck()) {
        LOGI("FindClass via ActivityThread success: %s", className);
        return loadedClass;
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    return nullptr;
}


std::pair<jmethodID, std::string> JavaHook::FindMethod(JNIEnv* env, jclass clazz,
                                                          const char* methodName,
                                                          const char* shorty,
                                                          bool isStatic) {
    std::string methodSignature = shorty ? shorty : "";
    std::string detectedShorty;

    if (!signature_to_shorty(methodSignature.c_str(), &detectedShorty)) {
        LOGE("Invalid method signature for shorty conversion: %s", methodSignature.c_str());
        return {nullptr, ""};
    }

    jmethodID methodID = isStatic ?
        env->GetStaticMethodID(clazz, methodName, methodSignature.c_str()) :
        env->GetMethodID(clazz, methodName, methodSignature.c_str());

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return {nullptr, ""};
    }

    return {methodID, detectedShorty};
}

int JavaHook::HookMethod(const char* className, const char* methodName,
                          const char* shorty, bool isStatic, HookCallback callback) {
    JavaEnv jenv;
    if (jenv.isNull()) {
        LOGE("JNIEnv is null");
        return -1;
    }

    JNIEnv* env = jenv.get();

    // 查找类
    jclass clazz = FindClass(env, className);
    if (!clazz) {
        LOGE("FindClass failed: %s", className);
        return -1;
    }

    // 查找方法
    auto [methodID, detectedShorty] = FindMethod(env, clazz, methodName, shorty, isStatic);
    if (!methodID) {
        LOGE("GetMethodID failed: %s", methodName);
        return -1;
    }

    // 获取 ArtMethod
    void* artMethod = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, methodID);
    if (!artMethod) {
        LOGE("Failed to decode method ID");
        return -1;
    }

    LOGI("ArtMethod: %p", artMethod);

    // 备份原始数据
    uint64_t* quickCode = (uint64_t*)((char*)artMethod + ArtInternals::ArtMethodLayout.offset_entry_quick);
    uint32_t orgFlag = *(uint32_t*)((char*)artMethod + ArtInternals::ArtMethodLayout.offset_access_flags);
    uint64_t* jni = (uint64_t*)((char*)artMethod + ArtInternals::ArtMethodLayout.offset_entry_jni);

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
        detectedShorty,
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
        ArtInternals::ArtMethodLayout,
        methodID,
        callback,
        true
    };

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

    if (ArtInternals::destroyScopedSuspendAllFn) {
        ArtInternals::destroyScopedSuspendAllFn(SSA);
    }

    LOGI("Hooked successfully: %s.%s (ID: %d)", className, methodName, hookID);
    return (int)hookID;
}

bool JavaHook::Unhook(int hookId) {
    if (hookId < 0) return false;

    std::mutex& mtx = HookIdLockManager::Instance().GetMutex(hookId);
    std::lock_guard<std::mutex> lock(mtx);

    std::mutex& storeMtx = HookStore<HookInfo>::Instance().GetMutex();
    std::lock_guard<std::mutex> storeLock(storeMtx);

    HookInfo& info = HookStore<HookInfo>::Instance().GetUnsafe(hookId);
    if (!info.valid) {
        return false;
    }

    info.valid = false;

    char SSA[128] = {};
    if (ArtInternals::ScopedSuspendAllFn) {
        ArtInternals::ScopedSuspendAllFn(SSA, "Unhook", false);
    }

    recover_artmethod(info.artMethod, info);

    if (info.trampoline) {
        tool::free_exec_mem(info.trampoline, TRAMPOLINE_SIZE);
        info.trampoline = nullptr;
    }

    if (info.backupArtMethod) {
        delete[] reinterpret_cast<uint8_t*>(info.backupArtMethod);
        info.backupArtMethod = nullptr;
    }
    info.backupValid = false;

    if (ArtInternals::destroyScopedSuspendAllFn) {
        ArtInternals::destroyScopedSuspendAllFn(SSA);
    }

    LOGI("Unhooked: ID %d", hookId);
    return true;
}

void JavaHook::UnhookAll() {
    JavaEnv jenv;
    for (size_t i = 0; i < HookStore<HookInfo>::Instance().Size(); i++) {
        Unhook((int)i);
    }
}
