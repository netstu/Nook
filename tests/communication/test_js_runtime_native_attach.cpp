#include <algorithm>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef DispatchMessage
#undef DispatchMessage
#endif
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "agent_runtime/js_runtime.h"
#include "agent_runtime/js_runtime_test_api.h"
#include "agent_runtime/nook_java_js_bridge.h"
#include "agent_runtime/nook_native_js_bridge.h"
#include "agent_runtime/script_registry.h"

using namespace nook::agent_runtime;

namespace NookNativeHookInternal {
bool ResolveSymbolAddress(const char* module_name, const char* symbol_name, void** symbol_address) {
    (void)module_name;
    (void)symbol_name;
    if (symbol_address != nullptr) {
        *symbol_address = nullptr;
    }
    return false;
}
}  // namespace NookNativeHookInternal

namespace {

extern "C" uint32_t NookNativeFunctionSmokeAdd(uint32_t left, uint32_t right);
extern "C" bool NookNativeFunctionSmokeBoolNot(bool value);
extern "C" uint64_t NookNativeFunctionSmokeAddU64(uint64_t left, uint64_t right);
extern "C" double NookNativeFunctionSmokeAddDouble(double left, double right);

uint64_t& GetLastInlineHookOriginalX0();
uint64_t& GetFakeJavaEnvPointerForTesting();

struct InlineHookInvokerCapture {
    void* replacement = nullptr;
};

InlineHookInvokerCapture& GetInlineHookInvokerCapture() {
    static InlineHookInvokerCapture capture;
    return capture;
}

struct JavaHookInstallCallCapture {
    int call_count = 0;
    JavaJsHookRequest request = {};
};

JavaHookInstallCallCapture& GetJavaHookInstallCallCapture() {
    static JavaHookInstallCallCapture capture;
    return capture;
}

struct JavaHookCallOriginalCapture {
    int call_count = 0;
    JavaJsHookRecord record = {};
    std::vector<JavaJsValue> args;
};

JavaHookCallOriginalCapture& GetJavaHookCallOriginalCapture() {
    static JavaHookCallOriginalCapture capture;
    return capture;
}

struct JavaMethodInvokeCapture {
    int call_count = 0;
    JavaJsMethodRecord record = {};
    uint64_t receiver_handle = 0u;
    std::vector<JavaJsValue> args;
};

JavaMethodInvokeCapture& GetJavaMethodInvokeCapture() {
    static JavaMethodInvokeCapture capture;
    return capture;
}

uint64_t& GetFakeJavaEnvPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

JsRuntimeJavaEnvQueryStatus& GetFakeJavaEnvQueryStatusForTesting() {
    static JsRuntimeJavaEnvQueryStatus value = JsRuntimeJavaEnvQueryStatus::kAvailable;
    return value;
}

bool& GetFakeJavaEnvRequiresAttachForTesting() {
    static bool value = false;
    return value;
}

size_t& GetFakeJavaEnvQueryCallCountForTesting() {
    static size_t value = 0u;
    return value;
}

bool& GetFakeJavaEnvUseSequenceForTesting() {
    static bool value = false;
    return value;
}

uint64_t& GetFakeJavaEnvSecondPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

bool& GetFakeJavaEnvExceptionCheckResultForTesting() {
    static bool value = false;
    return value;
}

uint64_t& GetFakeJavaEnvExceptionOccurredResultForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvExceptionCheckPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvExceptionOccurredPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvExceptionClearPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetFakeJavaEnvFindClassResultForTesting() {
    static uint64_t value = 0u;
    return value;
}

std::string& GetFakeJavaEnvFindClassNameForTesting() {
    static std::string value;
    return value;
}

uint64_t& GetFakeJavaEnvGetObjectClassResultForTesting() {
    static uint64_t value = 0u;
    return value;
}

bool& GetFakeJavaEnvIsSameObjectResultForTesting() {
    static bool value = false;
    return value;
}

bool& GetFakeJavaEnvIsInstanceOfResultForTesting() {
    static bool value = false;
    return value;
}

uint64_t& GetFakeJavaEnvNewStringUtfResultForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetFakeJavaEnvGetStringUtfCharsResultForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetFakeJavaEnvNewGlobalRefResultForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetFakeJavaEnvNewWeakGlobalRefResultForTesting() {
    static uint64_t value = 0u;
    return value;
}

bool& GetFakeJavaEnvGetSuperclassHasResultForTesting() {
    static bool value = false;
    return value;
}

std::string& GetFakeJavaEnvGetSuperclassResultNameForTesting() {
    static std::string value;
    return value;
}

bool& GetFakeJavaEnvIsAssignableFromResultForTesting() {
    static bool value = false;
    return value;
}

uint32_t& GetFakeJavaEnvGetObjectRefTypeResultForTesting() {
    static uint32_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvGetObjectClassPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvGetObjectClassObjectHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvIsSameObjectPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvIsSameObjectLeftHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvIsSameObjectRightHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvIsInstanceOfPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvIsInstanceOfObjectHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

std::string& GetLastJavaEnvIsInstanceOfClassNameForTesting() {
    static std::string value;
    return value;
}

uint64_t& GetLastJavaEnvNewStringUtfPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

std::string& GetLastJavaEnvNewStringUtfTextForTesting() {
    static std::string value;
    return value;
}

uint64_t& GetLastJavaEnvGetStringUtfCharsPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvGetStringUtfCharsStringHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvReleaseStringUtfCharsPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvReleaseStringUtfCharsStringHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvReleaseStringUtfCharsCharsHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvNewGlobalRefPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvNewGlobalRefObjectHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvDeleteGlobalRefPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvDeleteGlobalRefHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvNewWeakGlobalRefPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvNewWeakGlobalRefObjectHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvDeleteWeakGlobalRefPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvDeleteWeakGlobalRefHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvGetObjectRefTypePointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvGetObjectRefTypeObjectHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvGetSuperclassPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

std::string& GetLastJavaEnvGetSuperclassClassNameForTesting() {
    static std::string value;
    return value;
}

uint64_t& GetLastJavaEnvGetSuperclassLoaderHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJavaEnvIsAssignableFromPointerForTesting() {
    static uint64_t value = 0u;
    return value;
}

std::string& GetLastJavaEnvIsAssignableFromTargetClassNameForTesting() {
    static std::string value;
    return value;
}

uint64_t& GetLastJavaEnvIsAssignableFromTargetLoaderHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

std::string& GetLastJavaEnvIsAssignableFromSourceClassNameForTesting() {
    static std::string value;
    return value;
}

uint64_t& GetLastJavaEnvIsAssignableFromSourceLoaderHandleForTesting() {
    static uint64_t value = 0u;
    return value;
}

JsRuntimeJavaEnvQueryStatus FakeGetJavaEnvPointerForTesting(bool allow_attach,
                                                            uint64_t* env_ptr_out,
                                                            std::string* error_out) {
    GetFakeJavaEnvQueryCallCountForTesting() += 1u;
    JsRuntimeJavaEnvQueryStatus status = GetFakeJavaEnvQueryStatusForTesting();
    if (status == JsRuntimeJavaEnvQueryStatus::kUnavailable) {
        return status;
    }
    if (status == JsRuntimeJavaEnvQueryStatus::kError) {
        if (error_out != nullptr) {
            *error_out = "fake env query failure";
        }
        return status;
    }
    if (GetFakeJavaEnvRequiresAttachForTesting() && !allow_attach) {
        return JsRuntimeJavaEnvQueryStatus::kUnavailable;
    }

    if (env_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "env pointer out is null";
        }
        return JsRuntimeJavaEnvQueryStatus::kError;
    }

    if (GetFakeJavaEnvUseSequenceForTesting() &&
        GetFakeJavaEnvQueryCallCountForTesting() > 1u &&
        GetFakeJavaEnvSecondPointerForTesting() != 0u) {
        *env_ptr_out = GetFakeJavaEnvSecondPointerForTesting();
    } else {
        *env_ptr_out = GetFakeJavaEnvPointerForTesting();
    }
    return JsRuntimeJavaEnvQueryStatus::kAvailable;
}

bool FakeJavaEnvExceptionCheckForTesting(uint64_t env_ptr,
                                         bool* has_exception_out,
                                         std::string* error_out) {
    GetLastJavaEnvExceptionCheckPointerForTesting() = env_ptr;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (has_exception_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "has_exception_out is null";
        }
        return false;
    }

    *has_exception_out = GetFakeJavaEnvExceptionCheckResultForTesting();
    return true;
}

bool FakeJavaEnvExceptionOccurredForTesting(uint64_t env_ptr,
                                            uint64_t* exception_ptr_out,
                                            std::string* error_out) {
    GetLastJavaEnvExceptionOccurredPointerForTesting() = env_ptr;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (exception_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "exception_ptr_out is null";
        }
        return false;
    }

    *exception_ptr_out = GetFakeJavaEnvExceptionOccurredResultForTesting();
    return true;
}

bool FakeJavaEnvExceptionClearForTesting(uint64_t env_ptr, std::string* error_out) {
    GetLastJavaEnvExceptionClearPointerForTesting() = env_ptr;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }

    return true;
}

bool FakeJavaEnvFindClassForTesting(uint64_t env_ptr,
                                    const char* class_name,
                                    uint64_t* class_ptr_out,
                                    std::string* error_out) {
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (class_name == nullptr) {
        if (error_out != nullptr) {
            *error_out = "class_name is null";
        }
        return false;
    }
    if (class_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "class_ptr_out is null";
        }
        return false;
    }

    GetFakeJavaEnvFindClassNameForTesting() = class_name;
    if (GetFakeJavaEnvFindClassResultForTesting() != 0u) {
        *class_ptr_out = GetFakeJavaEnvFindClassResultForTesting();
        return true;
    }

    if (std::strcmp(class_name, "com/zj/wuaipojie/Demo") == 0) {
        *class_ptr_out = 0x6010u;
        return true;
    }
    if (std::strcmp(class_name, "java/lang/Class") == 0) {
        *class_ptr_out = 0x6020u;
        return true;
    }

    *class_ptr_out = 0u;
    return true;
}

bool FakeJavaEnvGetObjectClassForTesting(uint64_t env_ptr,
                                         uint64_t object_handle,
                                         uint64_t* class_ptr_out,
                                         std::string* error_out) {
    GetLastJavaEnvGetObjectClassPointerForTesting() = env_ptr;
    GetLastJavaEnvGetObjectClassObjectHandleForTesting() = object_handle;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (object_handle == 0u) {
        if (error_out != nullptr) {
            *error_out = "object handle is null";
        }
        return false;
    }
    if (class_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "class_ptr_out is null";
        }
        return false;
    }

    *class_ptr_out = GetFakeJavaEnvGetObjectClassResultForTesting();
    return true;
}

bool FakeJavaEnvIsSameObjectForTesting(uint64_t env_ptr,
                                       uint64_t left_object_handle,
                                       uint64_t right_object_handle,
                                       bool* result_out,
                                       std::string* error_out) {
    GetLastJavaEnvIsSameObjectPointerForTesting() = env_ptr;
    GetLastJavaEnvIsSameObjectLeftHandleForTesting() = left_object_handle;
    GetLastJavaEnvIsSameObjectRightHandleForTesting() = right_object_handle;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (left_object_handle == 0u || right_object_handle == 0u) {
        if (error_out != nullptr) {
            *error_out = "object handle is null";
        }
        return false;
    }
    if (result_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "result_out is null";
        }
        return false;
    }

    *result_out = GetFakeJavaEnvIsSameObjectResultForTesting();
    return true;
}

bool FakeJavaEnvIsInstanceOfForTesting(uint64_t env_ptr,
                                       uint64_t object_handle,
                                       const char* class_name,
                                       bool* result_out,
                                       std::string* error_out) {
    GetLastJavaEnvIsInstanceOfPointerForTesting() = env_ptr;
    GetLastJavaEnvIsInstanceOfObjectHandleForTesting() = object_handle;
    GetLastJavaEnvIsInstanceOfClassNameForTesting() = class_name != nullptr ? class_name : "";
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (object_handle == 0u) {
        if (error_out != nullptr) {
            *error_out = "object handle is null";
        }
        return false;
    }
    if (class_name == nullptr || class_name[0] == '\0') {
        if (error_out != nullptr) {
            *error_out = "class_name is invalid";
        }
        return false;
    }
    if (result_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "result_out is null";
        }
        return false;
    }

    *result_out = GetFakeJavaEnvIsInstanceOfResultForTesting();
    return true;
}

bool FakeJavaEnvNewStringUtfForTesting(uint64_t env_ptr,
                                       const char* utf8_text,
                                       uint64_t* string_ptr_out,
                                       std::string* error_out) {
    GetLastJavaEnvNewStringUtfPointerForTesting() = env_ptr;
    GetLastJavaEnvNewStringUtfTextForTesting() = utf8_text != nullptr ? utf8_text : "";
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (utf8_text == nullptr) {
        if (error_out != nullptr) {
            *error_out = "utf8_text is null";
        }
        return false;
    }
    if (string_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "string_ptr_out is null";
        }
        return false;
    }

    *string_ptr_out = GetFakeJavaEnvNewStringUtfResultForTesting();
    return true;
}

bool FakeJavaEnvGetStringUtfCharsForTesting(uint64_t env_ptr,
                                            uint64_t jstring_ptr,
                                            uint64_t* chars_ptr_out,
                                            std::string* error_out) {
    GetLastJavaEnvGetStringUtfCharsPointerForTesting() = env_ptr;
    GetLastJavaEnvGetStringUtfCharsStringHandleForTesting() = jstring_ptr;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (jstring_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "jstring_ptr is null";
        }
        return false;
    }
    if (chars_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "chars_ptr_out is null";
        }
        return false;
    }

    *chars_ptr_out = GetFakeJavaEnvGetStringUtfCharsResultForTesting();
    return true;
}

bool FakeJavaEnvReleaseStringUtfCharsForTesting(uint64_t env_ptr,
                                                uint64_t jstring_ptr,
                                                uint64_t chars_ptr,
                                                std::string* error_out) {
    GetLastJavaEnvReleaseStringUtfCharsPointerForTesting() = env_ptr;
    GetLastJavaEnvReleaseStringUtfCharsStringHandleForTesting() = jstring_ptr;
    GetLastJavaEnvReleaseStringUtfCharsCharsHandleForTesting() = chars_ptr;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (jstring_ptr == 0u || chars_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "string or chars handle is null";
        }
        return false;
    }
    return true;
}

bool FakeJavaEnvNewGlobalRefForTesting(uint64_t env_ptr,
                                       uint64_t object_handle,
                                       uint64_t* ref_ptr_out,
                                       std::string* error_out) {
    GetLastJavaEnvNewGlobalRefPointerForTesting() = env_ptr;
    GetLastJavaEnvNewGlobalRefObjectHandleForTesting() = object_handle;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (object_handle == 0u) {
        if (error_out != nullptr) {
            *error_out = "object handle is null";
        }
        return false;
    }
    if (ref_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "ref_ptr_out is null";
        }
        return false;
    }

    *ref_ptr_out = GetFakeJavaEnvNewGlobalRefResultForTesting();
    return true;
}

bool FakeJavaEnvDeleteGlobalRefForTesting(uint64_t env_ptr,
                                          uint64_t ref_ptr,
                                          std::string* error_out) {
    GetLastJavaEnvDeleteGlobalRefPointerForTesting() = env_ptr;
    GetLastJavaEnvDeleteGlobalRefHandleForTesting() = ref_ptr;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (ref_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "ref_ptr is null";
        }
        return false;
    }
    return true;
}

bool FakeJavaEnvNewWeakGlobalRefForTesting(uint64_t env_ptr,
                                           uint64_t object_handle,
                                           uint64_t* ref_ptr_out,
                                           std::string* error_out) {
    GetLastJavaEnvNewWeakGlobalRefPointerForTesting() = env_ptr;
    GetLastJavaEnvNewWeakGlobalRefObjectHandleForTesting() = object_handle;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (object_handle == 0u) {
        if (error_out != nullptr) {
            *error_out = "object handle is null";
        }
        return false;
    }
    if (ref_ptr_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "ref_ptr_out is null";
        }
        return false;
    }

    *ref_ptr_out = GetFakeJavaEnvNewWeakGlobalRefResultForTesting();
    return true;
}

bool FakeJavaEnvDeleteWeakGlobalRefForTesting(uint64_t env_ptr,
                                              uint64_t ref_ptr,
                                              std::string* error_out) {
    GetLastJavaEnvDeleteWeakGlobalRefPointerForTesting() = env_ptr;
    GetLastJavaEnvDeleteWeakGlobalRefHandleForTesting() = ref_ptr;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (ref_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "ref_ptr is null";
        }
        return false;
    }
    return true;
}

bool FakeJavaEnvGetObjectRefTypeForTesting(uint64_t env_ptr,
                                           uint64_t object_handle,
                                           uint32_t* ref_type_out,
                                           std::string* error_out) {
    GetLastJavaEnvGetObjectRefTypePointerForTesting() = env_ptr;
    GetLastJavaEnvGetObjectRefTypeObjectHandleForTesting() = object_handle;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (object_handle == 0u) {
        if (error_out != nullptr) {
            *error_out = "object handle is null";
        }
        return false;
    }
    if (ref_type_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "ref_type_out is null";
        }
        return false;
    }

    *ref_type_out = GetFakeJavaEnvGetObjectRefTypeResultForTesting();
    return true;
}

bool FakeJavaEnvGetSuperclassForTesting(uint64_t env_ptr,
                                        const char* class_name,
                                        uint64_t loader_handle,
                                        bool* has_superclass_out,
                                        std::string* superclass_name_out,
                                        std::string* error_out) {
    GetLastJavaEnvGetSuperclassPointerForTesting() = env_ptr;
    GetLastJavaEnvGetSuperclassClassNameForTesting() = class_name != nullptr ? class_name : "";
    GetLastJavaEnvGetSuperclassLoaderHandleForTesting() = loader_handle;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (class_name == nullptr || class_name[0] == '\0') {
        if (error_out != nullptr) {
            *error_out = "class name is invalid";
        }
        return false;
    }
    if (has_superclass_out == nullptr || superclass_name_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "superclass outputs are null";
        }
        return false;
    }

    *has_superclass_out = GetFakeJavaEnvGetSuperclassHasResultForTesting();
    *superclass_name_out = GetFakeJavaEnvGetSuperclassResultNameForTesting();
    return true;
}

bool FakeJavaEnvIsAssignableFromForTesting(uint64_t env_ptr,
                                           const char* target_class_name,
                                           uint64_t target_loader_handle,
                                           const char* source_class_name,
                                           uint64_t source_loader_handle,
                                           bool* result_out,
                                           std::string* error_out) {
    GetLastJavaEnvIsAssignableFromPointerForTesting() = env_ptr;
    GetLastJavaEnvIsAssignableFromTargetClassNameForTesting() =
        target_class_name != nullptr ? target_class_name : "";
    GetLastJavaEnvIsAssignableFromTargetLoaderHandleForTesting() = target_loader_handle;
    GetLastJavaEnvIsAssignableFromSourceClassNameForTesting() =
        source_class_name != nullptr ? source_class_name : "";
    GetLastJavaEnvIsAssignableFromSourceLoaderHandleForTesting() = source_loader_handle;
    if (env_ptr == 0u) {
        if (error_out != nullptr) {
            *error_out = "env pointer is null";
        }
        return false;
    }
    if (target_class_name == nullptr || target_class_name[0] == '\0' ||
        source_class_name == nullptr || source_class_name[0] == '\0') {
        if (error_out != nullptr) {
            *error_out = "class name is invalid";
        }
        return false;
    }
    if (result_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "result_out is null";
        }
        return false;
    }

    *result_out = GetFakeJavaEnvIsAssignableFromResultForTesting();
    return true;
}

std::string FormatJavaJsArrayForArraysToString(const JavaJsValue& value) {
    std::ostringstream stream;
    stream << "[";
    for (size_t i = 0; i < value.array_elements.size(); ++i) {
        if (i > 0u) {
            stream << ", ";
        }
        const JavaJsValue& element = value.array_elements[i];
        if (value.array_type_name == "boolean[]") {
            stream << ((element.kind == JavaJsValueKind::kBoolean && element.bool_value) ? "true" : "false");
        } else if (value.array_type_name == "byte[]" || value.array_type_name == "short[]") {
            if (element.kind == JavaJsValueKind::kInt32) {
                stream << element.int_value;
            } else if (element.kind == JavaJsValueKind::kDouble) {
                stream << static_cast<int32_t>(element.double_value);
            }
        } else if (value.array_type_name == "char[]") {
            if (element.kind == JavaJsValueKind::kString) {
                stream << element.string_value;
            }
        } else if (value.array_type_name == "long[]") {
            if (element.kind == JavaJsValueKind::kInt64) {
                stream << element.int64_value;
            } else if (element.kind == JavaJsValueKind::kInt32) {
                stream << element.int_value;
            } else if (element.kind == JavaJsValueKind::kDouble) {
                stream << static_cast<int64_t>(element.double_value);
            }
        } else if (value.array_type_name == "float[]") {
            if (element.kind == JavaJsValueKind::kFloat) {
                stream << element.float_value;
            } else if (element.kind == JavaJsValueKind::kDouble) {
                stream << static_cast<float>(element.double_value);
            } else if (element.kind == JavaJsValueKind::kInt32) {
                stream << static_cast<float>(element.int_value);
            }
        } else if (value.array_type_name == "double[]") {
            if (element.kind == JavaJsValueKind::kDouble) {
                stream << element.double_value;
            } else if (element.kind == JavaJsValueKind::kFloat) {
                stream << static_cast<double>(element.float_value);
            } else if (element.kind == JavaJsValueKind::kInt32) {
                stream << static_cast<double>(element.int_value);
            } else if (element.kind == JavaJsValueKind::kInt64) {
                stream << static_cast<double>(element.int64_value);
            }
        } else {
            if (element.kind == JavaJsValueKind::kString) {
                stream << element.string_value;
            } else if (element.kind == JavaJsValueKind::kBoolean) {
                stream << (element.bool_value ? "true" : "false");
            } else if (element.kind == JavaJsValueKind::kArray) {
                stream << FormatJavaJsArrayForArraysToString(element);
            } else if (element.kind == JavaJsValueKind::kInt32) {
                stream << element.int_value;
            } else if (element.kind == JavaJsValueKind::kInt64) {
                stream << element.int64_value;
            } else if (element.kind == JavaJsValueKind::kFloat) {
                stream << element.float_value;
            } else if (element.kind == JavaJsValueKind::kDouble) {
                stream << element.double_value;
            }
        }
    }
    stream << "]";
    return stream.str();
}

struct JavaConstructorInvokeCapture {
    int call_count = 0;
    JavaJsMethodRecord record = {};
    std::vector<JavaJsValue> args;
    uint64_t created_handle = 0u;
};

JavaConstructorInvokeCapture& GetJavaConstructorInvokeCapture() {
    static JavaConstructorInvokeCapture capture;
    return capture;
}

struct JavaRetainCapture {
    int call_count = 0;
    uint64_t source_handle = 0u;
    uint64_t retained_handle = 0u;
};

JavaRetainCapture& GetJavaRetainCapture() {
    static JavaRetainCapture capture;
    return capture;
}

struct JavaReleaseCapture {
    int call_count = 0;
    uint64_t released_handle = 0u;
};

JavaReleaseCapture& GetJavaReleaseCapture() {
    static JavaReleaseCapture capture;
    return capture;
}

struct JavaChooseCapture {
    int call_count = 0;
    std::string class_name;
    uint64_t loader_handle = 0u;
    std::vector<uint64_t> handles;
};

JavaChooseCapture& GetJavaChooseCapture() {
    static JavaChooseCapture capture;
    return capture;
}

struct JavaEnumerateLoadedClassesCapture {
    int call_count = 0;
    std::vector<std::string> class_names;
};

JavaEnumerateLoadedClassesCapture& GetJavaEnumerateLoadedClassesCapture() {
    static JavaEnumerateLoadedClassesCapture capture;
    return capture;
}

struct JavaMethodResolveCapture {
    int call_count = 0;
    std::string class_name;
    std::string method_name;
    std::vector<std::string> argument_type_names;
    bool is_static = false;
    uint64_t loader_handle = 0u;
};

JavaMethodResolveCapture& GetJavaMethodResolveCapture() {
    static JavaMethodResolveCapture capture;
    return capture;
}

struct JavaEnumerateClassLoadersCapture {
    int call_count = 0;
    std::vector<JavaJsValue> loaders;
};

JavaEnumerateClassLoadersCapture& GetJavaEnumerateClassLoadersCapture() {
    static JavaEnumerateClassLoadersCapture capture;
    return capture;
}

struct JavaRegisterClassCapture {
    int call_count = 0;
    JavaJsRegisterClassRequest request = {};
};

JavaRegisterClassCapture& GetJavaRegisterClassCapture() {
    static JavaRegisterClassCapture capture;
    return capture;
}

struct JavaMainThreadState {
    bool current_application_available = true;
    uint64_t current_looper_handle = 0x5000u;
    uint64_t main_looper_handle = 0x5000u;
    uint64_t handler_handle = 0x5001u;
    bool current_and_main_looper_same_object = true;
};

JavaMainThreadState& GetJavaMainThreadState() {
    static JavaMainThreadState state;
    return state;
}

struct JavaFieldResolveCallCapture {
    int call_count = 0;
    std::string class_name;
    std::string field_name;
    uint64_t loader_handle = 0u;
    bool is_static = false;
};

JavaFieldResolveCallCapture& GetJavaFieldResolveCallCapture() {
    static JavaFieldResolveCallCapture capture;
    return capture;
}

struct JavaFieldAccessKey {
    std::string class_name;
    std::string field_name;
    std::string reflected_field_name;
    bool is_static = false;
    uint64_t receiver_handle = 0;

    bool operator==(const JavaFieldAccessKey& other) const {
        return class_name == other.class_name &&
               field_name == other.field_name &&
               reflected_field_name == other.reflected_field_name &&
               is_static == other.is_static &&
               receiver_handle == other.receiver_handle;
    }
};

struct JavaFieldAccessKeyHash {
    std::size_t operator()(const JavaFieldAccessKey& key) const {
        std::size_t hash = std::hash<std::string>{}(key.class_name);
        hash ^= (std::hash<std::string>{}(key.field_name) << 1);
        hash ^= (std::hash<std::string>{}(key.reflected_field_name) << 2);
        hash ^= (std::hash<uint64_t>{}(key.receiver_handle) << 3);
        hash ^= (std::hash<bool>{}(key.is_static) << 4);
        return hash;
    }
};

using JavaFieldValueStore =
    std::unordered_map<JavaFieldAccessKey, JavaJsValue, JavaFieldAccessKeyHash>;

JavaFieldValueStore& GetJavaFieldValueStore() {
    static JavaFieldValueStore store;
    return store;
}

JavaFieldAccessKey MakeJavaFieldAccessKey(const JavaJsFieldRecord& record,
                                          uint64_t receiver_handle) {
    JavaFieldAccessKey key = {};
    key.class_name = record.class_name;
    key.field_name = record.field_name;
    key.reflected_field_name = record.reflected_field_name;
    key.is_static = record.is_static;
    key.receiver_handle = receiver_handle;
    return key;
}

bool FakeResolveJavaMethodSignature(const std::string& class_name,
                                    const std::string& method_name,
                                    const std::vector<std::string>& argument_type_names,
                                    uint64_t loader_handle,
                                    bool allow_static,
                                    std::string* signature,
                                    std::string* error_message);

bool FakeInstallJavaHook(const JavaJsHookRequest& request,
                         JavaJsHookRecord* out_record,
                         std::string* error_message) {
    (void)error_message;
    JavaHookInstallCallCapture& capture = GetJavaHookInstallCallCapture();
    ++capture.call_count;
    capture.request = request;
    if (out_record != nullptr) {
        out_record->hook_id = request.hook_id;
        out_record->class_name = request.class_name;
        out_record->method_name = request.method_name;
        out_record->signature = request.signature;
        out_record->loader_handle = request.loader_handle;
        out_record->is_static = request.is_static;
        out_record->deferred = request.deferred;
        out_record->installed_hook_id = static_cast<int>(4000u + request.hook_id);
    }
    return true;
}

bool FakeRetainJavaObject(uint64_t object_handle,
                          uint64_t* retained_handle,
                          std::string* error_message) {
    (void)error_message;
    JavaRetainCapture& capture = GetJavaRetainCapture();
    ++capture.call_count;
    capture.source_handle = object_handle;
    capture.retained_handle = object_handle + 0x1000u;
    if (retained_handle != nullptr) {
        *retained_handle = capture.retained_handle;
    }
    return true;
}

bool FakeReleaseJavaObject(uint64_t object_handle, std::string* error_message) {
    (void)error_message;
    JavaReleaseCapture& capture = GetJavaReleaseCapture();
    ++capture.call_count;
    capture.released_handle = object_handle;
    return true;
}

JavaJsHookInstallerDependencies MakeJavaRetainReleaseDependencies() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.retain_object = &FakeRetainJavaObject;
    dependencies.release_object = &FakeReleaseJavaObject;
    return dependencies;
}

const char* GetJavaRetainWithoutDisposeSource() {
    return
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  Java.retain(this);"
        "};";
}

const char* GetJavaRetainWithDisposeSource() {
    return
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var kept = Java.retain(this);"
        "  kept.$dispose();"
        "};";
}

void InvokeJavaInitViewHookOnce(uint32_t script_id, std::string* error_message) {
    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, error_message));
}

bool FakeEnumerateJavaObjects(const std::string& class_name,
                              uint64_t loader_handle,
                              std::vector<JavaJsValue>* matches,
                              std::string* error_message) {
    (void)error_message;
    JavaChooseCapture& capture = GetJavaChooseCapture();
    ++capture.call_count;
    capture.class_name = class_name;
    capture.loader_handle = loader_handle;
    capture.handles.clear();
    if (matches == nullptr) {
        return false;
    }
    matches->clear();

    JavaJsValue first = {};
    first.kind = JavaJsValueKind::kObject;
    first.object_handle = 0x1234u;
    first.object_class_name = class_name;
    matches->push_back(first);
    capture.handles.push_back(first.object_handle);

    JavaJsValue second = {};
    second.kind = JavaJsValueKind::kObject;
    second.object_handle = 0x2345u;
    second.object_class_name = class_name;
    matches->push_back(second);
    capture.handles.push_back(second.object_handle);
    return true;
}

bool FakeEnumerateLoadedJavaClasses(std::vector<std::string>* class_names,
                                    std::string* error_message) {
    (void)error_message;
    JavaEnumerateLoadedClassesCapture& capture = GetJavaEnumerateLoadedClassesCapture();
    ++capture.call_count;
    capture.class_names = {
        "com.demo.target.LoginFragment",
        "com.demo.target.TextFragment",
        "com.demo.target.TextFragment"
    };
    if (class_names != nullptr) {
        *class_names = capture.class_names;
    }
    return true;
}

bool FakeEnumerateJavaClassLoaders(std::vector<JavaJsValue>* matches,
                                   std::string* error_message) {
    (void)error_message;
    JavaEnumerateClassLoadersCapture& capture = GetJavaEnumerateClassLoadersCapture();
    ++capture.call_count;
    capture.loaders.clear();

    JavaJsValue app_loader = {};
    app_loader.kind = JavaJsValueKind::kObject;
    app_loader.object_handle = 0x1111u;
    app_loader.object_class_name = "dalvik.system.PathClassLoader";
    capture.loaders.push_back(app_loader);

    JavaJsValue app_loader_duplicate = app_loader;
    capture.loaders.push_back(app_loader_duplicate);

    JavaJsValue boot_loader = {};
    boot_loader.kind = JavaJsValueKind::kObject;
    boot_loader.object_handle = 0x2222u;
    boot_loader.object_class_name = "java.lang.BootClassLoader";
    capture.loaders.push_back(boot_loader);

    if (matches != nullptr) {
        *matches = capture.loaders;
    }
    return true;
}

bool FakeRegisterJavaClass(const JavaJsRegisterClassRequest& request,
                           JavaJsValue* result,
                           std::string* error_message) {
    (void)error_message;
    JavaRegisterClassCapture& capture = GetJavaRegisterClassCapture();
    ++capture.call_count;
    capture.request = request;
    if (result != nullptr) {
        result->kind = JavaJsValueKind::kObject;
        result->object_handle = 0x7777u;
        result->object_class_name = "java.lang.reflect.Proxy";
        result->object_handle_is_global = true;
    }
    return true;
}

bool FakeResolveJavaField(const std::string& class_name,
                          const std::string& field_name,
                          uint64_t loader_handle,
                          bool is_static,
                          JavaJsFieldRecord* out_record,
                          std::string* error_message) {
    (void)error_message;
    JavaFieldResolveCallCapture& capture = GetJavaFieldResolveCallCapture();
    ++capture.call_count;
    capture.class_name = class_name;
    capture.field_name = field_name;
    capture.loader_handle = loader_handle;
    capture.is_static = is_static;

    if (out_record == nullptr) {
        return false;
    }

    if (class_name == "com.demo.target.MainActivity" &&
        field_name == "interceptCount" &&
        is_static) {
        out_record->class_name = class_name;
        out_record->field_name = field_name;
        out_record->reflected_field_name = field_name;
        out_record->signature = "I";
        out_record->loader_handle = loader_handle;
        out_record->is_static = true;
        return true;
    }

    if (class_name == "com.demo.target.AdWallFragment" &&
        field_name == "adCount" &&
        !is_static) {
        out_record->class_name = class_name;
        out_record->field_name = field_name;
        out_record->reflected_field_name = field_name;
        out_record->signature = "I";
        out_record->loader_handle = loader_handle;
        out_record->is_static = false;
        return true;
    }

    if (class_name == "com.ad2001.frida0x6.Checker" &&
        (field_name == "num1" || field_name == "num2") &&
        !is_static) {
        out_record->class_name = class_name;
        out_record->field_name = field_name;
        out_record->reflected_field_name = field_name;
        out_record->signature = "I";
        out_record->loader_handle = loader_handle;
        out_record->is_static = false;
        return true;
    }

    if (class_name == "com.zj.wuaipojie.Demo" &&
        !is_static &&
        (field_name == "privateInt" || field_name == "_privateInt")) {
        out_record->class_name = class_name;
        out_record->field_name = field_name;
        out_record->reflected_field_name = "privateInt";
        out_record->signature = "I";
        out_record->loader_handle = loader_handle;
        out_record->is_static = false;
        out_record->uses_declared_field_lookup = true;
        return true;
    }

    return false;
}

bool FakeReadJavaField(const JavaJsFieldRecord& record,
                       uint64_t receiver_handle,
                       JavaJsValue* result,
                       std::string* error_message) {
    (void)error_message;
    if (result == nullptr) {
        return false;
    }

    JavaFieldAccessKey key = MakeJavaFieldAccessKey(record, receiver_handle);
    auto found = GetJavaFieldValueStore().find(key);
    if (found == GetJavaFieldValueStore().end()) {
        return false;
    }

    *result = found->second;
    return true;
}

bool FakeWriteJavaField(const JavaJsFieldRecord& record,
                        uint64_t receiver_handle,
                        const JavaJsValue& value,
                        std::string* error_message) {
    (void)error_message;
    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(record, receiver_handle)] = value;
    return true;
}

bool FakeCallOriginalJavaHook(const JavaJsHookRecord& record,
                              const JavaJsValue* args,
                              std::size_t arg_count,
                              JavaJsValue* result,
                              std::string* error_message) {
    (void)error_message;
    JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    ++capture.call_count;
    capture.record = record;
    capture.args.assign(args, args + arg_count);
    if (result != nullptr) {
        if (record.class_name == "com.demo.target.MainActivity" &&
            record.method_name == "incrementIntercept" &&
            record.signature == "(I)I") {
            result->kind = JavaJsValueKind::kInt32;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kInt32) {
                result->int_value = args[0].int_value + 1;
            } else if (arg_count > 0 &&
                       args[0].kind == JavaJsValueKind::kDouble &&
                       std::isfinite(args[0].double_value)) {
                result->int_value = static_cast<int32_t>(args[0].double_value) + 1;
            } else {
                result->int_value = 1;
            }
            return true;
        }
        result->kind = JavaJsValueKind::kString;
        if (arg_count > 0 && args[0].kind == JavaJsValueKind::kString) {
            result->string_value = std::string("original:") + args[0].string_value;
        } else if (arg_count > 0 &&
                   record.class_name == "com.demo.target.TextFragment" &&
                   record.method_name == "formatScaled" &&
                   record.signature == "(J)Ljava/lang/String;" &&
                   args[0].kind == JavaJsValueKind::kDouble) {
            std::ostringstream stream;
            stream << "original-long:" << std::fixed << std::setprecision(0)
                   << args[0].double_value;
            result->string_value = stream.str();
        } else if (arg_count > 0 &&
                   record.class_name == "com.demo.target.TextFragment" &&
                   record.method_name == "formatScaled" &&
                   record.signature == "(F)Ljava/lang/String;" &&
                   args[0].kind == JavaJsValueKind::kDouble) {
            std::ostringstream stream;
            stream << "original-float:" << std::fixed << std::setprecision(2)
                   << args[0].double_value;
            result->string_value = stream.str();
        } else if (arg_count > 0 && args[0].kind == JavaJsValueKind::kDouble) {
            std::ostringstream stream;
            stream << "original-double:" << std::fixed << std::setprecision(2)
                   << args[0].double_value;
            result->string_value = stream.str();
        } else {
            result->string_value = "original";
        }
    }
    return true;
}

bool FakeInvokeJavaMethod(const JavaJsMethodRecord& record,
                          uint64_t receiver_handle,
                          const JavaJsValue* args,
                          std::size_t arg_count,
                          JavaJsValue* result,
                          std::string* error_message) {
    (void)error_message;
    JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    ++capture.call_count;
    capture.record = record;
    capture.receiver_handle = receiver_handle;
    capture.args.assign(args, args + arg_count);
    if (result != nullptr) {
        if (record.class_name == "com.demo.target.TextFragment" &&
            record.method_name == "<init>" &&
            record.signature == "()V") {
            JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
            ++constructor_capture.call_count;
            constructor_capture.record = record;
            constructor_capture.args.assign(args, args + arg_count);
            constructor_capture.created_handle = 0x3456u;

            result->kind = JavaJsValueKind::kObject;
            result->object_handle = constructor_capture.created_handle;
            result->object_class_name = record.class_name;
            return true;
        }
        if (record.class_name == "com.zj.wuaipojie.Demo" &&
            record.method_name == "<init>" &&
            record.signature == "(Ljava/lang/String;)V") {
            JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
            ++constructor_capture.call_count;
            constructor_capture.record = record;
            constructor_capture.args.assign(args, args + arg_count);
            constructor_capture.created_handle = 0x4455u;

            result->kind = JavaJsValueKind::kUndefined;
            return true;
        }
        if (record.class_name == "com.ad2001.frida0x6.Checker" &&
            record.method_name == "<init>" &&
            record.signature == "()V") {
            JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
            ++constructor_capture.call_count;
            constructor_capture.record = record;
            constructor_capture.args.assign(args, args + arg_count);
            constructor_capture.created_handle = 0x6789u;

            result->kind = JavaJsValueKind::kObject;
            result->object_handle = constructor_capture.created_handle;
            result->object_class_name = record.class_name;
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.app.ActivityThread" &&
            record.method_name == "currentApplication" &&
            record.signature == "()Landroid/app/Application;") {
            const JavaMainThreadState& state = GetJavaMainThreadState();
            if (!state.current_application_available) {
                result->kind = JavaJsValueKind::kUndefined;
                return true;
            }
            result->kind = JavaJsValueKind::kObject;
            result->object_handle = 0x2000u;
            result->object_class_name = "android.app.Application";
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.app.Application" &&
            record.method_name == "getClassLoader" &&
            record.signature == "()Ljava/lang/ClassLoader;") {
            result->kind = JavaJsValueKind::kObject;
            result->object_handle = 0x1111u;
            result->object_class_name = "dalvik.system.PathClassLoader";
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.os.Looper" &&
            record.method_name == "myLooper" &&
            record.signature == "()Landroid/os/Looper;") {
            const JavaMainThreadState& state = GetJavaMainThreadState();
            result->kind = JavaJsValueKind::kObject;
            result->object_handle = state.current_looper_handle;
            result->object_class_name = "android.os.Looper";
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.os.Looper" &&
            record.method_name == "getMainLooper" &&
            record.signature == "()Landroid/os/Looper;") {
            const JavaMainThreadState& state = GetJavaMainThreadState();
            result->kind = JavaJsValueKind::kObject;
            result->object_handle = state.main_looper_handle;
            result->object_class_name = "android.os.Looper";
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.app.Application" &&
            record.method_name == "getCodeCacheDir" &&
            record.signature == "()Ljava/io/File;") {
            result->kind = JavaJsValueKind::kObject;
            result->object_handle = 0x3000u;
            result->object_class_name = "java.io.File";
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.app.Application" &&
            record.method_name == "getPackageCodePath" &&
            record.signature == "()Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            result->string_value = "/data/app/com.demo.target/base.apk";
            return true;
        }
        if (record.class_name == "java.io.File" &&
            record.method_name == "getAbsolutePath" &&
            record.signature == "()Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            result->string_value = "/data/user/0/com.demo.target/code_cache";
            return true;
        }
        if (record.class_name == "java.lang.Class" &&
            record.method_name == "getDeclaredMethods" &&
            record.signature == "()[Ljava/lang/reflect/Method;") {
            result->kind = JavaJsValueKind::kArray;
            result->array_type_name = "java.lang.reflect.Method[]";

            JavaJsValue first = {};
            first.kind = JavaJsValueKind::kObject;
            first.object_handle = 0x7010u;
            first.object_class_name = "java.lang.reflect.Method";
            first.object_handle_is_global = true;

            JavaJsValue second = {};
            second.kind = JavaJsValueKind::kObject;
            second.object_handle = 0x7020u;
            second.object_class_name = "java.lang.reflect.Method";
            second.object_handle_is_global = true;

            result->array_elements = { first, second };
            return true;
        }
        if (record.class_name == "java.lang.reflect.Method" &&
            record.method_name == "getName" &&
            record.signature == "()Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (receiver_handle == 0x7010u) {
                result->string_value = "a";
            } else if (receiver_handle == 0x7020u) {
                result->string_value = "test";
            } else {
                result->string_value = "method";
            }
            return true;
        }
        if (record.class_name == "java.lang.reflect.Method" &&
            record.method_name == "getParameterTypes" &&
            record.signature == "()[Ljava/lang/Class;") {
            result->kind = JavaJsValueKind::kArray;
            result->array_type_name = "java.lang.Class[]";
            if (receiver_handle == 0x7010u) {
                JavaJsValue string_class = {};
                string_class.kind = JavaJsValueKind::kObject;
                string_class.object_handle = 0x8010u;
                string_class.object_class_name = "java.lang.Class";
                string_class.object_handle_is_global = true;
                result->array_elements = { string_class };
            } else {
                result->array_elements = {};
            }
            return true;
        }
        if (record.class_name == "java.lang.reflect.Method" &&
            record.method_name == "getReturnType" &&
            record.signature == "()Ljava/lang/Class;") {
            result->kind = JavaJsValueKind::kObject;
            if (receiver_handle == 0x7010u) {
                result->object_handle = 0x8011u;
                result->object_class_name = "java.lang.Class";
            } else {
                result->object_handle = 0x8012u;
                result->object_class_name = "java.lang.Class";
            }
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "java.lang.reflect.Method" &&
            record.method_name == "getModifiers" &&
            record.signature == "()I") {
            result->kind = JavaJsValueKind::kInt32;
            result->int_value = 0;
            return true;
        }
        if (record.class_name == "java.lang.Class" &&
            record.method_name == "getName" &&
            record.signature == "()Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (receiver_handle == 0x8010u) {
                result->string_value = "java.lang.String";
            } else if (receiver_handle == 0x8011u) {
                result->string_value = "java.lang.String";
            } else if (receiver_handle == 0x8012u) {
                result->string_value = "void";
            } else {
                result->string_value = "java.lang.Object";
            }
            return true;
        }
        if (record.class_name == "java.lang.reflect.Modifier" &&
            record.method_name == "isStatic" &&
            record.signature == "(I)Z") {
            result->kind = JavaJsValueKind::kBoolean;
            result->bool_value = false;
            return true;
        }
        if (record.class_name == "dalvik.system.DexClassLoader" &&
            record.method_name == "<init>" &&
            record.signature ==
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V") {
            JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
            ++constructor_capture.call_count;
            constructor_capture.record = record;
            constructor_capture.args.assign(args, args + arg_count);
            constructor_capture.created_handle = 0x4567u;

            result->kind = JavaJsValueKind::kObject;
            result->object_handle = constructor_capture.created_handle;
            result->object_class_name = "dalvik.system.DexClassLoader";
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.os.Handler" &&
            record.method_name == "<init>" &&
            record.signature == "(Landroid/os/Looper;)V") {
            JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
            ++constructor_capture.call_count;
            constructor_capture.record = record;
            constructor_capture.args.assign(args, args + arg_count);
            constructor_capture.created_handle = GetJavaMainThreadState().handler_handle;

            result->kind = JavaJsValueKind::kObject;
            result->object_handle = constructor_capture.created_handle;
            result->object_class_name = "android.os.Handler";
            result->object_handle_is_global = true;
            return true;
        }
        if (record.class_name == "android.os.Handler" &&
            record.method_name == "post" &&
            record.signature == "(Ljava/lang/Runnable;)Z") {
            result->kind = JavaJsValueKind::kBoolean;
            result->bool_value = true;
            return true;
        }
        if (record.class_name == "android.os.Looper" &&
            record.method_name == "equals" &&
            record.signature == "(Ljava/lang/Object;)Z") {
            const JavaMainThreadState& state = GetJavaMainThreadState();
            bool same = false;
            if (arg_count > 0 &&
                args[0].kind == JavaJsValueKind::kObject &&
                ((receiver_handle == state.current_looper_handle &&
                  args[0].object_handle == state.main_looper_handle) ||
                 (receiver_handle == state.main_looper_handle &&
                  args[0].object_handle == state.current_looper_handle) ||
                 (receiver_handle == state.current_looper_handle &&
                  args[0].object_handle == state.current_looper_handle) ||
                 (receiver_handle == state.main_looper_handle &&
                  args[0].object_handle == state.main_looper_handle))) {
                same = state.current_and_main_looper_same_object;
            }
            result->kind = JavaJsValueKind::kBoolean;
            result->bool_value = same;
            return true;
        }
        if (record.class_name == "com.demo.target.NullableTarget" &&
            record.method_name == "describe" &&
            record.signature == "(Ljava/lang/Object;)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            result->string_value =
                (arg_count > 0 && args[0].kind == JavaJsValueKind::kUndefined) ? "nullable:null"
                                                                                : "nullable:value";
            return true;
        }
        if (record.class_name == "com.demo.target.BoxedTarget" &&
            record.method_name == "acceptBoolean" &&
            record.signature == "(Ljava/lang/Boolean;)Z") {
            result->kind = JavaJsValueKind::kBoolean;
            result->bool_value = arg_count > 0 &&
                                 args[0].kind == JavaJsValueKind::kBoolean &&
                                 args[0].bool_value;
            return true;
        }
        if (record.class_name == "com.demo.target.ObjectTarget" &&
            record.method_name == "describe" &&
            record.signature == "(Ljava/lang/Object;)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kString) {
                result->string_value = "object-string:" + args[0].string_value;
            } else if (arg_count > 0 && args[0].kind == JavaJsValueKind::kObject) {
                result->string_value = "object-wrapper:" + args[0].object_class_name;
            } else {
                result->string_value = "object-other";
            }
            return true;
        }
        if (record.class_name == "com.demo.target.NumberTarget" &&
            record.method_name == "describe" &&
            record.signature == "(Ljava/lang/Number;)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kDouble) {
                result->string_value = "number:" +
                                       std::to_string(static_cast<int>(args[0].double_value));
            } else {
                result->string_value = "number";
            }
            return true;
        }
        if (record.class_name == "com.demo.target.MainActivity" &&
            record.method_name == "incrementIntercept" &&
            record.signature == "(I)I") {
            result->kind = JavaJsValueKind::kInt32;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kDouble) {
                result->int_value = static_cast<int32_t>(args[0].double_value) + 1;
            } else if (arg_count > 0 && args[0].kind == JavaJsValueKind::kInt32) {
                result->int_value = args[0].int_value + 1;
            } else {
                result->int_value = 1;
            }
            return true;
        }
        if (record.class_name == "com.demo.target.LoginFragment" &&
            record.method_name == "verifyPasswordNative" &&
            record.signature == "(Ljava/lang/String;)Z") {
            result->kind = JavaJsValueKind::kBoolean;
            result->bool_value = true;
            return true;
        }
        if (record.class_name == "com.demo.target.TextFragment" &&
            record.method_name == "formatBalance" &&
            record.signature == "(D)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kDouble) {
                std::ostringstream stream;
                stream << "instance-double:" << std::fixed << std::setprecision(2)
                       << args[0].double_value;
                result->string_value = stream.str();
            } else {
                result->string_value = "instance-double";
            }
            return true;
        }
        if (record.class_name == "com.demo.target.TextFragment" &&
            record.method_name == "formatScaled" &&
            record.signature == "(J)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kDouble) {
                std::ostringstream stream;
                stream << "instance-long:" << std::fixed << std::setprecision(0)
                       << args[0].double_value;
                result->string_value = stream.str();
            } else {
                result->string_value = "instance-long";
            }
            return true;
        }
        if (record.class_name == "com.demo.target.TextFragment" &&
            record.method_name == "formatScaled" &&
            record.signature == "(F)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kDouble) {
                std::ostringstream stream;
                stream << "instance-float:" << std::fixed << std::setprecision(2)
                       << args[0].double_value;
                result->string_value = stream.str();
            } else {
                result->string_value = "instance-float";
            }
            return true;
        }
        if (record.class_name == "com.demo.target.ArrayTarget" &&
            record.method_name == "sumInts" &&
            record.signature == "([I)I") {
            result->kind = JavaJsValueKind::kInt32;
            result->int_value = 0;
            if (arg_count > 0 &&
                args[0].kind == JavaJsValueKind::kArray &&
                args[0].array_type_name == "int[]") {
                for (const JavaJsValue& element : args[0].array_elements) {
                    if (element.kind == JavaJsValueKind::kInt32) {
                        result->int_value += element.int_value;
                    } else if (element.kind == JavaJsValueKind::kDouble) {
                        result->int_value += static_cast<int32_t>(element.double_value);
                    }
                }
            }
            return true;
        }
        if (record.class_name == "com.demo.target.ArrayTarget" &&
            record.method_name == "joinStrings" &&
            record.signature == "([Ljava/lang/String;)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 &&
                args[0].kind == JavaJsValueKind::kArray &&
                args[0].array_type_name == "java.lang.String[]") {
                std::ostringstream stream;
                for (size_t i = 0; i < args[0].array_elements.size(); ++i) {
                    if (i > 0u) {
                        stream << "|";
                    }
                    if (args[0].array_elements[i].kind == JavaJsValueKind::kString) {
                        stream << args[0].array_elements[i].string_value;
                    }
                }
                result->string_value = stream.str();
            } else {
                result->string_value.clear();
            }
            return true;
        }
        if (record.class_name == "java.util.Arrays" &&
            record.method_name == "toString" &&
            record.signature == "([I)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kArray) {
                result->string_value = FormatJavaJsArrayForArraysToString(args[0]);
            } else {
                result->string_value.clear();
            }
            return true;
        }
        if (record.class_name == "java.util.Arrays" &&
            record.method_name == "toString" &&
            (record.signature == "([B)Ljava/lang/String;" ||
             record.signature == "([S)Ljava/lang/String;" ||
             record.signature == "([C)Ljava/lang/String;" ||
             record.signature == "([Z)Ljava/lang/String;" ||
             record.signature == "([J)Ljava/lang/String;" ||
             record.signature == "([F)Ljava/lang/String;" ||
             record.signature == "([D)Ljava/lang/String;" ||
             record.signature == "([Ljava/lang/Object;)Ljava/lang/String;" ||
             record.signature == "([Ljava/lang/String;)Ljava/lang/String;")) {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kArray) {
                result->string_value = FormatJavaJsArrayForArraysToString(args[0]);
            } else {
                result->string_value.clear();
            }
            return true;
        }
        if (record.class_name == "java.util.Arrays" &&
            record.method_name == "deepToString" &&
            record.signature == "([Ljava/lang/Object;)Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            if (arg_count > 0 && args[0].kind == JavaJsValueKind::kArray) {
                result->string_value = FormatJavaJsArrayForArraysToString(args[0]);
            } else {
                result->string_value.clear();
            }
            return true;
        }
        if (record.class_name == "com.demo.injected.Payload" &&
            record.method_name == "marker" &&
            record.signature == "()Ljava/lang/String;") {
            result->kind = JavaJsValueKind::kString;
            result->string_value = "injected-marker";
            return true;
        }
        result->kind = JavaJsValueKind::kUndefined;
    }
    return true;
}

bool FakeResolveJavaMethodSignature(const std::string& class_name,
                                    const std::string& method_name,
                                    const std::vector<std::string>& argument_type_names,
                                    uint64_t loader_handle,
                                    bool is_static,
                                    std::string* signature,
                                    std::string* error_message) {
    (void)error_message;
    JavaMethodResolveCapture& capture = GetJavaMethodResolveCapture();
    ++capture.call_count;
    capture.class_name = class_name;
    capture.method_name = method_name;
    capture.argument_type_names = argument_type_names;
    capture.loader_handle = loader_handle;
    capture.is_static = is_static;
    if (signature == nullptr) {
        return false;
    }
    if (class_name == "com.demo.target.TextFragment" &&
        method_name == "formatBalance" &&
        !is_static &&
        argument_type_names.size() == 1u) {
        if (argument_type_names[0] == "double") {
            *signature = "(D)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "java.lang.String") {
            *signature = "(Ljava/lang/String;)Ljava/lang/String;";
            return true;
        }
    }
    if (class_name == "com.demo.target.TextFragment" &&
        method_name == "initView" &&
        !is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "android.view.View") {
        *signature = "(Landroid/view/View;)V";
        return true;
    }
    if (class_name == "com.demo.target.TextFragment" &&
        method_name == "<init>" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()V";
        return true;
    }
    if (class_name == "com.demo.target.TextFragment" &&
        method_name == "formatScaled" &&
        !is_static &&
        argument_type_names.size() == 1u) {
        if (argument_type_names[0] == "long") {
            *signature = "(J)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "float") {
            *signature = "(F)Ljava/lang/String;";
            return true;
        }
    }
    if (class_name == "com.demo.target.ArrayTarget" &&
        method_name == "sumInts" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "int[]") {
        *signature = "([I)I";
        return true;
    }
    if (class_name == "com.demo.target.ArrayTarget" &&
        method_name == "joinStrings" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "java.lang.String[]") {
        *signature = "([Ljava/lang/String;)Ljava/lang/String;";
        return true;
    }
    if (class_name == "java.util.Arrays" &&
        method_name == "toString" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "int[]") {
        *signature = "([I)Ljava/lang/String;";
        return true;
    }
    if (class_name == "android.os.Looper" &&
        is_static &&
        argument_type_names.empty()) {
        if (method_name == "myLooper") {
            *signature = "()Landroid/os/Looper;";
            return true;
        }
        if (method_name == "getMainLooper") {
            *signature = "()Landroid/os/Looper;";
            return true;
        }
    }
    if (class_name == "android.os.Handler" &&
        method_name == "<init>" &&
        !is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "android.os.Looper") {
        *signature = "(Landroid/os/Looper;)V";
        return true;
    }
    if (class_name == "android.os.Handler" &&
        method_name == "post" &&
        !is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "java.lang.Runnable") {
        *signature = "(Ljava/lang/Runnable;)Z";
        return true;
    }
    if (class_name == "android.os.Looper" &&
        method_name == "equals" &&
        !is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "java.lang.Object") {
        *signature = "(Ljava/lang/Object;)Z";
        return true;
    }
    if (class_name == "com.demo.target.NullableTarget" &&
        method_name == "describe" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "__nook_null__") {
        *signature = "(Ljava/lang/Object;)Ljava/lang/String;";
        return true;
    }
    if (class_name == "com.demo.target.BoxedTarget" &&
        method_name == "acceptBoolean" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "java.lang.Boolean") {
        *signature = "(Ljava/lang/Boolean;)Z";
        return true;
    }
    if (class_name == "com.demo.target.ObjectTarget" &&
        method_name == "describe" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "java.lang.Object") {
        *signature = "(Ljava/lang/Object;)Ljava/lang/String;";
        return true;
    }
    if (class_name == "com.demo.target.NumberTarget" &&
        method_name == "describe" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "java.lang.Number") {
        *signature = "(Ljava/lang/Number;)Ljava/lang/String;";
        return true;
    }
    if (class_name == "java.util.Arrays" &&
        method_name == "toString" &&
        is_static &&
        argument_type_names.size() == 1u) {
        if (argument_type_names[0] == "byte[]") {
            *signature = "([B)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "short[]") {
            *signature = "([S)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "char[]") {
            *signature = "([C)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "boolean[]") {
            *signature = "([Z)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "long[]") {
            *signature = "([J)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "float[]") {
            *signature = "([F)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "double[]") {
            *signature = "([D)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "java.lang.String[]") {
            *signature = "([Ljava/lang/String;)Ljava/lang/String;";
            return true;
        }
        if (argument_type_names[0] == "java.lang.Object[]") {
            *signature = "([Ljava/lang/Object;)Ljava/lang/String;";
            return true;
        }
    }
    if (class_name == "java.util.Arrays" &&
        method_name == "deepToString" &&
        is_static &&
        argument_type_names.size() == 1u) {
        if (argument_type_names[0] == "boolean[][]" ||
            argument_type_names[0] == "byte[][]" ||
            argument_type_names[0] == "short[][]" ||
            argument_type_names[0] == "char[][]" ||
            argument_type_names[0] == "int[][]" ||
            argument_type_names[0] == "long[][]" ||
            argument_type_names[0] == "float[][]" ||
            argument_type_names[0] == "double[][]" ||
            argument_type_names[0] == "java.lang.String[][]" ||
            argument_type_names[0] == "java.lang.Object[][]") {
            *signature = "([Ljava/lang/Object;)Ljava/lang/String;";
            return true;
        }
    }
    if (class_name == "com.demo.target.MainActivity" &&
        method_name == "incrementIntercept" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "int") {
        *signature = "(I)I";
        return true;
    }
    if (class_name == "com.demo.target.MainActivity" &&
        method_name == "incrementIntercept" &&
        is_static &&
        argument_type_names.empty()) {
        *signature = "()V";
        return true;
    }
    if (class_name == "android.app.ActivityThread" &&
        method_name == "currentApplication" &&
        is_static &&
        argument_type_names.empty()) {
        *signature = "()Landroid/app/Application;";
        return true;
    }
    if (class_name == "android.app.Application" &&
        method_name == "getClassLoader" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/lang/ClassLoader;";
        return true;
    }
    if (class_name == "android.app.Application" &&
        method_name == "getCodeCacheDir" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/io/File;";
        return true;
    }
    if (class_name == "android.app.Application" &&
        method_name == "getPackageCodePath" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/lang/String;";
        return true;
    }
    if (class_name == "java.io.File" &&
        method_name == "getAbsolutePath" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/lang/String;";
        return true;
    }
    if (class_name == "java.lang.Class" &&
        method_name == "getDeclaredMethods" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()[Ljava/lang/reflect/Method;";
        return true;
    }
    if (class_name == "java.lang.reflect.Method" &&
        method_name == "getName" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/lang/String;";
        return true;
    }
    if (class_name == "java.lang.reflect.Method" &&
        method_name == "getParameterTypes" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()[Ljava/lang/Class;";
        return true;
    }
    if (class_name == "java.lang.reflect.Method" &&
        method_name == "getReturnType" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/lang/Class;";
        return true;
    }
    if (class_name == "java.lang.reflect.Method" &&
        method_name == "getModifiers" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()I";
        return true;
    }
    if (class_name == "java.lang.Class" &&
        method_name == "getName" &&
        !is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/lang/String;";
        return true;
    }
    if (class_name == "java.lang.reflect.Modifier" &&
        method_name == "isStatic" &&
        is_static &&
        argument_type_names.size() == 1u &&
        argument_type_names[0] == "int") {
        *signature = "(I)Z";
        return true;
    }
    if (class_name == "dalvik.system.DexClassLoader" &&
        method_name == "<init>" &&
        !is_static &&
        argument_type_names.size() == 4u &&
        argument_type_names[0] == "java.lang.String" &&
        argument_type_names[1] == "java.lang.String" &&
        argument_type_names[2] == "java.lang.String" &&
        argument_type_names[3] == "java.lang.ClassLoader") {
        *signature = "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V";
        return true;
    }
    if (class_name == "com.demo.injected.Payload" &&
        method_name == "marker" &&
        is_static &&
        argument_type_names.empty()) {
        *signature = "()Ljava/lang/String;";
        return true;
    }
    if (class_name == "com.demo.target.AdWallFragment" &&
        method_name == "loadAd" &&
        !is_static &&
        argument_type_names.size() == 2u &&
        argument_type_names[0] == "java.lang.String" &&
        argument_type_names[1] == "java.lang.String") {
        *signature = "(Ljava/lang/String;Ljava/lang/String;)V";
        return true;
    }
    if (class_name != "com.demo.target.LoginFragment" ||
        method_name != "verifyPasswordNative" ||
        is_static ||
        argument_type_names.size() != 1u ||
        argument_type_names[0] != "java.lang.String") {
        return false;
    }
    *signature = "(Ljava/lang/String;)Z";
    return true;
}

int& GetInlineHookUnhookCallCount() {
    static int count = 0;
    return count;
}

std::vector<NativeJsArgumentSnapshotRequest>& GetLastSnapshotRequests() {
    static std::vector<NativeJsArgumentSnapshotRequest> snapshots;
    return snapshots;
}

bool FakeInlineInstaller(const NativeJsHookRequest& request,
                         void** hook_handle,
                         std::string* error_message) {
    (void)error_message;
    if (request.type != "inline") {
        return false;
    }
    GetLastSnapshotRequests() = request.snapshots;
    *hook_handle = reinterpret_cast<void*>(0x1234u);
    return true;
}

bool FakeResolveLoadedSymbolAddress(const char* module_name,
                                    const char* symbol_name,
                                    void** target_address) {
    (void)module_name;
    (void)symbol_name;
    if (target_address != nullptr) {
        *target_address = reinterpret_cast<void*>(0x10000000u);
    }
    return true;
}

bool FailingResolveLoadedSymbolAddress(const char* module_name,
                                       const char* symbol_name,
                                       void** target_address) {
    (void)module_name;
    (void)symbol_name;
    if (target_address != nullptr) {
        *target_address = nullptr;
    }
    return false;
}

bool FakeResolveSymbolAddressFallback(const char* module_name,
                                      const char* symbol_name,
                                      void** target_address) {
    if (target_address == nullptr) {
        return false;
    }
    *target_address = nullptr;
    if (std::strcmp(module_name, "libc.so") == 0 &&
        std::strcmp(symbol_name, "strcmp") == 0) {
        *target_address = reinterpret_cast<void*>(0x70000000u);
        return true;
    }
    return false;
}

bool UnsafeInlineHookSymbolChecker(const char* module_name,
                                   const char* symbol_name,
                                   void* target_address) {
    (void)module_name;
    (void)symbol_name;
    return target_address != reinterpret_cast<void*>(0x10000000u);
}

NookStatus FakeInlineHookAddressInvoker(void* target_address,
                                        void* replacement,
                                        void** original,
                                        void** hook_handle) {
    (void)target_address;
    (void)replacement;
    auto fake_original = +[](uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t) -> uint64_t {
        return 0x4242u;
    };
    if (original != nullptr) {
        *original = reinterpret_cast<void*>(fake_original);
    }
    if (hook_handle != nullptr) {
        *hook_handle = reinterpret_cast<void*>(0x9abcu);
    }
    return NOOK_STATUS_OK;
}

NookStatus FakeInlineHookAddressInvokerCaptureX0(void* target_address,
                                                 void* replacement,
                                                 void** original,
                                                 void** hook_handle) {
    (void)target_address;
    (void)replacement;
    auto fake_original = +[](uint64_t x0,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t) -> uint64_t {
        GetLastInlineHookOriginalX0() = x0;
        return x0 + 1u;
    };
    if (original != nullptr) {
        *original = reinterpret_cast<void*>(fake_original);
    }
    if (hook_handle != nullptr) {
        *hook_handle = reinterpret_cast<void*>(0x9abdu);
    }
    return NOOK_STATUS_OK;
}

NookStatus FakeInlineHookAddressInvokerCaptureReplacement(void* target_address,
                                                          void* replacement,
                                                          void** original,
                                                          void** hook_handle) {
    (void)target_address;
    GetInlineHookInvokerCapture().replacement = replacement;
    auto fake_original = +[](uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t) -> uint64_t {
        return 0x4242u;
    };
    if (original != nullptr) {
        *original = reinterpret_cast<void*>(fake_original);
    }
    if (hook_handle != nullptr) {
        *hook_handle = reinterpret_cast<void*>(0x9abeu);
    }
    return NOOK_STATUS_OK;
}

NookStatus FakeInlineHookUnhookInvoker(void* hook_handle) {
    (void)hook_handle;
    ++GetInlineHookUnhookCallCount();
    return NOOK_STATUS_OK;
}

NookStatus& GetEnsureObserverAsyncStatus() {
    static NookStatus status = NOOK_STATUS_OK;
    return status;
}

NookStatus FakeEnsureInlineHookModuleObserverAsync() {
    return GetEnsureObserverAsyncStatus();
}

std::string FormatTestPointer(const void* pointer) {
    std::ostringstream stream;
    stream << "0x" << std::hex << reinterpret_cast<uintptr_t>(pointer);
    return stream.str();
}

std::string GetCurrentTestModuleName() {
#if defined(_WIN32)
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(sizeof(path)));
    assert(length > 0 && length < sizeof(path));
    const char* base_name = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            base_name = cursor + 1;
        }
    }
    return std::string(base_name);
#else
    char path[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    assert(length > 0 && static_cast<size_t>(length) < sizeof(path));
    path[length] = '\0';
    const char* base_name = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            base_name = cursor + 1;
        }
    }
    return std::string(base_name);
#endif
}

struct TestFrameRecord {
    uint64_t previous_frame = 0u;
    uint64_t saved_link_register = 0u;
};

uint64_t GetCurrentTestModuleBaseAddress() {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleA(nullptr);
    assert(module != nullptr);
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(module));
#else
    void* module_base = ElfHooker::get_module_base(0, GetCurrentTestModuleName().c_str());
    assert(module_base != nullptr);
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(module_base));
#endif
}

uint32_t& GetNativeFunctionSinkValue() {
    static uint32_t value = 0u;
    return value;
}

uint64_t& GetLastJniEnvPointer() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastJniStringPointer() {
    static uint64_t value = 0u;
    return value;
}

uint64_t& GetLastInlineHookOriginalX0() {
    static uint64_t value = 0u;
    return value;
}

bool FakeReadJStringUtf8(uint64_t env_ptr,
                         uint64_t jstring_ptr,
                         std::string* text_out,
                         std::string* error_out) {
    (void)error_out;
    GetLastJniEnvPointer() = env_ptr;
    GetLastJniStringPointer() = jstring_ptr;
    if (text_out != nullptr) {
        *text_out = "jni-password";
    }
    return true;
}

uint32_t& GetReplaceableAddEntryCount() {
    static uint32_t count = 0u;
    return count;
}

using ReplaceThunk = uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t);

ReplaceThunk& GetReplaceThunk() {
    static ReplaceThunk thunk = nullptr;
    return thunk;
}

void*& GetReplaceHookHandle() {
    static void* handle = nullptr;
    return handle;
}

extern "C" int TestNativeFunctionAdd(int left, int right) {
    return left + right;
}

extern "C" bool TestNativeFunctionBoolNot(bool value) {
    return !value;
}

extern "C" int16_t TestNativeFunctionAddS16(int16_t left, int16_t right) {
    return static_cast<int16_t>(left + right);
}

extern "C" uint64_t TestNativeFunctionAddU64(uint64_t left, uint64_t right) {
    return left + right;
}

extern "C" float TestNativeFunctionAddFloat(float left, float right) {
    return left + right;
}

extern "C" double TestNativeFunctionAddDouble(double left, double right) {
    return left + right;
}

extern "C" uint64_t TestNativeFunctionMixU64Double(uint64_t left, double right) {
    return left + static_cast<uint64_t>(right * 10.0);
}

extern "C" float TestNativeFunctionMixFloatU32(float left, uint32_t right) {
    return left + static_cast<float>(right);
}

extern "C" double TestNativeFunctionMixDoubleU32(double left, uint32_t right) {
    return left + static_cast<double>(right) + 0.25;
}

extern "C" uintptr_t TestNativeFunctionEchoPointer(uintptr_t value) {
    return value;
}

extern "C" void TestNativeFunctionSinkU32(uint32_t value) {
    GetNativeFunctionSinkValue() = value;
}

extern "C" uint32_t TestNativeFunctionReplaceableAdd(uint32_t left, uint32_t right) {
    GetReplaceableAddEntryCount() += 1u;
    ReplaceThunk thunk = GetReplaceThunk();
    if (thunk != nullptr) {
        return static_cast<uint32_t>(thunk(left, right, 0u, 0u));
    }
    return left + right;
}

NookStatus FakeReplaceInlineHookAddressInvoker(void* target_address,
                                               void* replacement,
                                               void** original,
                                               void** hook_handle) {
    if (target_address != reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd) ||
        replacement == nullptr || hook_handle == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    GetReplaceThunk() = reinterpret_cast<ReplaceThunk>(replacement);
    GetReplaceHookHandle() = reinterpret_cast<void*>(0xfeedu);
    if (original != nullptr) {
        *original = reinterpret_cast<void*>(
            +[](uint64_t left, uint64_t right, uint64_t, uint64_t) -> uint64_t {
                return left + right;
            });
    }
    *hook_handle = GetReplaceHookHandle();
    return NOOK_STATUS_OK;
}

NookStatus FakeReplaceInlineHookUnhookInvoker(void* hook_handle) {
    if (hook_handle != GetReplaceHookHandle() || hook_handle == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }
    GetReplaceThunk() = nullptr;
    GetReplaceHookHandle() = nullptr;
    return NOOK_STATUS_OK;
}

class ScopedTestPageMapping {
public:
    ScopedTestPageMapping() {
#if defined(_WIN32)
        SYSTEM_INFO info = {};
        GetSystemInfo(&info);
        size_ = static_cast<size_t>(info.dwPageSize);
        address_ = VirtualAlloc(nullptr, size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        const long page_size = sysconf(_SC_PAGESIZE);
        size_ = page_size > 0 ? static_cast<size_t>(page_size) : 4096u;
        address_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (address_ == MAP_FAILED) {
            address_ = nullptr;
        }
#endif
    }

    ~ScopedTestPageMapping() {
#if defined(_WIN32)
        if (address_ != nullptr) {
            VirtualFree(address_, 0, MEM_RELEASE);
        }
#else
        if (address_ != nullptr) {
            munmap(address_, size_);
        }
#endif
    }

    void* address() const { return address_; }
    size_t size() const { return size_; }

private:
    void* address_ = nullptr;
    size_t size_ = 0;
};

class ScopedTestGuardedPageMapping {
public:
    ScopedTestGuardedPageMapping() {
#if defined(_WIN32)
        SYSTEM_INFO info = {};
        GetSystemInfo(&info);
        page_size_ = static_cast<size_t>(info.dwPageSize);
        size_ = page_size_ * 2u;
        address_ = VirtualAlloc(nullptr, size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (address_ != nullptr) {
            DWORD old_protect = 0;
            if (!VirtualProtect(static_cast<uint8_t*>(address_) + page_size_,
                                page_size_,
                                PAGE_NOACCESS,
                                &old_protect)) {
                VirtualFree(address_, 0, MEM_RELEASE);
                address_ = nullptr;
                size_ = 0;
                page_size_ = 0;
            }
        }
#else
        const long page_size = sysconf(_SC_PAGESIZE);
        page_size_ = page_size > 0 ? static_cast<size_t>(page_size) : 4096u;
        size_ = page_size_ * 2u;
        address_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (address_ == MAP_FAILED) {
            address_ = nullptr;
            size_ = 0;
            page_size_ = 0;
        } else if (mprotect(static_cast<uint8_t*>(address_) + page_size_, page_size_, PROT_NONE) != 0) {
            munmap(address_, size_);
            address_ = nullptr;
            size_ = 0;
            page_size_ = 0;
        }
#endif
    }

    ~ScopedTestGuardedPageMapping() {
#if defined(_WIN32)
        if (address_ != nullptr) {
            VirtualFree(address_, 0, MEM_RELEASE);
        }
#else
        if (address_ != nullptr) {
            munmap(address_, size_);
        }
#endif
    }

    void* address() const { return address_; }
    size_t size() const { return size_; }
    size_t page_size() const { return page_size_; }

private:
    void* address_ = nullptr;
    size_t size_ = 0;
    size_t page_size_ = 0;
};

void DriveNativeHookDispatchUntil(std::atomic<bool>& done_flag) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done_flag.load() && std::chrono::steady_clock::now() < deadline) {
        std::string error_message;
        assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void TestNativeAttachBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Nook === 'object' &&"
        "           typeof Nook.Native === 'object' &&"
        "           typeof Nook.Native.attach"
        "});";
    assert(registry.CreateScript("native_attach_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJniBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Nook === 'object' &&"
        "           typeof Nook.Jni === 'object' &&"
        "           typeof Nook.Jni.readJStringUtf8"
        "});";
    assert(registry.CreateScript("jni_binding_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJniReadJStringUtf8ReturnsDecodedString() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetLastJniEnvPointer() = 0u;
    GetLastJniStringPointer() = 0u;
    JsRuntimeSetReadJStringUtf8ForTesting(&FakeReadJStringUtf8);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: Nook.Jni.readJStringUtf8(ptr('0x1111'), ptr('0x2222'))"
        "});";
    assert(registry.CreateScript("jni_read_jstring_utf8.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"jni-password\"}");
    assert(received_data.empty());
    assert(GetLastJniEnvPointer() == 0x1111u);
    assert(GetLastJniStringPointer() == 0x2222u);

    JsRuntimeResetReadJStringUtf8ForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJniReadJStringUtf8RejectsNullArguments() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Nook.Jni.readJStringUtf8(NULL, ptr('0x2222'));"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('non-zero pointer') >= 0 ? 'null' : String(e) });"
        "}";
    assert(registry.CreateScript("jni_read_jstring_utf8_null.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"null\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJniReadJStringUtf8RejectsAsyncRuntimeUsage() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Nook.Jni.readJStringUtf8(ptr('0x1111'), ptr('0x2222'));"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({"
        "    type: 'send',"
        "    payload: String(e).indexOf('unsafe in the current async hook runtime') >= 0 ?"
        "      'guarded' : String(e)"
        "  });"
        "}";
    assert(registry.CreateScript("jni_read_jstring_utf8_guarded.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"guarded\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleAndInterceptorBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Module.findExportByName + ':' +"
        "           typeof Module.attachExport + ':' +"
        "           typeof Interceptor.attach + ':' +"
        "           typeof Interceptor.detach + ':' +"
        "           typeof Interceptor.detachAll"
        "});";
    assert(registry.CreateScript("module_interceptor_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function:function:function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Java + ':' + typeof Java.perform + ':' + typeof Java.use"
        "});";
    assert(registry.CreateScript("java_bindings_exist.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object:function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaPerformInvokesCallbackSynchronously() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.ready = function (fn) { fn(); };"
        "var called = false;"
        "Java.perform(function () { called = true; });"
        "send({ type: 'send', payload: String(called) });";
    assert(registry.CreateScript("java_perform_invokes_callback.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaPerformRejectsNonFunction() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.perform(123);";
    assert(registry.CreateScript("java_perform_rejects_non_function.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.perform requires a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaPerformDelegatesToJavaVmPerformWhenReady() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "var originalVmPerform = Java.vm.perform;"
        "Java.ready = function (fn) {"
        "  seen.push('ready');"
        "  return fn();"
        "};"
        "Java.vm.perform = function (fn) {"
        "  seen.push('vm');"
        "  return originalVmPerform(function () {"
        "    seen.push('callback');"
        "    return fn();"
        "  });"
        "};"
        "Java.perform(function () {"
        "  seen.push('user');"
        "});"
        "seen.push('after');"
        "send({ type: 'send', payload: seen.join('|') });";
    assert(registry.CreateScript("java_perform_delegates_to_java_vm_perform_when_ready.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"ready|vm|callback|user|after\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaUseReturnsClassAndMethodWrappers() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "send({"
        "  type: 'send',"
        "  payload: typeof LoginFragment + ':' +"
        "           typeof LoginFragment.verifyPasswordNative + ':' +"
        "           typeof LoginFragment.verifyPasswordNative.callOriginal"
        "});";
    assert(registry.CreateScript("java_use_returns_wrappers.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object:function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaCastBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "send({ type: 'send', payload: typeof Java.cast });";
    assert(registry.CreateScript("java_cast_binding_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaCastReturnsRewrappedObjectWithNewClassName() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var casted = Java.cast(this, TextFragment);"
        "  send({"
        "    type: 'send',"
        "    payload: casted.$className + ':' +"
        "             String(casted !== this) + ':' +"
        "             String(casted.__nookJavaReceiverHandle === this.__nookJavaReceiverHandle)"
        "  });"
        "};";
    assert(registry.CreateScript("java_cast_rewraps_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaCastWrapperCanInvokeTargetClassMethodDirectly() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var casted = Java.cast(this, TextFragment);"
        "  return casted.formatBalance(10.0);"
        "};";
    assert(registry.CreateScript("java_cast_direct_invoke.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "instance-double:10.00");

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatBalance");
    assert(capture.record.signature == "(D)Ljava/lang/String;");
    assert(!capture.record.is_static);
    assert(capture.receiver_handle == 1u);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 10.0);

    registry.Clear();
    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaCastRejectsNonJavaObject() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "Java.cast({}, TextFragment);";
    assert(registry.CreateScript("java_cast_rejects_non_java_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.cast object must be a Java object wrapper") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaCastRejectsNonClassWrapper() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  return Java.cast(this, this);"
        "};";
    assert(registry.CreateScript("java_cast_rejects_non_class_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(!JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(error_message.find("Java.cast target must be a Java class wrapper") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRetainBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.retain });";
    assert(registry.CreateScript("java_retain_binding_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRetainReturnsRewrappedObjectWithRetainedHandle() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.retain_object = &FakeRetainJavaObject;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var kept = Java.retain(this);"
        "  send({"
        "    type: 'send',"
        "    payload: kept.$className + ':' +"
        "             String(kept !== this) + ':' +"
        "             String(kept.__nookJavaReceiverHandle !== this.__nookJavaReceiverHandle)"
        "  });"
        "};";
    assert(registry.CreateScript("java_retain_rewraps_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:true:true\"}");
    assert(received_data.empty());

    const JavaRetainCapture& capture = GetJavaRetainCapture();
    assert(capture.call_count == 1);
    assert(capture.source_handle == 1u);
    assert(capture.retained_handle == 0x1001u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRetainRejectsNonJavaObject() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.retain({});";
    assert(registry.CreateScript("java_retain_rejects_non_java_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.retain requires a Java object wrapper") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRetainRejectsNullHandleObject() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "Java.retain(TextFragment);";
    assert(registry.CreateScript("java_retain_rejects_null_handle_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.retain object handle is invalid") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaDisposeReleasesOwnedRetainedHandleOnce() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};

    JavaJsHookInstallerDependencies dependencies = MakeJavaRetainReleaseDependencies();
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::vector<std::string> payloads;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        payloads.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var kept = Java.retain(this);"
        "  send({"
        "    type: 'send',"
        "    payload: typeof kept.$dispose + ':' + String(kept.__nookJavaOwnedHandle)"
        "  });"
        "  kept.$dispose();"
        "  kept.$dispose();"
        "  send({"
        "    type: 'send',"
        "    payload: kept.__nookJavaReceiverHandle + ':' + kept.__jptr + ':' +"
        "             String(kept.__nookJavaOwnedHandle)"
        "  });"
        "};";
    assert(registry.CreateScript("java_dispose_releases_owned_retained_handle_once.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));

    assert(payloads.size() == 2u);
    assert(payloads[0] == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(payloads[1] == "{\"type\":\"send\",\"payload\":\"0x0:0x0:false\"}");

    const JavaRetainCapture& retain_capture = GetJavaRetainCapture();
    assert(retain_capture.call_count == 1);
    assert(retain_capture.source_handle == 1u);
    assert(retain_capture.retained_handle == 0x1001u);

    const JavaReleaseCapture& release_capture = GetJavaReleaseCapture();
    assert(release_capture.call_count == 1);
    assert(release_capture.released_handle == 0x1001u);

    registry.Clear();
    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOwnedHandleCleanupOnUnloadReleasesRetainedHandle() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(MakeJavaRetainReleaseDependencies());

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    assert(registry.CreateScript("java_cleanup_unload_releases_retained_handle.js",
                                 GetJavaRetainWithoutDisposeSource(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    InvokeJavaInitViewHookOnce(script_id, &error_message);

    const JavaRetainCapture& retain_capture = GetJavaRetainCapture();
    assert(retain_capture.call_count == 1);
    assert(retain_capture.retained_handle == 0x1001u);
    assert(GetJavaReleaseCapture().call_count == 0);

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetJavaReleaseCapture().call_count == 1);
    assert(GetJavaReleaseCapture().released_handle == 0x1001u);

    registry.Clear();
    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOwnedHandleCleanupOnRegistryClearReleasesRetainedHandle() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(MakeJavaRetainReleaseDependencies());

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    assert(registry.CreateScript("java_cleanup_clear_releases_retained_handle.js",
                                 GetJavaRetainWithoutDisposeSource(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    InvokeJavaInitViewHookOnce(script_id, &error_message);

    assert(GetJavaRetainCapture().call_count == 1);
    assert(GetJavaReleaseCapture().call_count == 0);

    registry.Clear();
    assert(GetJavaReleaseCapture().call_count == 1);
    assert(GetJavaReleaseCapture().released_handle == 0x1001u);

    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOwnedHandleCleanupOnShutdownReleasesRetainedHandle() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(MakeJavaRetainReleaseDependencies());

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    assert(registry.CreateScript("java_cleanup_shutdown_releases_retained_handle.js",
                                 GetJavaRetainWithoutDisposeSource(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    InvokeJavaInitViewHookOnce(script_id, &error_message);

    assert(GetJavaRetainCapture().call_count == 1);
    assert(GetJavaReleaseCapture().call_count == 0);

    JsRuntime::Shutdown();
    assert(GetJavaReleaseCapture().call_count == 1);
    assert(GetJavaReleaseCapture().released_handle == 0x1001u);

    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOwnedHandleCleanupAfterExplicitDisposeDoesNotDoubleRelease() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(MakeJavaRetainReleaseDependencies());

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    assert(registry.CreateScript("java_cleanup_dispose_then_unload_no_double_release.js",
                                 GetJavaRetainWithDisposeSource(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    InvokeJavaInitViewHookOnce(script_id, &error_message);

    assert(GetJavaRetainCapture().call_count == 1);
    assert(GetJavaReleaseCapture().call_count == 1);
    assert(GetJavaReleaseCapture().released_handle == 0x1001u);

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetJavaReleaseCapture().call_count == 1);

    registry.Clear();
    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOwnedHandleCleanupOnGcReleasesRetainedHandle() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(MakeJavaRetainReleaseDependencies());

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    assert(registry.CreateScript("java_cleanup_gc_releases_retained_handle.js",
                                 GetJavaRetainWithoutDisposeSource(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    InvokeJavaInitViewHookOnce(script_id, &error_message);

    assert(GetJavaRetainCapture().call_count == 1);
    assert(GetJavaReleaseCapture().call_count == 0);

    JsRuntimeRunGcForTesting();

    assert(GetJavaReleaseCapture().call_count == 1);
    assert(GetJavaReleaseCapture().released_handle == 0x1001u);

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetJavaReleaseCapture().call_count == 1);

    registry.Clear();
    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryRetainDisposeReleasesRetainedHandle() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(MakeJavaRetainReleaseDependencies());

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var TextFragment = cf.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var kept = cf.retain(this);"
        "  send({"
        "    type: 'send',"
        "    payload: String(kept.__nookJavaOwnedHandle) + ':' + String(typeof kept.$dispose)"
        "  });"
        "  kept.$dispose();"
        "};";
    assert(registry.CreateScript("java_class_factory_retain_dispose_releases_retained_handle.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    InvokeJavaInitViewHookOnce(script_id, &error_message);

    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:function\"}");
    assert(received_data.empty());
    assert(GetJavaRetainCapture().call_count == 1);
    assert(GetJavaReleaseCapture().call_count == 1);
    assert(GetJavaReleaseCapture().released_handle == 0x1001u);

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetJavaReleaseCapture().call_count == 1);

    registry.Clear();
    JsRuntime::Shutdown();
    ResetJavaJsHookRegistryForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestScriptWeakBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Script.bindWeak + ':' + typeof Script.unbindWeak });";
    assert(registry.CreateScript("script_bindweak_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptWeakBindingGcApiExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Script._runGcForTesting });";
    assert(registry.CreateScript("script_bindweak_gc_api_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptWeakBindingFiresOnGc() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "function onWeakGc() { events.push('gc'); }"
        "rpc.exports.arm = function () {"
        "  var target = {};"
        "  Script.bindWeak(target, onWeakGc);"
        "  return true;"
        "};"
        "rpc.exports.getevents = function () { return events.slice(); };";
    assert(registry.CreateScript("script_bindweak_gc.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[]");
    assert(JsRuntime::CallRpc(script_id, "arm", "[]", &result_json, &error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[]");

    JsRuntimeRunGcForTesting();

    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"gc\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptWeakBindingFiresOnScriptGcApi() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "function onWeakGc() { events.push('gc'); }"
        "rpc.exports.arm = function () {"
        "  var target = {};"
        "  Script.bindWeak(target, onWeakGc);"
        "  return true;"
        "};"
        "rpc.exports.gc = function () {"
        "  Script._runGcForTesting();"
        "  return true;"
        "};"
        "rpc.exports.getevents = function () { return events.slice(); };";
    assert(registry.CreateScript("script_bindweak_gc_api.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "arm", "[]", &result_json, &error_message));
    assert(JsRuntime::CallRpc(script_id, "gc", "[]", &result_json, &error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"gc\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptWeakBindingUnbindFiresCallbackImmediately() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "function onWeakGc() { events.push('gc'); }"
        "rpc.exports.arm = function () {"
        "  var target = {};"
        "  var token = Script.bindWeak(target, onWeakGc);"
        "  return Script.unbindWeak(token);"
        "};"
        "rpc.exports.getevents = function () { return events.slice(); };";
    assert(registry.CreateScript("script_bindweak_unbind.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "arm", "[]", &result_json, &error_message));
    assert(result_json == "true");

    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"gc\"]");

    JsRuntimeRunGcForTesting();

    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"gc\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptWeakBindingFiresOnUnload() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::vector<std::string> messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var target = {};"
        "globalThis.keep = target;"
        "Script.bindWeak(target, function () {"
        "  send({ type: 'send', payload: 'weak-unload-fired' });"
        "});";
    assert(registry.CreateScript("script_bindweak_unload.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(messages.empty());

    assert(registry.UnloadScript(script_id, &error_message));
    assert(messages.size() == 1u);
    assert(messages[0] == "{\"type\":\"send\",\"payload\":\"weak-unload-fired\"}");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptPinBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Script.pin + ':' + typeof Script.unpin });";
    assert(registry.CreateScript("script_pin_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptPinPreventsUnloadUntilFullyUnpinned() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "rpc.exports.pin = function () { Script.pin(); return true; };"
        "rpc.exports.unpin = function () { Script.unpin(); return true; };";
    assert(registry.CreateScript("script_pin_prevents_unload.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "pin", "[]", &result_json, &error_message));
    assert(result_json == "true");
    assert(JsRuntime::CallRpc(script_id, "pin", "[]", &result_json, &error_message));
    assert(result_json == "true");

    assert(!registry.UnloadScript(script_id, &error_message));
    assert(error_message.find("script is pinned") != std::string::npos);

    assert(JsRuntime::CallRpc(script_id, "unpin", "[]", &result_json, &error_message));
    assert(result_json == "true");

    assert(!registry.UnloadScript(script_id, &error_message));
    assert(error_message.find("script is pinned") != std::string::npos);

    assert(JsRuntime::CallRpc(script_id, "unpin", "[]", &result_json, &error_message));
    assert(result_json == "true");

    assert(registry.UnloadScript(script_id, &error_message));

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestTimersBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: ["
        "  typeof setImmediate,"
        "  typeof setTimeout,"
        "  typeof clearTimeout,"
        "  typeof setInterval,"
        "  typeof clearInterval"
        "].join(':') });";
    assert(registry.CreateScript("js_timers_bindings_exist.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:function:function:function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestSetImmediateExecutesAsynchronously() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "setImmediate(function () { events.push('inside'); });"
        "events.push('after');"
        "send({ type: 'send', payload: events.join('|') });"
        "rpc.exports.getevents = function () { return events.slice(); };";
    assert(registry.CreateScript("js_set_immediate_async.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"after\"}");
    assert(received_data.empty());

    std::string result_json;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"after\",\"inside\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestPumpPendingTasksExecutesQueuedSetImmediateCallbacks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "setImmediate(function () {"
        "  events.push('inside');"
        "  send({ type: 'send', payload: events.join('|') });"
        "});"
        "events.push('after');";
    assert(registry.CreateScript("pump_pending_tasks_executes_queued_set_immediate_callbacks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(received_json.empty());
    assert(JsRuntime::PumpPendingTasks(&error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"after|inside\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestSetTimeoutExecutesAsynchronously() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "setTimeout(function () { events.push('inside'); }, 0);"
        "events.push('after');"
        "send({ type: 'send', payload: events.join('|') });"
        "rpc.exports.getevents = function () { return events.slice(); };";
    assert(registry.CreateScript("js_set_timeout_async.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"after\"}");
    assert(received_data.empty());

    std::string result_json;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"after\",\"inside\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestClearTimeoutCancelsPendingTimer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "var id = setTimeout(function () { events.push('inside'); }, 0);"
        "clearTimeout(id);"
        "events.push('after');"
        "rpc.exports.getevents = function () { return events.slice(); };";
    assert(registry.CreateScript("js_clear_timeout_cancels.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string result_json;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"after\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestSetIntervalRepeatsUntilCleared() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "var timer = setInterval(function () {"
        "  events.push('tick');"
        "  if (events.length === 2) clearInterval(timer);"
        "}, 1);"
        "rpc.exports.getevents = function () { return events.slice(); };";
    assert(registry.CreateScript("js_set_interval_repeats.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string result_json;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"tick\",\"tick\"]");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"tick\",\"tick\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestTimersAreClearedOnUnload() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::vector<std::string> messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "setTimeout(function () {"
        "  send({ type: 'send', payload: 'timer-fired' });"
        "}, 5);";
    assert(registry.CreateScript("js_timers_unload_cleanup.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(messages.empty());

    assert(registry.UnloadScript(script_id, &error_message));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(messages.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestScriptUnpinRejectsUnderflow() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source = "Script.unpin();";
    assert(registry.CreateScript("script_unpin_underflow.js", source, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Script.unpin called while pin count is zero") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaChooseBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.choose });";
    assert(registry.CreateScript("java_choose_binding_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaChooseRejectsNonStringClassName() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.choose({}, { onMatch() {}, onComplete() {} });";
    assert(registry.CreateScript("java_choose_rejects_non_string_class_name.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.choose class name must be a string") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaChooseRejectsNonObjectCallbacks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.choose('com.demo.target.TextFragment', 1);";
    assert(registry.CreateScript("java_choose_rejects_non_object_callbacks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.choose callbacks must be an object") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaChooseRejectsMissingOnMatch() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.choose('com.demo.target.TextFragment', { onComplete() {} });";
    assert(registry.CreateScript("java_choose_rejects_missing_on_match.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.choose onMatch must be a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaChooseRejectsMissingOnComplete() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.choose('com.demo.target.TextFragment', { onMatch() {} });";
    assert(registry.CreateScript("java_choose_rejects_missing_on_complete.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.choose onComplete must be a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaChooseDispatchesMatchesAndComplete() {
    GetJavaChooseCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_objects = &FakeEnumerateJavaObjects;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Java.choose('com.demo.target.TextFragment', {"
        "  onMatch(instance) {"
        "    seen.push(instance.$className + ':' + String(instance.formatBalance(10.0)));"
        "  },"
        "  onComplete() {"
        "    send({ type: 'send', payload: seen.join('|') + ':complete' });"
        "  }"
        "});";
    assert(registry.CreateScript("java_choose_dispatches_matches_and_complete.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:instance-double:10.00|com.demo.target.TextFragment:instance-double:10.00:complete\"}");
    assert(received_data.empty());

    const JavaChooseCapture& capture = GetJavaChooseCapture();
    assert(capture.call_count == 1);
    assert(capture.class_name == "com.demo.target.TextFragment");
    assert(capture.handles.size() == 2u);
    assert(capture.handles[0] == 0x1234u);
    assert(capture.handles[1] == 0x2345u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 2);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaEnumerateLoadedClassesBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.enumerateLoadedClasses });";
    assert(registry.CreateScript("java_enumerate_loaded_classes_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateLoadedClassesRejectsNonObjectCallbacks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.enumerateLoadedClasses(1);";
    assert(registry.CreateScript("java_enumerate_loaded_classes_rejects_non_object_callbacks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.enumerateLoadedClasses callbacks must be an object") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateLoadedClassesRejectsMissingOnMatch() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.enumerateLoadedClasses({ onComplete() {} });";
    assert(registry.CreateScript("java_enumerate_loaded_classes_rejects_missing_on_match.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.enumerateLoadedClasses onMatch must be a function") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateLoadedClassesRejectsMissingOnComplete() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.enumerateLoadedClasses({ onMatch() {} });";
    assert(registry.CreateScript("java_enumerate_loaded_classes_rejects_missing_on_complete.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.enumerateLoadedClasses onComplete must be a function") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateLoadedClassesDispatchesDeduplicatedMatchesAndComplete() {
    GetJavaEnumerateLoadedClassesCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_loaded_classes = &FakeEnumerateLoadedJavaClasses;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Java.enumerateLoadedClasses({"
        "  onMatch(name) {"
        "    seen.push(name);"
        "  },"
        "  onComplete() {"
        "    send({ type: 'send', payload: seen.join('|') + ':complete' });"
        "  }"
        "});";
    assert(registry.CreateScript("java_enumerate_loaded_classes_dispatches_deduplicated_matches.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.LoginFragment|com.demo.target.TextFragment:complete\"}");
    assert(received_data.empty());

    const JavaEnumerateLoadedClassesCapture& capture = GetJavaEnumerateLoadedClassesCapture();
    assert(capture.call_count == 1);
    assert(capture.class_names.size() == 3u);
    assert(capture.class_names[0] == "com.demo.target.LoginFragment");
    assert(capture.class_names[1] == "com.demo.target.TextFragment");
    assert(capture.class_names[2] == "com.demo.target.TextFragment");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaEnumerateClassLoadersBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.enumerateClassLoaders });";
    assert(registry.CreateScript("java_enumerate_class_loaders_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateClassLoadersRejectsNonObjectCallbacks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.enumerateClassLoaders(1);";
    assert(registry.CreateScript("java_enumerate_class_loaders_rejects_non_object_callbacks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.enumerateClassLoaders callbacks must be an object") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateClassLoadersRejectsMissingOnMatch() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.enumerateClassLoaders({ onComplete() {} });";
    assert(registry.CreateScript("java_enumerate_class_loaders_rejects_missing_on_match.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.enumerateClassLoaders onMatch must be a function") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateClassLoadersRejectsMissingOnComplete() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.enumerateClassLoaders({ onMatch() {} });";
    assert(registry.CreateScript("java_enumerate_class_loaders_rejects_missing_on_complete.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.enumerateClassLoaders onComplete must be a function") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaEnumerateClassLoadersDispatchesDeduplicatedMatchesAndComplete() {
    GetJavaEnumerateClassLoadersCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_class_loaders = &FakeEnumerateJavaClassLoaders;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Java.enumerateClassLoaders({"
        "  onMatch(loader) {"
        "    seen.push(loader.$className + '@' + loader.__jptr);"
        "  },"
        "  onComplete() {"
        "    send({ type: 'send', payload: seen.join('|') + ':complete' });"
        "  }"
        "});";
    assert(registry.CreateScript("java_enumerate_class_loaders_dispatches_deduplicated_matches.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"dalvik.system.PathClassLoader@0x1111|java.lang.BootClassLoader@0x2222:complete\"}");
    assert(received_data.empty());

    const JavaEnumerateClassLoadersCapture& capture = GetJavaEnumerateClassLoadersCapture();
    assert(capture.call_count == 1);
    assert(capture.loaders.size() == 3u);
    assert(capture.loaders[0].object_handle == 0x1111u);
    assert(capture.loaders[1].object_handle == 0x1111u);
    assert(capture.loaders[2].object_handle == 0x2222u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaEnumerateClassLoadersWorksInsideReadyImmediate() {
    GetJavaEnumerateClassLoadersCapture() = {};
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_class_loaders = &FakeEnumerateJavaClassLoaders;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java._isAppReady = function () { return true; };"
        "Java.ready(function () {"
        "  var seen = [];"
        "  Java.enumerateClassLoaders({"
        "    onMatch: function (loader) {"
        "      seen.push(loader.$className + '@' + loader.__jptr);"
        "    },"
        "    onComplete: function () {"
        "      send({ type: 'send', payload: typeof Java.enumerateClassLoaders + ':' + seen.join('|') });"
        "    }"
        "  });"
        "});";
    assert(registry.CreateScript("java_enumerate_class_loaders_works_inside_ready_immediate.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:dalvik.system.PathClassLoader@0x1111|java.lang.BootClassLoader@0x2222\"}");
    assert(received_data.empty());

    const JavaEnumerateClassLoadersCapture& capture = GetJavaEnumerateClassLoadersCapture();
    assert(capture.call_count == 1);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaReadyDeferredCallbackPreservesScriptContextForImplementationInstall() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "globalThis.__nookReadyFlag = false;"
        "Java._isClassLoaderReady = function () {"
        "  return globalThis.__nookReadyFlag;"
        "};"
        "Java._isLifecycleReady = function () {"
        "  return false;"
        "};"
        "Java.ready(function () {"
        "  var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "  LoginFragment.verifyPasswordNative.implementation = function (password) {"
        "    return password;"
        "  };"
        "  send({ type: 'send', payload: 'implementation-installed-from-ready' });"
        "});";
    assert(registry.CreateScript(
        "java_ready_deferred_callback_preserves_script_context_for_implementation_install.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.empty());

    assert(JsRuntime::Evaluate("globalThis.__nookReadyFlag = true;",
                               "java_ready_deferred_callback_ready_flip.js",
                               0,
                               &error_message));
    assert(JsRuntime::DispatchJavaReadyCallbacks(&error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"implementation-installed-from-ready\"}");
    assert(received_data.empty());

    const JavaHookInstallCallCapture& capture = GetJavaHookInstallCallCapture();
    assert(capture.call_count == 1);
    assert(capture.request.class_name == "com.demo.target.LoginFragment");
    assert(capture.request.method_name == "verifyPasswordNative");
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRegisterClassBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.registerClass });";
    assert(registry.CreateScript("java_register_class_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRegisterClassReturnsClassLikeObjectWithNew() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyClickListener',"
        "  implements: [OnClickListener],"
        "  methods: {"
        "    onClick: function (view) {"
        "      return undefined;"
        "    }"
        "  }"
        "});"
        "send({ type: 'send', payload: typeof Klass.$new });";
    assert(registry.CreateScript("java_register_class_returns_class_like_object_with_new.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRegisterClassNewForwardsInterfacesAndMethodsToBridge() {
    GetJavaRegisterClassCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.register_class = &FakeRegisterJavaClass;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyClickListener',"
        "  implements: [OnClickListener],"
        "  methods: {"
        "    onClick: function (view) {"
        "      return undefined;"
        "    }"
        "  }"
        "});"
        "var instance = Klass.$new();"
        "send({ type: 'send', payload: instance.$className + ':' + instance.__jptr });";
    assert(registry.CreateScript("java_register_class_new_forwards_interfaces_and_methods_to_bridge.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"java.lang.reflect.Proxy:0x7777\"}");
    assert(received_data.empty());

    const JavaRegisterClassCapture& capture = GetJavaRegisterClassCapture();
    assert(capture.call_count == 1);
    assert(capture.request.class_name == "com.nook.ProxyClickListener");
    assert(capture.request.interface_class_names.size() == 1u);
    assert(capture.request.interface_class_names[0] == "android.view.View$OnClickListener");
    assert(capture.request.methods.size() == 1u);
    assert(capture.request.methods[0].name == "onClick");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRegisterClassCallbackDispatchesByMethodName() {
    GetJavaRegisterClassCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.register_class = &FakeRegisterJavaClass;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyClickListener',"
        "  implements: [OnClickListener],"
        "  methods: {"
        "    onClick: function (view) {"
        "      return 'clicked:' + view.$className;"
        "    }"
        "  }"
        "});"
        "Klass.$new();";
    assert(registry.CreateScript("java_register_class_callback_dispatches_by_method_name.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    arg.object_handle_is_global = true;

    JavaJsValue result = {};
    const JavaRegisterClassCapture& capture = GetJavaRegisterClassCapture();
    assert(JsRuntimeInvokeJavaRegisteredClassCallbackForTesting(
        script_id,
        capture.request.callback_id,
        0x7777u,
        "java.lang.reflect.Proxy",
        "onClick",
        nullptr,
        &arg,
        1u,
        &result,
        &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "clicked:android.view.View");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRegisterClassAcceptsMethodDeclarationObject() {
    GetJavaRegisterClassCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.register_class = &FakeRegisterJavaClass;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyClickListenerObjectDecl',"
        "  implements: [OnClickListener],"
        "  methods: {"
        "    onClick: {"
        "      returnType: 'void',"
        "      argumentTypes: ['android.view.View'],"
        "      implementation: function (view) {"
        "        return undefined;"
        "      }"
        "    }"
        "  }"
        "});"
        "var instance = Klass.$new();"
        "send({ type: 'send', payload: instance.$className + ':' + instance.__jptr });";
    assert(registry.CreateScript("java_register_class_accepts_method_declaration_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"java.lang.reflect.Proxy:0x7777\"}");
    assert(received_data.empty());

    const JavaRegisterClassCapture& capture = GetJavaRegisterClassCapture();
    assert(capture.call_count == 1);
    assert(capture.request.methods.size() == 1u);
    assert(capture.request.methods[0].name == "onClick");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRegisterClassAcceptsSingleEntryMethodDeclarationArray() {
    GetJavaRegisterClassCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.register_class = &FakeRegisterJavaClass;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyClickListenerArrayDecl',"
        "  implements: [OnClickListener],"
        "  methods: {"
        "    onClick: [{"
        "      returnType: 'void',"
        "      argumentTypes: ['android.view.View'],"
        "      implementation: function (view) {"
        "        return undefined;"
        "      }"
        "    }]"
        "  }"
        "});"
        "var instance = Klass.$new();"
        "send({ type: 'send', payload: instance.$className + ':' + instance.__jptr });";
    assert(registry.CreateScript("java_register_class_accepts_single_entry_method_declaration_array.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"java.lang.reflect.Proxy:0x7777\"}");
    assert(received_data.empty());

    const JavaRegisterClassCapture& capture = GetJavaRegisterClassCapture();
    assert(capture.call_count == 1);
    assert(capture.request.methods.size() == 1u);
    assert(capture.request.methods[0].name == "onClick");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRegisterClassRejectsMethodDeclarationObjectWithoutImplementation() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyClickListenerBadDecl',"
        "  implements: [OnClickListener],"
        "  methods: {"
        "    onClick: { returnType: 'void', argumentTypes: ['android.view.View'] }"
        "  }"
        "});"
        "Klass.$new();";
    assert(registry.CreateScript(
        "java_register_class_rejects_method_declaration_object_without_implementation.js",
        source,
        &script_id,
        &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.registerClass method declarations must provide an implementation function") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRegisterClassRejectsMultipleMethodDeclarations() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyClickListenerBadArrayDecl',"
        "  implements: [OnClickListener],"
        "  methods: {"
        "    onClick: ["
        "      { implementation: function (view) { return undefined; } },"
        "      { implementation: function (view) { return undefined; } }"
        "    ]"
        "  }"
        "});"
        "Klass.$new();";
    assert(registry.CreateScript("java_register_class_rejects_multiple_method_declarations.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.registerClass method declaration arrays require returnType") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRegisterClassAcceptsMultipleMethodDeclarationsWithDistinctSignatures() {
    GetJavaRegisterClassCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.register_class = &FakeRegisterJavaClass;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MarkerInterface = Java.use('com.demo.target.MarkerInterface');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyMultiDecl',"
        "  implements: [MarkerInterface],"
        "  methods: {"
        "    marker: ["
        "      {"
        "        returnType: 'java.lang.String',"
        "        argumentTypes: ['int'],"
        "        implementation: function (value) { return 'int:' + value; }"
        "      },"
        "      {"
        "        returnType: 'java.lang.String',"
        "        argumentTypes: ['java.lang.String'],"
        "        implementation: function (value) { return 'str:' + value; }"
        "      }"
        "    ]"
        "  }"
        "});"
        "var instance = Klass.$new();"
        "send({ type: 'send', payload: instance.$className + ':' + instance.__jptr });";
    assert(registry.CreateScript(
        "java_register_class_accepts_multiple_method_declarations_with_distinct_signatures.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"java.lang.reflect.Proxy:0x7777\"}");
    assert(received_data.empty());

    const JavaRegisterClassCapture& capture = GetJavaRegisterClassCapture();
    assert(capture.call_count == 1);
    assert(capture.request.methods.size() == 2u);
    assert(capture.request.methods[0].name == "marker");
    assert(capture.request.methods[0].signature == "(I)Ljava/lang/String;");
    assert(capture.request.methods[1].name == "marker");
    assert(capture.request.methods[1].signature == "(Ljava/lang/String;)Ljava/lang/String;");

    JavaJsValue int_arg = {};
    int_arg.kind = JavaJsValueKind::kInt32;
    int_arg.int_value = 7;

    JavaJsValue string_arg = {};
    string_arg.kind = JavaJsValueKind::kString;
    string_arg.string_value = "hello";

    JavaJsValue int_result = {};
    assert(JsRuntimeInvokeJavaRegisteredClassCallbackForTesting(
        script_id,
        capture.request.callback_id,
        0x7777u,
        "java.lang.reflect.Proxy",
        "marker",
        "(I)Ljava/lang/String;",
        &int_arg,
        1u,
        &int_result,
        &error_message));
    assert(int_result.kind == JavaJsValueKind::kString);
    assert(int_result.string_value == "int:7");

    JavaJsValue string_result = {};
    assert(JsRuntimeInvokeJavaRegisteredClassCallbackForTesting(
        script_id,
        capture.request.callback_id,
        0x7777u,
        "java.lang.reflect.Proxy",
        "marker",
        "(Ljava/lang/String;)Ljava/lang/String;",
        &string_arg,
        1u,
        &string_result,
        &error_message));
    assert(string_result.kind == JavaJsValueKind::kString);
    assert(string_result.string_value == "str:hello");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaRegisterClassRejectsDuplicateMethodDeclarationSignatures() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MarkerInterface = Java.use('com.demo.target.MarkerInterface');"
        "var Klass = Java.registerClass({"
        "  name: 'com.nook.ProxyDuplicateDecl',"
        "  implements: [MarkerInterface],"
        "  methods: {"
        "    marker: ["
        "      {"
        "        returnType: 'java.lang.String',"
        "        argumentTypes: ['int'],"
        "        implementation: function (value) { return 'a:' + value; }"
        "      },"
        "      {"
        "        returnType: 'java.lang.String',"
        "        argumentTypes: ['int'],"
        "        implementation: function (value) { return 'b:' + value; }"
        "      }"
        "    ]"
        "  }"
        "});"
        "Klass.$new();";
    assert(registry.CreateScript(
        "java_register_class_rejects_duplicate_method_declaration_signatures.js",
        source,
        &script_id,
        &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find(
               "Java.registerClass method declaration signatures must be unique per method name") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRegisterClassRejectsUnsupportedFieldsSpec() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var OnClickListener = Java.use('android.view.View$OnClickListener');"
        "Java.registerClass({"
        "  name: 'com.nook.ProxyFieldsUnsupported',"
        "  implements: [OnClickListener],"
        "  fields: {"
        "    counter: 'int'"
        "  },"
        "  methods: {"
        "    onClick: function (view) {}"
        "  }"
        "});";
    assert(registry.CreateScript(
        "java_register_class_rejects_unsupported_fields_spec.js",
        source,
        &script_id,
        &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.registerClass spec.fields is not supported") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaRegisterClassRejectsUnsupportedSuperClassSpec() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Runnable = Java.use('java.lang.Runnable');"
        "var ObjectClass = Java.use('java.lang.Object');"
        "Java.registerClass({"
        "  name: 'com.nook.ProxySuperUnsupported',"
        "  superClass: ObjectClass,"
        "  implements: [Runnable],"
        "  methods: {"
        "    run: function () {}"
        "  }"
        "});";
    assert(registry.CreateScript(
        "java_register_class_rejects_unsupported_super_class_spec.js",
        source,
        &script_id,
        &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.registerClass spec.superClass is not supported") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaPerformNowBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.performNow });";
    assert(registry.CreateScript("java_perform_now_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaPerformNowRejectsNonFunction() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.performNow(123);";
    assert(registry.CreateScript("java_perform_now_rejects_non_function.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.performNow requires a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaPerformNowExecutesImmediately() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Java.performNow(function () {"
        "  seen.push('inside');"
        "});"
        "seen.push('after');"
        "send({ type: 'send', payload: seen.join('|') });";
    assert(registry.CreateScript("java_perform_now_executes_immediately.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"inside|after\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaPerformNowDelegatesToVmPerform() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "var originalVmPerform = Java.vm.perform;"
        "Java.vm.perform = function (fn) {"
        "  seen.push('vm');"
        "  return originalVmPerform(function () {"
        "    seen.push('inside');"
        "    return fn();"
        "  });"
        "};"
        "Java.performNow(function () {"
        "  seen.push('callback');"
        "});"
        "seen.push('after');"
        "send({ type: 'send', payload: seen.join('|') });";
    assert(registry.CreateScript("java_perform_now_delegates_to_vm_perform.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"vm|inside|callback|after\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaVmPerformBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.vm + ':' + typeof Java.vm.perform });";
    assert(registry.CreateScript("java_vm_perform_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaVmPerformRejectsNonFunction() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.vm.perform(123);";
    assert(registry.CreateScript("java_vm_perform_rejects_non_function.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.vm.perform requires a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaVmPerformExecutesImmediately() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Java.vm.perform(function () {"
        "  seen.push('inside');"
        "});"
        "seen.push('after');"
        "send({ type: 'send', payload: seen.join('|') });";
    assert(registry.CreateScript("java_vm_perform_executes_immediately.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"inside|after\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaVmPerformCanUseJavaBridgeImmediately() {
    GetJavaMainThreadState().current_application_available = true;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.vm.perform(function () {"
        "  var ActivityThread = Java.use('android.app.ActivityThread');"
        "  var app = ActivityThread.currentApplication();"
        "  send({ type: 'send', payload: String(app !== null && app !== undefined) + ':' + app.$className });"
        "});";
    assert(registry.CreateScript("java_vm_perform_can_use_java_bridge_immediately.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:android.app.Application\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaVmGetEnvBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.vm.getEnv });";
    assert(registry.CreateScript("java_vm_getenv_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaVmGetEnvReturnsEnvWrapper() {
    GetFakeJavaEnvPointerForTesting() = 0x1234u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env1 = Java.vm.getEnv();"
        "var env2 = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload:"
        "    typeof env1 + ':' +"
        "    typeof env1.handle + ':' +"
        "    typeof env1.handle.isNull + ':' +"
        "    env1.handle.toString() + ':' +"
        "    env1.toString() + ':' +"
        "    String(env1.handle.equals(env2.handle))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_returns_env_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"object:object:function:0x1234:Env(0x1234):true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWorksInsideVmPerform() {
    GetFakeJavaEnvPointerForTesting() = 0x5678u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.vm.perform(function () {"
        "  var env = Java.vm.getEnv();"
        "  send({"
        "    type: 'send',"
        "    payload: env.toString() + ':' + env.handle.toString() + ':' + String(env.handle.isNull())"
        "  });"
        "});";
    assert(registry.CreateScript("java_vm_getenv_inside_vm_perform.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"Env(0x5678):0x5678:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmTryGetEnvBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.vm.tryGetEnv });";
    assert(registry.CreateScript("java_vm_trygetenv_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaVmTryGetEnvReturnsEnvWrapperWhenAvailable() {
    GetFakeJavaEnvPointerForTesting() = 0x9abcu;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.tryGetEnv();"
        "send({"
        "  type: 'send',"
        "  payload: (env === null) ? 'null' : (typeof env + ':' + typeof env.handle + ':' + env.toString() + ':' + env.handle.toString())"
        "});";
    assert(registry.CreateScript("java_vm_trygetenv_returns_env_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"object:object:Env(0x9abc):0x9abc\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmTryGetEnvReturnsNullWhenUnavailable() {
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kUnavailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.tryGetEnv();"
        "send({ type: 'send', payload: env === null ? 'null' : env.toString() });";
    assert(registry.CreateScript("java_vm_trygetenv_returns_null.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"null\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmTryGetEnvWorksInsideVmPerform() {
    GetFakeJavaEnvPointerForTesting() = 0xdef0u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvRequiresAttachForTesting() = false;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.vm.perform(function () {"
        "  var env = Java.vm.tryGetEnv();"
        "  send({"
        "    type: 'send',"
        "    payload: env === null ? 'null' : (env.toString() + ':' + env.handle.toString())"
        "  });"
        "});";
    assert(registry.CreateScript("java_vm_trygetenv_inside_vm_perform.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"Env(0xdef0):0xdef0\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmPerformMakesTryGetEnvAvailableInsideCallback() {
    GetFakeJavaEnvPointerForTesting() = 0x2468u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvRequiresAttachForTesting() = true;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var direct = Java.vm.tryGetEnv();"
        "Java.vm.perform(function () {"
        "  var inside = Java.vm.tryGetEnv();"
        "  send({"
        "    type: 'send',"
        "    payload: String(direct === null) + ':' + (inside === null ? 'null' : inside.toString())"
        "  });"
        "});";
    assert(registry.CreateScript("java_vm_perform_makes_trygetenv_available.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:Env(0x2468)\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
    GetFakeJavaEnvRequiresAttachForTesting() = false;
}

void TestJavaVmGetEnvWrapperExceptionCheckReturnsBoolean() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvExceptionCheckResultForTesting() = true;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvExceptionCheckForTesting(&FakeJavaEnvExceptionCheckForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.exceptionCheck + ':' + String(env.exceptionCheck())"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_exception_check.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvExceptionCheckForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperExceptionOccurredReturnsPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvExceptionOccurredResultForTesting() = 0x2468u;
    GetLastJavaEnvExceptionOccurredPointerForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvExceptionOccurredForTesting(&FakeJavaEnvExceptionOccurredForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var pending = env.exceptionOccurred();"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.exceptionOccurred + ':' + pending.toString()"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_exception_occurred.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:0x2468\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvExceptionOccurredPointerForTesting() == 0x1357u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvExceptionOccurredForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperExceptionClearReturnsTrue() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetLastJavaEnvExceptionClearPointerForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvExceptionClearForTesting(&FakeJavaEnvExceptionClearForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.exceptionClear + ':' + String(env.exceptionClear())"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_exception_clear.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvExceptionClearPointerForTesting() == 0x1357u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvExceptionClearForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmEnvMethodsRequeryLiveEnvPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1111u;
    GetFakeJavaEnvSecondPointerForTesting() = 0x2222u;
    GetFakeJavaEnvQueryCallCountForTesting() = 0u;
    GetFakeJavaEnvUseSequenceForTesting() = true;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvExceptionCheckResultForTesting() = false;
    GetLastJavaEnvExceptionCheckPointerForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvExceptionCheckForTesting(&FakeJavaEnvExceptionCheckForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload: env.toString() + ':' + String(env.exceptionCheck())"
        "});";
    assert(registry.CreateScript("java_vm_env_methods_requery_live_pointer.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"Env(0x1111):false\"}");
    assert(received_data.empty());
    assert(GetFakeJavaEnvQueryCallCountForTesting() >= 2u);
    assert(GetLastJavaEnvExceptionCheckPointerForTesting() == 0x2222u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvExceptionCheckForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
    GetFakeJavaEnvUseSequenceForTesting() = false;
    GetFakeJavaEnvSecondPointerForTesting() = 0u;
    GetFakeJavaEnvQueryCallCountForTesting() = 0u;
}

void TestJavaVmGetEnvWrapperGetObjectClassReturnsPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvGetObjectClassResultForTesting() = 0x9876u;
    GetLastJavaEnvGetObjectClassPointerForTesting() = 0u;
    GetLastJavaEnvGetObjectClassObjectHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvGetObjectClassForTesting(&FakeJavaEnvGetObjectClassForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "var clazz = env.getObjectClass(obj);"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.getObjectClass + ':' + clazz.toString()"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_get_object_class.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:0x9876\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvGetObjectClassPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvGetObjectClassObjectHandleForTesting() == 0x4444u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvGetObjectClassForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperIsSameObjectReturnsBoolean() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvIsSameObjectResultForTesting() = true;
    GetLastJavaEnvIsSameObjectPointerForTesting() = 0u;
    GetLastJavaEnvIsSameObjectLeftHandleForTesting() = 0u;
    GetLastJavaEnvIsSameObjectRightHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvIsSameObjectForTesting(&FakeJavaEnvIsSameObjectForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var left = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "var right = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x5555',"
        "  __nookJavaReceiverHandle: '0x5555'"
        "};"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.isSameObject + ':' + String(env.isSameObject(left, right))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_is_same_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvIsSameObjectPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvIsSameObjectLeftHandleForTesting() == 0x4444u);
    assert(GetLastJavaEnvIsSameObjectRightHandleForTesting() == 0x5555u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvIsSameObjectForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperFindClassReturnsPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvFindClassResultForTesting() = 0x4321u;
    GetFakeJavaEnvFindClassNameForTesting().clear();
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvFindClassForTesting(&FakeJavaEnvFindClassForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var clazz = env.findClass('java/lang/String');"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.findClass + ':' + clazz.toString()"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_find_class.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:0x4321\"}");
    assert(received_data.empty());
    assert(GetFakeJavaEnvFindClassNameForTesting() == "java/lang/String");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvFindClassForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperIsInstanceOfReturnsBoolean() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvIsInstanceOfResultForTesting() = true;
    GetLastJavaEnvIsInstanceOfPointerForTesting() = 0u;
    GetLastJavaEnvIsInstanceOfObjectHandleForTesting() = 0u;
    GetLastJavaEnvIsInstanceOfClassNameForTesting().clear();
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvIsInstanceOfForTesting(&FakeJavaEnvIsInstanceOfForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "var klass = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x0',"
        "  __nookJavaReceiverHandle: '0x0'"
        "};"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.isInstanceOf + ':' + String(env.isInstanceOf(obj, klass))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_is_instance_of.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvIsInstanceOfPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvIsInstanceOfObjectHandleForTesting() == 0x4444u);
    assert(GetLastJavaEnvIsInstanceOfClassNameForTesting() == "com.demo.target.TextFragment");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvIsInstanceOfForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperNewStringUtfReturnsPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvNewStringUtfResultForTesting() = 0x6789u;
    GetLastJavaEnvNewStringUtfPointerForTesting() = 0u;
    GetLastJavaEnvNewStringUtfTextForTesting().clear();
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvNewStringUtfForTesting(&FakeJavaEnvNewStringUtfForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var jstr = env.newStringUtf('hello');"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.newStringUtf + ':' + jstr.toString()"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_new_string_utf.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:0x6789\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvNewStringUtfPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvNewStringUtfTextForTesting() == "hello");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvNewStringUtfForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetStringUtfCharsReturnsPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvGetStringUtfCharsResultForTesting() = 0x8888u;
    GetLastJavaEnvGetStringUtfCharsPointerForTesting() = 0u;
    GetLastJavaEnvGetStringUtfCharsStringHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvGetStringUtfCharsForTesting(&FakeJavaEnvGetStringUtfCharsForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var cstr = env.getStringUtfChars(ptr('0x6789'));"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.getStringUtfChars + ':' + cstr.toString()"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_get_string_utf_chars.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:0x8888\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvGetStringUtfCharsPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvGetStringUtfCharsStringHandleForTesting() == 0x6789u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvGetStringUtfCharsForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperReleaseStringUtfCharsReturnsTrue() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetLastJavaEnvReleaseStringUtfCharsPointerForTesting() = 0u;
    GetLastJavaEnvReleaseStringUtfCharsStringHandleForTesting() = 0u;
    GetLastJavaEnvReleaseStringUtfCharsCharsHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvReleaseStringUtfCharsForTesting(
        &FakeJavaEnvReleaseStringUtfCharsForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.releaseStringUtfChars + ':' + "
        "           String(env.releaseStringUtfChars(ptr('0x6789'), ptr('0x8888')))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_release_string_utf_chars.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvReleaseStringUtfCharsPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvReleaseStringUtfCharsStringHandleForTesting() == 0x6789u);
    assert(GetLastJavaEnvReleaseStringUtfCharsCharsHandleForTesting() == 0x8888u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvReleaseStringUtfCharsForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperNewGlobalRefReturnsPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvNewGlobalRefResultForTesting() = 0x9999u;
    GetLastJavaEnvNewGlobalRefPointerForTesting() = 0u;
    GetLastJavaEnvNewGlobalRefObjectHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvNewGlobalRefForTesting(&FakeJavaEnvNewGlobalRefForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x6789',"
        "  __nookJavaReceiverHandle: '0x6789'"
        "};"
        "var ref = env.newGlobalRef(obj);"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.newGlobalRef + ':' + ref.toString()"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_new_global_ref.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:0x9999\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvNewGlobalRefPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvNewGlobalRefObjectHandleForTesting() == 0x6789u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvNewGlobalRefForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperDeleteGlobalRefReturnsTrue() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetLastJavaEnvDeleteGlobalRefPointerForTesting() = 0u;
    GetLastJavaEnvDeleteGlobalRefHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvDeleteGlobalRefForTesting(&FakeJavaEnvDeleteGlobalRefForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.deleteGlobalRef + ':' + "
        "           String(env.deleteGlobalRef(ptr('0x9999')))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_delete_global_ref.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvDeleteGlobalRefPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvDeleteGlobalRefHandleForTesting() == 0x9999u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvDeleteGlobalRefForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperNewWeakGlobalRefReturnsPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvNewWeakGlobalRefResultForTesting() = 0xaaa1u;
    GetLastJavaEnvNewWeakGlobalRefPointerForTesting() = 0u;
    GetLastJavaEnvNewWeakGlobalRefObjectHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvNewWeakGlobalRefForTesting(&FakeJavaEnvNewWeakGlobalRefForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x6789',"
        "  __nookJavaReceiverHandle: '0x6789'"
        "};"
        "var ref = env.newWeakGlobalRef(obj);"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.newWeakGlobalRef + ':' + ref.toString()"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_new_weak_global_ref.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:0xaaa1\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvNewWeakGlobalRefPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvNewWeakGlobalRefObjectHandleForTesting() == 0x6789u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvNewWeakGlobalRefForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperDeleteWeakGlobalRefReturnsTrue() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetLastJavaEnvDeleteWeakGlobalRefPointerForTesting() = 0u;
    GetLastJavaEnvDeleteWeakGlobalRefHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvDeleteWeakGlobalRefForTesting(&FakeJavaEnvDeleteWeakGlobalRefForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.deleteWeakGlobalRef + ':' + "
        "           String(env.deleteWeakGlobalRef(ptr('0xaaa1')))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_delete_weak_global_ref.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvDeleteWeakGlobalRefPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvDeleteWeakGlobalRefHandleForTesting() == 0xaaa1u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvDeleteWeakGlobalRefForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperDoesNotExposeMonitorEnterOrExit() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.monitorEnter + ':' + typeof env.monitorExit"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_monitor_absent.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"undefined:undefined\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetSuperclassReturnsClassWrapper() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvGetSuperclassHasResultForTesting() = true;
    GetFakeJavaEnvGetSuperclassResultNameForTesting() = "java.lang.Object";
    GetLastJavaEnvGetSuperclassPointerForTesting() = 0u;
    GetLastJavaEnvGetSuperclassClassNameForTesting().clear();
    GetLastJavaEnvGetSuperclassLoaderHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvGetSuperclassForTesting(&FakeJavaEnvGetSuperclassForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var klass = {"
        "  $className: 'java.lang.String',"
        "  __jptr: '0x0',"
        "  __nookJavaReceiverHandle: '0x0',"
        "  __nookJavaLoaderHandle: '0x3333'"
        "};"
        "var superKlass = env.getSuperclass(klass);"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.getSuperclass + ':' + "
        "           superKlass.$className + ':' + "
        "           String(superKlass.__nookJavaReceiverHandle) + ':' + "
        "           String(superKlass.__nookJavaLoaderHandle)"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_get_superclass.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:java.lang.Object:0x0:0x3333\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvGetSuperclassPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvGetSuperclassClassNameForTesting() == "java.lang.String");
    assert(GetLastJavaEnvGetSuperclassLoaderHandleForTesting() == 0x3333u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvGetSuperclassForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetSuperclassReturnsNullWithoutParent() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvGetSuperclassHasResultForTesting() = false;
    GetFakeJavaEnvGetSuperclassResultNameForTesting().clear();
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvGetSuperclassForTesting(&FakeJavaEnvGetSuperclassForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var klass = {"
        "  $className: 'java.lang.Object',"
        "  __jptr: '0x0',"
        "  __nookJavaReceiverHandle: '0x0'"
        "};"
        "var superKlass = env.getSuperclass(klass);"
        "send({ type: 'send', payload: String(superKlass) });";
    assert(registry.CreateScript("java_vm_getenv_wrapper_get_superclass_null.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"null\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvGetSuperclassForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperIsAssignableFromReturnsBoolean() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvIsAssignableFromResultForTesting() = true;
    GetLastJavaEnvIsAssignableFromPointerForTesting() = 0u;
    GetLastJavaEnvIsAssignableFromTargetClassNameForTesting().clear();
    GetLastJavaEnvIsAssignableFromTargetLoaderHandleForTesting() = 0u;
    GetLastJavaEnvIsAssignableFromSourceClassNameForTesting().clear();
    GetLastJavaEnvIsAssignableFromSourceLoaderHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvIsAssignableFromForTesting(&FakeJavaEnvIsAssignableFromForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var target = {"
        "  $className: 'java.lang.Object',"
        "  __jptr: '0x0',"
        "  __nookJavaReceiverHandle: '0x0',"
        "  __nookJavaLoaderHandle: '0x1111'"
        "};"
        "var sourceClass = {"
        "  $className: 'java.lang.String',"
        "  __jptr: '0x0',"
        "  __nookJavaReceiverHandle: '0x0',"
        "  __nookJavaLoaderHandle: '0x2222'"
        "};"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.isAssignableFrom + ':' + "
        "           String(env.isAssignableFrom(target, sourceClass))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_is_assignable_from.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvIsAssignableFromPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvIsAssignableFromTargetClassNameForTesting() == "java.lang.Object");
    assert(GetLastJavaEnvIsAssignableFromTargetLoaderHandleForTesting() == 0x1111u);
    assert(GetLastJavaEnvIsAssignableFromSourceClassNameForTesting() == "java.lang.String");
    assert(GetLastJavaEnvIsAssignableFromSourceLoaderHandleForTesting() == 0x2222u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvIsAssignableFromForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetObjectRefTypeReturnsGlobal() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvGetObjectRefTypeResultForTesting() = 2u;
    GetLastJavaEnvGetObjectRefTypePointerForTesting() = 0u;
    GetLastJavaEnvGetObjectRefTypeObjectHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvGetObjectRefTypeForTesting(&FakeJavaEnvGetObjectRefTypeForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'java.lang.Object',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "send({"
        "  type: 'send',"
        "  payload: typeof env.getObjectRefType + ':' + String(env.getObjectRefType(obj))"
        "});";
    assert(registry.CreateScript("java_vm_getenv_wrapper_get_object_ref_type_global.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:global\"}");
    assert(received_data.empty());
    assert(GetLastJavaEnvGetObjectRefTypePointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvGetObjectRefTypeObjectHandleForTesting() == 0x4444u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvGetObjectRefTypeForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetObjectRefTypeReturnsInvalid() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvGetObjectRefTypeResultForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvGetObjectRefTypeForTesting(&FakeJavaEnvGetObjectRefTypeForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'java.lang.Object',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "send({ type: 'send', payload: String(env.getObjectRefType(obj)) });";
    assert(registry.CreateScript("java_vm_getenv_wrapper_get_object_ref_type_invalid.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"invalid\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvGetObjectRefTypeForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperIsSameObjectRejectsNonJavaObject() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var right = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x5555',"
        "  __nookJavaReceiverHandle: '0x5555'"
        "};"
        "try {"
        "  env.isSameObject({}, right);"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_is_same_object_rejects_non_java_object.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.isSameObject requires two Java objects\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperIsInstanceOfRejectsNonJavaObject() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var klass = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x0',"
        "  __nookJavaReceiverHandle: '0x0'"
        "};"
        "try {"
        "  env.isInstanceOf({}, klass);"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_is_instance_of_rejects_non_java_object.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.isInstanceOf requires a Java object and class wrapper\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperIsInstanceOfRejectsNonClassWrapper() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'com.demo.target.TextFragment',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "try {"
        "  env.isInstanceOf(obj, obj);"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_is_instance_of_rejects_non_class_wrapper.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.isInstanceOf requires a Java object and class wrapper\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetSuperclassRejectsNonClassWrapper() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var obj = {"
        "  $className: 'java.lang.String',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "try {"
        "  env.getSuperclass(obj);"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_get_superclass_rejects_non_class_wrapper.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.getSuperclass requires a Java class wrapper\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperIsAssignableFromRejectsNonClassWrapper() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "var target = {"
        "  $className: 'java.lang.Object',"
        "  __jptr: '0x0',"
        "  __nookJavaReceiverHandle: '0x0'"
        "};"
        "var obj = {"
        "  $className: 'java.lang.String',"
        "  __jptr: '0x4444',"
        "  __nookJavaReceiverHandle: '0x4444'"
        "};"
        "try {"
        "  env.isAssignableFrom(target, obj);"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_is_assignable_from_rejects_non_class_wrapper.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.isAssignableFrom requires two Java class wrappers\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetObjectRefTypeRejectsNonJavaObject() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.getObjectRefType({});"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_get_object_ref_type_rejects_non_java_object.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.getObjectRefType requires a Java object\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperNewStringUtfRejectsNonString() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.newStringUtf(123);"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_new_string_utf_rejects_non_string.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.newStringUtf requires a string\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetStringUtfCharsRejectsNonPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.getStringUtfChars('x');"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_get_string_utf_chars_rejects_non_pointer.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.getStringUtfChars requires a non-null jstring pointer\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperReleaseStringUtfCharsRejectsNonPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.releaseStringUtfChars(ptr('0x6789'), 'x');"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_release_string_utf_chars_rejects_non_pointer.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.releaseStringUtfChars requires non-null jstring and cstring pointers\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperNewGlobalRefRejectsNonJavaObject() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.newGlobalRef({});"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_new_global_ref_rejects_non_java_object.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.newGlobalRef requires a Java object\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperDeleteGlobalRefRejectsNonPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.deleteGlobalRef('x');"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_delete_global_ref_rejects_non_pointer.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.deleteGlobalRef requires a non-null global reference pointer\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperNewWeakGlobalRefRejectsNonJavaObject() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.newWeakGlobalRef({});"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_new_weak_global_ref_rejects_non_java_object.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.newWeakGlobalRef requires a Java object\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperDeleteWeakGlobalRefRejectsNonPointer() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.deleteWeakGlobalRef('x');"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript(
        "java_vm_getenv_wrapper_delete_weak_global_ref_rejects_non_pointer.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(
        received_json ==
        "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.deleteWeakGlobalRef requires a non-null weak global reference pointer\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperGetObjectClassRejectsNonJavaObject() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.getObjectClass({});"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript("java_vm_getenv_wrapper_get_object_class_rejects_non_java_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.getObjectClass requires a Java object\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaVmGetEnvWrapperFindClassRejectsNonString() {
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var env = Java.vm.getEnv();"
        "try {"
        "  env.findClass(123);"
        "  send({ type: 'send', payload: 'no-error' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e) });"
        "}";
    assert(registry.CreateScript("java_vm_getenv_wrapper_find_class_rejects_non_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"TypeError: Java.Env.findClass requires a string name\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetGetJavaEnvPointerForTesting();
}

void TestJavaMainThreadBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.isMainThread + ':' + typeof Java.scheduleOnMainThread });";
    assert(registry.CreateScript("java_main_thread_bindings_exist.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaScheduleOnMainThreadRejectsNonFunction() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.scheduleOnMainThread(123);";
    assert(registry.CreateScript("java_schedule_on_main_thread_rejects_non_function.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.scheduleOnMainThread requires a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaIsMainThreadComparesLooperHandles() {
    GetJavaMainThreadState().current_looper_handle = 0x5002u;
    GetJavaMainThreadState().main_looper_handle = 0x5000u;
    GetJavaMainThreadState().current_and_main_looper_same_object = false;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: String(Java.isMainThread()) });";
    assert(registry.CreateScript("java_is_main_thread_compares_looper_handles.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaIsMainThreadUsesJavaObjectEqualityInsteadOfReferenceValue() {
    GetJavaMainThreadState().current_looper_handle = 0x5002u;
    GetJavaMainThreadState().main_looper_handle = 0x5000u;
    GetJavaMainThreadState().current_and_main_looper_same_object = true;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: String(Java.isMainThread()) });";
    assert(registry.CreateScript("java_is_main_thread_uses_java_object_equality.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaScheduleOnMainThreadUsesHandlerPostAndRunnable() {
    GetJavaRegisterClassCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};
    GetJavaMainThreadState().current_application_available = true;
    GetJavaMainThreadState().current_looper_handle = 0x5000u;
    GetJavaMainThreadState().main_looper_handle = 0x5000u;
    GetJavaMainThreadState().handler_handle = 0x5001u;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.register_class = &FakeRegisterJavaClass;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.scheduleOnMainThread(function () {});"
        "send({ type: 'send', payload: 'scheduled' });";
    assert(registry.CreateScript("java_schedule_on_main_thread_uses_handler_post_and_runnable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"scheduled\"}");
    assert(received_data.empty());

    const JavaRegisterClassCapture& register_capture = GetJavaRegisterClassCapture();
    assert(register_capture.call_count == 1);
    assert(register_capture.request.interface_class_names.size() == 1u);
    assert(register_capture.request.interface_class_names[0] == "java.lang.Runnable");
    assert(register_capture.request.methods.size() == 1u);
    assert(register_capture.request.methods[0].name == "run");

    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 1);
    assert(constructor_capture.record.class_name == "android.os.Handler");
    assert(constructor_capture.record.method_name == "<init>");
    assert(constructor_capture.record.signature == "(Landroid/os/Looper;)V");
    assert(constructor_capture.args.size() == 1u);
    assert(constructor_capture.args[0].kind == JavaJsValueKind::kObject);
    assert(constructor_capture.args[0].object_class_name == "android.os.Looper");
    assert(constructor_capture.args[0].object_handle == GetJavaMainThreadState().main_looper_handle);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count >= 2);
    assert(invoke_capture.record.class_name == "android.os.Handler");
    assert(invoke_capture.record.method_name == "post");
    assert(invoke_capture.record.signature == "(Ljava/lang/Runnable;)Z");
    assert(invoke_capture.receiver_handle == GetJavaMainThreadState().handler_handle);
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kObject);
    assert(invoke_capture.args[0].object_class_name == "java.lang.reflect.Proxy");
    assert(invoke_capture.args[0].object_handle == 0x7777u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaScheduleOnMainThreadDefersUntilReadyWhenApplicationUnavailable() {
    GetJavaRegisterClassCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};
    GetJavaMainThreadState().current_application_available = false;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.register_class = &FakeRegisterJavaClass;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var readyCalls = 0;"
        "Java.ready = function () {"
        "  readyCalls++;"
        "};"
        "Java.scheduleOnMainThread(function () {});"
        "send({ type: 'send', payload: 'ready:' + String(readyCalls) });";
    assert(registry.CreateScript(
        "java_schedule_on_main_thread_defers_until_ready_when_application_unavailable.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"ready:1\"}");
    assert(received_data.empty());

    const JavaRegisterClassCapture& register_capture = GetJavaRegisterClassCapture();
    assert(register_capture.call_count == 0);
    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
    GetJavaMainThreadState().current_application_available = true;
}

void TestJavaArrayBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.array });";
    assert(registry.CreateScript("java_array_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaArrayOverloadSupportsArrayTypeNames() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var ArrayTarget = Java.use('com.demo.target.ArrayTarget');"
        "var selected = ArrayTarget.sumInts.overload('int[]');"
        "send({ type: 'send', payload: selected.$signature + ':' + String(selected.$isStatic) });";
    assert(registry.CreateScript("java_array_overload_type_names.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"([I)I:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaUseSupportsPrototypeNamedMethodOverload() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var selected = Arrays.toString.overload('int[]');"
        "send({ type: 'send', payload: selected.$signature + ':' + String(selected.$isStatic) });";
    assert(registry.CreateScript("java_use_supports_prototype_named_method_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"([I)Ljava/lang/String;:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersPrimitiveArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var ArrayTarget = Java.use('com.demo.target.ArrayTarget');"
        "var values = Java.array('int', [1, 2, 3]);"
        "send({ type: 'send', payload: String(ArrayTarget.sumInts(values)) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_primitive_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"6:int[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "com.demo.target.ArrayTarget");
    assert(resolve_capture.method_name == "sumInts");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "int[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([I)I");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "int[]");
    assert(invoke_capture.args[0].array_elements.size() == 3u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersStringArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var ArrayTarget = Java.use('com.demo.target.ArrayTarget');"
        "var values = Java.array('java.lang.String', ['a', 'b']);"
        "send({ type: 'send', payload: ArrayTarget.joinStrings(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_string_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"a|b:java.lang.String[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "com.demo.target.ArrayTarget");
    assert(resolve_capture.method_name == "joinStrings");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "java.lang.String[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/String;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "java.lang.String[]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersObjectArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('java.lang.Object', ['a', true, 2.5]);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_object_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[a, true, 2.5]:java.lang.Object[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "java.lang.Object[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "java.lang.Object[]");
    assert(invoke_capture.args[0].array_elements.size() == 3u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersInt2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('int', [1, 2]);"
        "var row2 = Java.array('int', [3, 4]);"
        "var values = Java.array('int[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_int_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[1, 2], [3, 4]]:int[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "int[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "int[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "int[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersBoolean2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('boolean', [true, false]);"
        "var row2 = Java.array('boolean', [false, true]);"
        "var values = Java.array('boolean[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_boolean_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[true, false], [false, true]]:boolean[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "boolean[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "boolean[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "boolean[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersByte2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('byte', [1, 2]);"
        "var row2 = Java.array('byte', [3, 4]);"
        "var values = Java.array('byte[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_byte_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[1, 2], [3, 4]]:byte[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "byte[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "byte[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "byte[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersShort2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('short', [1, 2]);"
        "var row2 = Java.array('short', [3, 4]);"
        "var values = Java.array('short[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_short_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[1, 2], [3, 4]]:short[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "short[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "short[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "short[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersChar2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('char', ['a', 'b']);"
        "var row2 = Java.array('char', ['c']);"
        "var values = Java.array('char[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_char_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[a, b], [c]]:char[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "char[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "char[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "char[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersLong2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('long', [1, 2]);"
        "var row2 = Java.array('long', [3, 4]);"
        "var values = Java.array('long[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_long_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[1, 2], [3, 4]]:long[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "long[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "long[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "long[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersFloat2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('float', [1.25, 2.5]);"
        "var row2 = Java.array('float', [3.75]);"
        "var values = Java.array('float[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_float_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[1.25, 2.5], [3.75]]:float[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "float[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "float[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "float[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersDouble2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('double', [1.25, 2.5]);"
        "var row2 = Java.array('double', [3.75]);"
        "var values = Java.array('double[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_double_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[1.25, 2.5], [3.75]]:double[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "double[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "double[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "double[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersString2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('java.lang.String', ['a', 'b']);"
        "var row2 = Java.array('java.lang.String', ['c']);"
        "var values = Java.array('java.lang.String[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_string_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[a, b], [c]]:java.lang.String[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "java.lang.String[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "java.lang.String[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "java.lang.String[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersObject2dArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('java.lang.Object', ['a', true]);"
        "var row2 = Java.array('java.lang.Object', [2.5, 'b']);"
        "var values = Java.array('java.lang.Object[]', [row1, row2]);"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_object_2d_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"[[a, true], [2.5, b]]:java.lang.Object[][]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "deepToString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "java.lang.Object[][]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "java.lang.Object[][]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_type_name == "java.lang.Object[]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaArrayDefaultInvokeParsesArraySignature() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('int', [1, 2, 3]);"
        "var toStringInts = Arrays.toString.overload('int[]');"
        "toStringInts(values);";
    assert(registry.CreateScript("java_array_default_invoke_parses_array_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("java method invoker is not configured") != std::string::npos);
    assert(error_message.find("unsupported Java argument type in signature") == std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersBooleanArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('boolean', [true, false, true]);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_boolean_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[true, false, true]:boolean[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "boolean[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Z)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "boolean[]");
    assert(invoke_capture.args[0].array_elements.size() == 3u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersByteArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('byte', [1, 2, 3]);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_byte_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[1, 2, 3]:byte[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "byte[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([B)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "byte[]");
    assert(invoke_capture.args[0].array_elements.size() == 3u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersShortArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('short', [1, 2, 3]);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_short_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[1, 2, 3]:short[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "short[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([S)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "short[]");
    assert(invoke_capture.args[0].array_elements.size() == 3u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersCharArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('char', ['a', 'b']);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_char_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[a, b]:char[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "char[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([C)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "char[]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kString);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersLongArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('long', [1, 2, 3]);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_long_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[1, 2, 3]:long[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "long[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([J)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "long[]");
    assert(invoke_capture.args[0].array_elements.size() == 3u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersFloatArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('float', [1.25, 2.5]);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_float_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[1.25, 2.5]:float[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "float[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([F)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "float[]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokeInfersDoubleArrayOverload() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('double', [1.25, 2.5]);"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + values.$className });";
    assert(registry.CreateScript("java_invoke_infers_double_array_overload.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[1.25, 2.5]:double[]\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.class_name == "java.util.Arrays");
    assert(resolve_capture.method_name == "toString");
    assert(resolve_capture.argument_type_names.size() == 1u);
    assert(resolve_capture.argument_type_names[0] == "double[]");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([D)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_type_name == "double[]");
    assert(invoke_capture.args[0].array_elements.size() == 2u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokePreservesPrimitiveArrayMutation() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var ArrayTarget = Java.use('com.demo.target.ArrayTarget');"
        "var values = Java.array('int', [1, 2, 3]);"
        "values[1] = 9;"
        "send({ type: 'send', payload: String(ArrayTarget.sumInts(values)) + ':' + String(values[1]) });";
    assert(registry.CreateScript("java_invoke_preserves_primitive_array_mutation.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"13:9\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([I)I");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements.size() == 3u);
    assert(invoke_capture.args[0].array_elements[1].kind == JavaJsValueKind::kDouble);
    assert(invoke_capture.args[0].array_elements[1].double_value == 9.0);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokePreservesObjectArrayMutation() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var values = Java.array('java.lang.Object', ['a', true, 2.5]);"
        "values[1] = 'b';"
        "send({ type: 'send', payload: Arrays.toString(values) + ':' + String(values[1]) });";
    assert(registry.CreateScript("java_invoke_preserves_object_array_mutation.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[a, b, 2.5]:b\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements.size() == 3u);
    assert(invoke_capture.args[0].array_elements[1].kind == JavaJsValueKind::kString);
    assert(invoke_capture.args[0].array_elements[1].string_value == "b");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInvokePreservesNestedArrayMutation() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Arrays = Java.use('java.util.Arrays');"
        "var row1 = Java.array('int', [1, 2]);"
        "var row2 = Java.array('int', [3, 4]);"
        "var values = Java.array('int[]', [row1, row2]);"
        "row1[0] = 7;"
        "send({ type: 'send', payload: Arrays.deepToString(values) + ':' + String(row1[0]) });";
    assert(registry.CreateScript("java_invoke_preserves_nested_array_mutation.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"[[7, 2], [3, 4]]:7\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.record.signature == "([Ljava/lang/Object;)Ljava/lang/String;");
    assert(invoke_capture.args.size() == 1u);
    assert(invoke_capture.args[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].kind == JavaJsValueKind::kArray);
    assert(invoke_capture.args[0].array_elements[0].array_elements.size() == 2u);
    assert(invoke_capture.args[0].array_elements[0].array_elements[0].kind == JavaJsValueKind::kDouble);
    assert(invoke_capture.args[0].array_elements[0].array_elements[0].double_value == 7.0);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.ClassFactory.get });";
    assert(registry.CreateScript("java_class_factory_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryGetRejectsNonLoaderObject() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.ClassFactory.get({});";
    assert(registry.CreateScript("java_class_factory_get_rejects_non_loader_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.ClassFactory.get requires a Java ClassLoader wrapper") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryGetReturnsFactoryWithUse() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "send({ type: 'send', payload: typeof cf.use });";
    assert(registry.CreateScript("java_class_factory_get_returns_factory_with_use.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryGetReturnsFactoryWithChoose() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "send({ type: 'send', payload: typeof cf.choose });";
    assert(registry.CreateScript("java_class_factory_get_returns_factory_with_choose.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryGetReturnsFactoryWithCast() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "send({ type: 'send', payload: typeof cf.cast });";
    assert(registry.CreateScript("java_class_factory_get_returns_factory_with_cast.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryGetReturnsFactoryWithRetain() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "send({ type: 'send', payload: typeof cf.retain });";
    assert(registry.CreateScript("java_class_factory_get_returns_factory_with_retain.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryGetReturnsFactoryWithNew() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "send({ type: 'send', payload: typeof cf.$new });";
    assert(registry.CreateScript("java_class_factory_get_returns_factory_with_new.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryUseReturnsWrapperWithNew() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var TextFragment = cf.use('com.demo.target.TextFragment');"
        "send({ type: 'send', payload: typeof TextFragment.$new });";
    assert(registry.CreateScript("java_class_factory_use_returns_wrapper_with_new.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryUseForwardsLoaderHandleToMethodResolveAndInvoke() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var MainActivity = cf.use('com.demo.target.MainActivity');"
        "send({ type: 'send', payload: String(MainActivity.incrementIntercept(41)) });";
    assert(registry.CreateScript("java_class_factory_use_forwards_loader_handle_to_method_resolve_and_invoke.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"42\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 1);
    assert(resolve_capture.class_name == "com.demo.target.MainActivity");
    assert(resolve_capture.method_name == "incrementIntercept");
    assert(resolve_capture.loader_handle == 0x1111u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 1);
    assert(invoke_capture.record.class_name == "com.demo.target.MainActivity");
    assert(invoke_capture.record.loader_handle == 0x1111u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryUseNewForwardsLoaderHandleToConstructorResolveAndInvoke() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};
    GetJavaRetainCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.retain_object = &FakeRetainJavaObject;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var TextFragment = cf.use('com.demo.target.TextFragment');"
        "var instance = TextFragment.$new();"
        "send({ type: 'send', payload: instance.$className + ':' + String(instance.formatBalance(10.0)) });";
    assert(registry.CreateScript("java_class_factory_use_new_forwards_loader_handle_to_constructor_resolve_and_invoke.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:instance-double:10.00\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 2);
    assert(resolve_capture.loader_handle == 0x1111u);

    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 1);
    assert(constructor_capture.record.class_name == "com.demo.target.TextFragment");
    assert(constructor_capture.record.method_name == "<init>");
    assert(constructor_capture.record.signature == "()V");
    assert(constructor_capture.record.loader_handle == 0x1111u);
    assert(constructor_capture.created_handle == 0x3456u);

    const JavaRetainCapture& retain_capture = GetJavaRetainCapture();
    assert(retain_capture.call_count == 1);
    assert(retain_capture.source_handle == 0x3456u);
    assert(retain_capture.retained_handle == 0x4456u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 2);
    assert(invoke_capture.record.class_name == "com.demo.target.TextFragment");
    assert(invoke_capture.record.method_name == "formatBalance");
    assert(invoke_capture.record.loader_handle == 0x1111u);
    assert(invoke_capture.receiver_handle == 0x4456u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryNewForwardsLoaderHandleToConstructorResolveAndInvoke() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};
    GetJavaRetainCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.retain_object = &FakeRetainJavaObject;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var instance = cf.$new('com.demo.target.TextFragment');"
        "send({ type: 'send', payload: instance.$className + ':' + String(instance.formatBalance(10.0)) });";
    assert(registry.CreateScript("java_class_factory_new_forwards_loader_handle_to_constructor_resolve_and_invoke.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:instance-double:10.00\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 2);
    assert(resolve_capture.loader_handle == 0x1111u);

    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 1);
    assert(constructor_capture.record.class_name == "com.demo.target.TextFragment");
    assert(constructor_capture.record.method_name == "<init>");
    assert(constructor_capture.record.signature == "()V");
    assert(constructor_capture.record.loader_handle == 0x1111u);
    assert(constructor_capture.created_handle == 0x3456u);

    const JavaRetainCapture& retain_capture = GetJavaRetainCapture();
    assert(retain_capture.call_count == 1);
    assert(retain_capture.source_handle == 0x3456u);
    assert(retain_capture.retained_handle == 0x4456u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 2);
    assert(invoke_capture.record.class_name == "com.demo.target.TextFragment");
    assert(invoke_capture.record.method_name == "formatBalance");
    assert(invoke_capture.record.loader_handle == 0x1111u);
    assert(invoke_capture.receiver_handle == 0x4456u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryCastForwardsLoaderHandleToTargetWrapperAndInvoke() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var TextFragment = cf.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var casted = cf.cast(this, TextFragment);"
        "  send({"
        "    type: 'send',"
        "    payload: casted.$className + ':' +"
        "             String(casted !== this) + ':' +"
        "             String(casted.__nookJavaLoaderHandle === loader.__nookJavaReceiverHandle) + ':' +"
        "             String(casted.formatBalance(10.0))"
        "  });"
        "};";
    assert(registry.CreateScript("java_class_factory_cast_forwards_loader_handle.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:true:true:instance-double:10.00\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 2);
    assert(resolve_capture.class_name == "com.demo.target.TextFragment");
    assert(resolve_capture.loader_handle == 0x1111u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 1);
    assert(invoke_capture.record.class_name == "com.demo.target.TextFragment");
    assert(invoke_capture.record.method_name == "formatBalance");
    assert(invoke_capture.record.loader_handle == 0x1111u);
    assert(invoke_capture.receiver_handle == 1u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryRetainPreservesLoaderHandleAndInvoke() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaRetainCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.retain_object = &FakeRetainJavaObject;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var TextFragment = cf.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var kept = cf.retain(this);"
        "  send({"
        "    type: 'send',"
        "    payload: kept.$className + ':' +"
        "             String(kept !== this) + ':' +"
        "             String(kept.__nookJavaLoaderHandle === loader.__nookJavaReceiverHandle) + ':' +"
        "             String(kept.formatBalance(10.0))"
        "  });"
        "};";
    assert(registry.CreateScript("java_class_factory_retain_preserves_loader_handle.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:true:true:instance-double:10.00\"}");
    assert(received_data.empty());

    const JavaRetainCapture& retain_capture = GetJavaRetainCapture();
    assert(retain_capture.call_count == 1);
    assert(retain_capture.source_handle == 1u);
    assert(retain_capture.retained_handle == 0x1001u);

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 2);
    assert(resolve_capture.class_name == "com.demo.target.TextFragment");
    assert(resolve_capture.loader_handle == 0x1111u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 1);
    assert(invoke_capture.record.class_name == "com.demo.target.TextFragment");
    assert(invoke_capture.record.method_name == "formatBalance");
    assert(invoke_capture.record.loader_handle == 0x1111u);
    assert(invoke_capture.receiver_handle == 0x1001u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaSetClassLoaderBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.setClassLoader });";
    assert(registry.CreateScript("java_set_class_loader_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaSetClassLoaderRejectsNonLoaderObject() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Java.setClassLoader({});";
    assert(registry.CreateScript("java_set_class_loader_rejects_non_loader.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.setClassLoader requires a Java ClassLoader wrapper") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaSetClassLoaderMakesSubsequentUseLoaderAware() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "Java.setClassLoader(loader);"
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "var selected = MainActivity.incrementIntercept.overload();"
        "send({"
        "  type: 'send',"
        "  payload: selected.$signature + ':' + String(selected.$isStatic)"
        "});";
    assert(registry.CreateScript("java_set_class_loader_makes_subsequent_use_loader_aware.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"()V:true\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 1);
    assert(resolve_capture.class_name == "com.demo.target.MainActivity");
    assert(resolve_capture.loader_handle == 0x1111u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaSetClassLoaderDoesNotRetroactivelyModifyExistingWrapper() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "Java.setClassLoader(loader);"
        "var selected = MainActivity.incrementIntercept.overload();"
        "send({"
        "  type: 'send',"
        "  payload: selected.$signature + ':' + String(selected.$isStatic)"
        "});";
    assert(registry.CreateScript("java_set_class_loader_does_not_modify_existing_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"()V:true\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 1);
    assert(resolve_capture.loader_handle == 0u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaSetClassLoaderMakesChooseLoaderAware() {
    GetJavaChooseCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_objects = &FakeEnumerateJavaObjects;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "Java.setClassLoader(loader);"
        "Java.choose('com.demo.target.TextFragment', {"
        "  onMatch: function (instance) {"
        "    send({ type: 'send', payload: instance.$className + ':' + String(instance.__nookJavaLoaderHandle === loader.__nookJavaReceiverHandle) });"
        "  },"
        "  onComplete: function () {}"
        "});";
    assert(registry.CreateScript("java_set_class_loader_makes_choose_loader_aware.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:true\"}");
    assert(received_data.empty());

    const JavaChooseCapture& choose_capture = GetJavaChooseCapture();
    assert(choose_capture.call_count == 1);
    assert(choose_capture.loader_handle == 0x1111u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaSetClassLoaderMakesRetainLoaderAware() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaRetainCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.retain_object = &FakeRetainJavaObject;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "Java.setClassLoader(loader);"
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  var kept = Java.retain(this);"
        "  send({"
        "    type: 'send',"
        "    payload: kept.$className + ':' +"
        "             String(kept.__nookJavaLoaderHandle === loader.__nookJavaReceiverHandle) + ':' +"
        "             String(kept.formatBalance(10.0))"
        "  });"
        "};";
    assert(registry.CreateScript("java_set_class_loader_makes_retain_loader_aware.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:true:instance-double:10.00\"}");
    assert(received_data.empty());

    const JavaRetainCapture& retain_capture = GetJavaRetainCapture();
    assert(retain_capture.call_count == 1);
    assert(retain_capture.source_handle == 1u);

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 2);
    assert(resolve_capture.loader_handle == 0x1111u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 1);
    assert(invoke_capture.record.loader_handle == 0x1111u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOpenClassFileBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Java.openClassFile });";
    assert(registry.CreateScript("java_open_class_file_binding_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaOpenClassFileLoadSetsDefaultLoaderForSubsequentUse() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};
    GetJavaRetainCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.retain_object = &FakeRetainJavaObject;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var dex = Java.openClassFile('/data/app/com.demo.injected/base.apk');"
        "var loader = dex.load();"
        "var Payload = Java.use('com.demo.injected.Payload');"
        "send({"
        "  type: 'send',"
        "  payload: loader.$className + ':' +"
        "           String(Payload.__nookJavaLoaderHandle === loader.__nookJavaReceiverHandle) + ':' +"
        "           String(Payload.marker())"
        "});";
    assert(registry.CreateScript("java_open_class_file_load_sets_default_loader_for_subsequent_use.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"dalvik.system.DexClassLoader:true:injected-marker\"}");
    assert(received_data.empty());

    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 1);
    assert(constructor_capture.record.class_name == "dalvik.system.DexClassLoader");
    assert(constructor_capture.record.method_name == "<init>");
    assert(constructor_capture.record.signature ==
           "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 1);
    assert(resolve_capture.class_name == "com.demo.injected.Payload");
    assert(resolve_capture.loader_handle == 0x4567u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count >= 1);
    assert(invoke_capture.record.class_name == "com.demo.injected.Payload");
    assert(invoke_capture.record.method_name == "marker");
    assert(invoke_capture.record.loader_handle == 0x4567u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryGetReturnsFactoryWithOpenClassFile() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "send({ type: 'send', payload: typeof cf.openClassFile });";
    assert(registry.CreateScript("java_class_factory_get_returns_factory_with_open_class_file.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryOpenClassFileRejectsNonString() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "cf.openClassFile(123);";
    assert(registry.CreateScript("java_class_factory_open_class_file_rejects_non_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java.ClassFactory.openClassFile requires a file path string") !=
           std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaClassFactoryOpenClassFileLoadReturnsScopedLoaderAndFactoryUse() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x9999',"
        "  __nookJavaReceiverHandle: '0x9999'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var dex = cf.openClassFile('/data/app/com.demo.injected/base.apk');"
        "var dexLoader = dex.load();"
        "var scopedFactory = Java.ClassFactory.get(dexLoader);"
        "var Payload = scopedFactory.use('com.demo.injected.Payload');"
        "send({"
        "  type: 'send',"
        "  payload: dexLoader.$className + ':' +"
        "           String(Payload.__nookJavaLoaderHandle === dexLoader.__nookJavaReceiverHandle) + ':' +"
        "           String(Payload.marker())"
        "});";
    assert(registry.CreateScript(
        "java_class_factory_open_class_file_load_returns_scoped_loader_and_factory_use.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"dalvik.system.DexClassLoader:true:injected-marker\"}");
    assert(received_data.empty());

    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 1);
    assert(constructor_capture.record.class_name == "dalvik.system.DexClassLoader");
    assert(constructor_capture.args.size() == 4u);
    assert(constructor_capture.args[3].kind == JavaJsValueKind::kObject);
    assert(constructor_capture.args[3].object_handle == 0x9999u);

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 1);
    assert(resolve_capture.class_name == "com.demo.injected.Payload");
    assert(resolve_capture.loader_handle == 0x4567u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryOpenClassFileLoadDoesNotSetDefaultLoader() {
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x9999',"
        "  __nookJavaReceiverHandle: '0x9999'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "cf.openClassFile('/data/app/com.demo.injected/base.apk').load();"
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "var selected = MainActivity.incrementIntercept.overload();"
        "send({ type: 'send', payload: selected.$signature + ':' + String(selected.$isStatic) });";
    assert(registry.CreateScript(
        "java_class_factory_open_class_file_load_does_not_set_default_loader.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"()V:true\"}");
    assert(received_data.empty());

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 1);
    assert(resolve_capture.class_name == "com.demo.target.MainActivity");
    assert(resolve_capture.loader_handle == 0u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryChooseForwardsLoaderHandleToEnumerationAndMatches() {
    GetJavaChooseCapture() = {};
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_objects = &FakeEnumerateJavaObjects;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var seen = [];"
        "cf.choose('com.demo.target.TextFragment', {"
        "  onMatch(instance) {"
        "    seen.push(instance.$className + ':' + String(instance.formatBalance(10.0)));"
        "  },"
        "  onComplete() {"
        "    send({ type: 'send', payload: seen.join('|') + ':complete' });"
        "  }"
        "});";
    assert(registry.CreateScript("java_class_factory_choose_forwards_loader_handle_to_enumeration_and_matches.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"com.demo.target.TextFragment:instance-double:10.00|com.demo.target.TextFragment:instance-double:10.00:complete\"}");
    assert(received_data.empty());

    const JavaChooseCapture& choose_capture = GetJavaChooseCapture();
    assert(choose_capture.call_count == 1);
    assert(choose_capture.class_name == "com.demo.target.TextFragment");
    assert(choose_capture.loader_handle == 0x1111u);
    assert(choose_capture.handles.size() == 2u);
    assert(choose_capture.handles[0] == 0x1234u);
    assert(choose_capture.handles[1] == 0x2345u);

    const JavaMethodResolveCapture& resolve_capture = GetJavaMethodResolveCapture();
    assert(resolve_capture.call_count >= 1);
    assert(resolve_capture.loader_handle == 0x1111u);

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 2);
    assert(invoke_capture.record.loader_handle == 0x1111u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaClassFactoryUseForwardsLoaderHandleToImplementationInstall() {
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodResolveCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var loader = {"
        "  $className: 'dalvik.system.PathClassLoader',"
        "  __jptr: '0x1111',"
        "  __nookJavaReceiverHandle: '0x1111'"
        "};"
        "var cf = Java.ClassFactory.get(loader);"
        "var LoginFragment = cf.use('com.demo.target.LoginFragment');"
        "LoginFragment.verifyPasswordNative.overload('java.lang.String').implementation = function (text) {"
        "  return true;"
        "};";
    assert(registry.CreateScript("java_class_factory_use_forwards_loader_handle_to_implementation_install.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    const JavaHookInstallCallCapture& capture = GetJavaHookInstallCallCapture();
    assert(capture.call_count == 1);
    assert(capture.request.class_name == "com.demo.target.LoginFragment");
    assert(capture.request.method_name == "verifyPasswordNative");
    assert(capture.request.signature == "(Ljava/lang/String;)Z");
    assert(capture.request.loader_handle == 0x1111u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaUseReturnsStaticFieldWrapper() {
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();

    JavaJsValue initial = {};
    initial.kind = JavaJsValueKind::kInt32;
    initial.int_value = 7;
    JavaJsFieldRecord record = {};
    record.class_name = "com.demo.target.MainActivity";
    record.field_name = "interceptCount";
    record.signature = "I";
    record.is_static = true;
    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(record, 0u)] = initial;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_field = &FakeResolveJavaField;
    dependencies.read_field = &FakeReadJavaField;
    dependencies.write_field = &FakeWriteJavaField;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "send({"
        "  type: 'send',"
        "  payload: typeof MainActivity.interceptCount + ':' +"
        "           MainActivity.interceptCount.$signature + ':' +"
        "           String(MainActivity.interceptCount.$isStatic) + ':' +"
        "           String(MainActivity.interceptCount.value)"
        "});";
    assert(registry.CreateScript("java_use_returns_static_field_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object:I:true:7\"}");
    assert(received_data.empty());
    assert(GetJavaFieldResolveCallCapture().call_count == 1);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticFieldValueCanBeWritten() {
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();

    JavaJsValue initial = {};
    initial.kind = JavaJsValueKind::kInt32;
    initial.int_value = 9;
    JavaJsFieldRecord record = {};
    record.class_name = "com.demo.target.MainActivity";
    record.field_name = "interceptCount";
    record.signature = "I";
    record.is_static = true;
    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(record, 0u)] = initial;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_field = &FakeResolveJavaField;
    dependencies.read_field = &FakeReadJavaField;
    dependencies.write_field = &FakeWriteJavaField;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "MainActivity.interceptCount.value = 42;"
        "send({ type: 'send', payload: String(MainActivity.interceptCount.value) });";
    assert(registry.CreateScript("java_static_field_value_can_be_written.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"42\"}");
    assert(received_data.empty());

    auto found = GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(record, 0u));
    assert(found != GetJavaFieldValueStore().end());
    assert(found->second.kind == JavaJsValueKind::kInt32);
    assert(found->second.int_value == 42);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaInstanceFieldValueCanBeReadAndWrittenInsideCallbackReceiver() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();

    JavaJsValue initial = {};
    initial.kind = JavaJsValueKind::kInt32;
    initial.int_value = 7;
    JavaJsFieldRecord record = {};
    record.class_name = "com.demo.target.AdWallFragment";
    record.field_name = "adCount";
    record.signature = "I";
    record.is_static = false;
    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(record, 1u)] = initial;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.resolve_field = &FakeResolveJavaField;
    dependencies.read_field = &FakeReadJavaField;
    dependencies.write_field = &FakeWriteJavaField;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var AdWallFragment = Java.use('com.demo.target.AdWallFragment');"
        "AdWallFragment.loadAd.overload('java.lang.String', 'java.lang.String').implementation = function (adType, position) {"
        "  var before = this.adCount.value;"
        "  this.adCount.value = before + 1;"
        "  return String(before) + ':' + String(this.adCount.value);"
        "};";
    assert(registry.CreateScript("java_instance_field_value_callback_receiver.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue args[2] = {};
    args[0].kind = JavaJsValueKind::kString;
    args[0].string_value = "rewarded";
    args[1].kind = JavaJsValueKind::kString;
    args[1].string_value = "feed";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, args, 2u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "7:8");

    auto found = GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(record, 1u));
    assert(found != GetJavaFieldValueStore().end());
    assert(found->second.kind == JavaJsValueKind::kInt32);
    assert(found->second.int_value == 8);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaInstanceFieldValueCanBeWrittenOnConstructedObject() {
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();
    GetJavaMethodResolveCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.resolve_field = &FakeResolveJavaField;
    dependencies.read_field = &FakeReadJavaField;
    dependencies.write_field = &FakeWriteJavaField;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Checker = Java.use('com.ad2001.frida0x6.Checker');"
        "var checker = Checker.$new();"
        "checker.num1.value = 1234;"
        "checker.num2.value = 4321;"
        "send({"
        "  type: 'send',"
        "  payload: String(checker.num1.value) + ':' + String(checker.num2.value)"
        "});";
    assert(registry.CreateScript("java_instance_field_value_constructed_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    if (!registry.LoadScript(script_id, &error_message)) {
        std::fprintf(stderr,
                     "java_instance_field_value_constructed_object load failed: %s\n",
                     error_message.c_str());
        assert(false);
    }
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1234:4321\"}");
    assert(received_data.empty());

    JavaJsFieldRecord num1 = {};
    num1.class_name = "com.ad2001.frida0x6.Checker";
    num1.field_name = "num1";
    num1.signature = "I";
    num1.is_static = false;

    JavaJsFieldRecord num2 = {};
    num2.class_name = "com.ad2001.frida0x6.Checker";
    num2.field_name = "num2";
    num2.signature = "I";
    num2.is_static = false;

    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 1);
    assert(constructor_capture.record.class_name == "com.ad2001.frida0x6.Checker");
    assert(constructor_capture.record.method_name == "<init>");
    const uint64_t checker_handle = constructor_capture.created_handle;
    assert(checker_handle != 0u);

    auto found_num1 = GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(num1, checker_handle));
    assert(found_num1 != GetJavaFieldValueStore().end());
    assert(found_num1->second.kind == JavaJsValueKind::kInt32);
    assert(found_num1->second.int_value == 1234);

    auto found_num2 = GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(num2, checker_handle));
    assert(found_num2 != GetJavaFieldValueStore().end());
    assert(found_num2->second.kind == JavaJsValueKind::kInt32);
    assert(found_num2->second.int_value == 4321);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaChooseInstanceFieldValueCanBeReadAndWritten() {
    GetJavaChooseCapture() = {};
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();

    JavaJsValue initial_num1 = {};
    initial_num1.kind = JavaJsValueKind::kInt32;
    initial_num1.int_value = 10;
    JavaJsFieldRecord num1 = {};
    num1.class_name = "com.ad2001.frida0x6.Checker";
    num1.field_name = "num1";
    num1.signature = "I";
    num1.is_static = false;

    JavaJsValue initial_num2 = {};
    initial_num2.kind = JavaJsValueKind::kInt32;
    initial_num2.int_value = 20;
    JavaJsFieldRecord num2 = {};
    num2.class_name = "com.ad2001.frida0x6.Checker";
    num2.field_name = "num2";
    num2.signature = "I";
    num2.is_static = false;

    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(num1, 0x1234u)] = initial_num1;
    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(num2, 0x1234u)] = initial_num2;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_objects = &FakeEnumerateJavaObjects;
    dependencies.resolve_field = &FakeResolveJavaField;
    dependencies.read_field = &FakeReadJavaField;
    dependencies.write_field = &FakeWriteJavaField;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Java.choose('com.ad2001.frida0x6.Checker', {"
        "  onMatch(instance) {"
        "    instance.num1.value = 1234;"
        "    instance.num2.value = 4321;"
        "    seen.push(String(instance.num1.value) + ':' + String(instance.num2.value));"
        "  },"
        "  onComplete() {"
        "    send({ type: 'send', payload: seen.join('|') + ':complete' });"
        "  }"
        "});";
    assert(registry.CreateScript("java_choose_instance_field_value_can_be_read_and_written.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1234:4321|1234:4321:complete\"}");
    assert(received_data.empty());

    auto found_num1 = GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(num1, 0x1234u));
    assert(found_num1 != GetJavaFieldValueStore().end());
    assert(found_num1->second.kind == JavaJsValueKind::kInt32);
    assert(found_num1->second.int_value == 1234);

    auto found_num2 = GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(num2, 0x1234u));
    assert(found_num2 != GetJavaFieldValueStore().end());
    assert(found_num2->second.kind == JavaJsValueKind::kInt32);
    assert(found_num2->second.int_value == 4321);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaChoosePrivateFieldAliasValueCanBeReadAndWritten() {
    GetJavaChooseCapture() = {};
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();

    JavaJsValue initial_private = {};
    initial_private.kind = JavaJsValueKind::kInt32;
    initial_private.int_value = 300;

    JavaJsFieldRecord private_field = {};
    private_field.class_name = "com.zj.wuaipojie.Demo";
    private_field.field_name = "_privateInt";
    private_field.reflected_field_name = "privateInt";
    private_field.signature = "I";
    private_field.is_static = false;
    private_field.uses_declared_field_lookup = true;

    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(private_field, 0x1234u)] = initial_private;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_objects = &FakeEnumerateJavaObjects;
    dependencies.resolve_field = &FakeResolveJavaField;
    dependencies.read_field = &FakeReadJavaField;
    dependencies.write_field = &FakeWriteJavaField;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Java.choose('com.zj.wuaipojie.Demo', {"
        "  onMatch(instance) {"
        "    seen.push(String(instance._privateInt.value));"
        "    instance._privateInt.value = 9999;"
        "    seen.push(String(instance.privateInt.value));"
        "  },"
        "  onComplete() {"
        "    send({ type: 'send', payload: seen.join('|') + ':complete' });"
        "  }"
        "});";
    assert(registry.CreateScript("java_choose_private_field_alias_value_can_be_read_and_written.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"300|9999:complete\"}");
    assert(received_data.empty());

    JavaJsFieldRecord expected = private_field;
    expected.field_name = "privateInt";
    auto found_private_alias =
        GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(private_field, 0x1234u));
    auto found_private_plain =
        GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(expected, 0x1234u));
    assert(found_private_alias != GetJavaFieldValueStore().end() ||
           found_private_plain != GetJavaFieldValueStore().end());
    const JavaJsValue& stored =
        found_private_plain != GetJavaFieldValueStore().end()
            ? found_private_plain->second
            : found_private_alias->second;
    assert(stored.kind == JavaJsValueKind::kInt32);
    assert(stored.int_value == 9999);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaChooseRetainedSameObjectFieldMutationPersists() {
    GetJavaChooseCapture() = {};
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();
    GetJavaRetainCapture() = {};
    GetJavaReleaseCapture() = {};

    JavaJsValue initial_private = {};
    initial_private.kind = JavaJsValueKind::kInt32;
    initial_private.int_value = 300;

    JavaJsFieldRecord private_field = {};
    private_field.class_name = "com.zj.wuaipojie.Demo";
    private_field.field_name = "privateInt";
    private_field.signature = "I";
    private_field.is_static = false;

    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(private_field, 0x1234u)] = initial_private;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.enumerate_objects = &FakeEnumerateJavaObjects;
    dependencies.resolve_field = &FakeResolveJavaField;
    dependencies.read_field = &FakeReadJavaField;
    dependencies.write_field = &FakeWriteJavaField;
    dependencies.retain_object = &FakeRetainJavaObject;
    dependencies.release_object = &FakeReleaseJavaObject;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var kept = null;"
        "Java.choose('com.zj.wuaipojie.Demo', {"
        "  onMatch(instance) {"
        "    if (kept !== null) return;"
        "    kept = Java.retain(instance);"
        "    kept.privateInt.value = 9999;"
        "  },"
        "  onComplete() {"
        "    send({ type: 'send', payload: String(kept.privateInt.value) });"
        "    kept.$dispose();"
        "  }"
        "});";
    assert(registry.CreateScript("java_choose_retained_same_object_field_mutation_persists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"9999\"}");
    assert(received_data.empty());

    auto found_private =
        GetJavaFieldValueStore().find(MakeJavaFieldAccessKey(private_field, 0x1234u));
    assert(found_private != GetJavaFieldValueStore().end());
    assert(found_private->second.kind == JavaJsValueKind::kInt32);
    assert(found_private->second.int_value == 9999);

    const JavaRetainCapture& retain_capture = GetJavaRetainCapture();
    assert(retain_capture.call_count == 1);
    assert(retain_capture.object_handle == 0x1234u);
    assert(retain_capture.retained_handle == 0x2234u);

    const JavaReleaseCapture& release_capture = GetJavaReleaseCapture();
    assert(release_capture.call_count == 1);
    assert(release_capture.object_handle == 0x2234u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaWrapperStringifyUsesClassNameFallback() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Demo = Java.use('com.zj.wuaipojie.Demo');"
        "console.log(Demo);"
        "send({ type: 'send', payload: String(Demo) });";
    assert(registry.CreateScript("java_wrapper_stringify_uses_class_name_fallback.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"<JavaClass com.zj.wuaipojie.Demo>\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaInstanceWrapperStringifyUsesObjectFallback() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Demo = Java.use('com.zj.wuaipojie.Demo');"
        "var methods = Demo.class.getDeclaredMethods();"
        "send({ type: 'send', payload: String(methods[0]) });";
    assert(registry.CreateScript("java_instance_wrapper_stringify_uses_object_fallback.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"<JavaObject java.lang.reflect.Method>\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaMethodAndFieldWrapperStringifyUseFallbackText() {
    GetJavaFieldResolveCallCapture() = {};
    GetJavaFieldValueStore().clear();

    JavaJsValue initial = {};
    initial.kind = JavaJsValueKind::kInt32;
    initial.int_value = 7;
    JavaJsFieldRecord record = {};
    record.class_name = "com.demo.target.MainActivity";
    record.field_name = "interceptCount";
    record.signature = "I";
    record.is_static = true;
    GetJavaFieldValueStore()[MakeJavaFieldAccessKey(record, 0u)] = initial;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_field = &FakeResolveJavaField;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "send({"
        "  type: 'send',"
        "  payload: String(MainActivity.incrementIntercept) + ':' + String(MainActivity.interceptCount)"
        "});";
    assert(registry.CreateScript("java_method_and_field_wrapper_stringify.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"<JavaMethod com.demo.target.MainActivity.incrementIntercept>:<JavaField com.demo.target.MainActivity.interceptCount>\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaReflectionMethodArrayConsoleLogFallsBackToReadableText() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);
    GetFakeJavaEnvPointerForTesting() = 0x1357u;
    GetFakeJavaEnvQueryStatusForTesting() = JsRuntimeJavaEnvQueryStatus::kAvailable;
    GetFakeJavaEnvFindClassResultForTesting() = 0u;
    GetFakeJavaEnvNewGlobalRefResultForTesting() = 0x6110u;
    GetFakeJavaEnvFindClassNameForTesting().clear();
    GetLastJavaEnvNewGlobalRefPointerForTesting() = 0u;
    GetLastJavaEnvNewGlobalRefObjectHandleForTesting() = 0u;
    JsRuntimeSetGetJavaEnvPointerForTesting(&FakeGetJavaEnvPointerForTesting);
    JsRuntimeSetJavaEnvFindClassForTesting(&FakeJavaEnvFindClassForTesting);
    JsRuntimeSetJavaEnvNewGlobalRefForTesting(&FakeJavaEnvNewGlobalRefForTesting);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Demo = Java.use('com.zj.wuaipojie.Demo');"
        "var methods = Demo.class.getDeclaredMethods();"
        "console.log(methods);"
        "send({ type: 'send', payload: String(methods) });";
    assert(registry.CreateScript("java_reflection_method_array_console_log_fallback.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"<JavaArray java.lang.reflect.Method[] length=2>\"}");
    assert(received_data.empty());
    assert(GetFakeJavaEnvFindClassNameForTesting() == "com/zj/wuaipojie/Demo");
    assert(GetLastJavaEnvNewGlobalRefPointerForTesting() == 0x1357u);
    assert(GetLastJavaEnvNewGlobalRefObjectHandleForTesting() == 0x6010u);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaEnvNewGlobalRefForTesting();
    JsRuntimeResetJavaEnvFindClassForTesting();
    JsRuntimeResetGetJavaEnvPointerForTesting();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaReturnArrayForTestingUsesArrayKindAndTypeName() {
    JavaJsValue method0 = {};
    method0.kind = JavaJsValueKind::kObject;
    method0.object_handle = 0x7010u;
    method0.object_class_name = "java.lang.reflect.Method";

    JavaJsValue method1 = {};
    method1.kind = JavaJsValueKind::kObject;
    method1.object_handle = 0x7020u;
    method1.object_class_name = "java.lang.reflect.Method";

    JavaJsValue result = {};
    std::string error_message;
    assert(ConvertJavaReturnArrayForTesting(
        "[Ljava/lang/reflect/Method;", {method0, method1}, &result, &error_message));
    assert(error_message.empty());
    assert(result.kind == JavaJsValueKind::kArray);
    assert(result.array_type_name == "java.lang.reflect.Method[]");
    assert(result.object_handle == 0u);
    assert(result.object_class_name.empty());
    assert(result.array_elements.size() == 2u);
    assert(result.array_elements[0].kind == JavaJsValueKind::kObject);
    assert(result.array_elements[0].object_class_name == "java.lang.reflect.Method");
    assert(result.array_elements[1].kind == JavaJsValueKind::kObject);
    assert(result.array_elements[1].object_class_name == "java.lang.reflect.Method");
}

void TestJavaMethodOverloadReturnsSignatureBoundWrapper() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "var selected = LoginFragment.verifyPasswordNative.overload('java.lang.String');"
        "send({"
        "  type: 'send',"
        "  payload: typeof selected + ':' +"
        "           String(selected !== LoginFragment.verifyPasswordNative) + ':' +"
        "           selected.$signature"
        "});";
    assert(registry.CreateScript("java_method_overload_returns_bound_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true:(Ljava/lang/String;)Z\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaMethodWrapperExposesMinimalOverloadsArray() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Demo = Java.use('com.zj.wuaipojie.Demo');"
        "send({"
        "  type: 'send',"
        "  payload: typeof Demo.a.overloads + ':' +"
        "           String(Array.isArray(Demo.a.overloads)) + ':' +"
        "           String(Demo.a.overloads.length) + ':' +"
        "           Demo.a.overloads[0].$overloadTypeNames.join(',')"
        "});";
    assert(registry.CreateScript("java_method_wrapper_exposes_minimal_overloads_array.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object:true:1:java.lang.String\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestJavaMethodWrapperOverloadsAllowImplementationInstall() {
    GetJavaHookInstallCallCapture() = {};
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    dependencies.install_hook = &FakeInstallJavaHook;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Demo = Java.use('com.zj.wuaipojie.Demo');"
        "Demo.a.overloads[0].implementation = function (str) {"
        "  return this.a.callOriginal(str);"
        "};"
        "send({ type: 'send', payload: 'ok' });";
    assert(registry.CreateScript("java_method_wrapper_overloads_allow_implementation_install.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"ok\"}");
    assert(received_data.empty());

    const JavaHookInstallCallCapture& install_capture = GetJavaHookInstallCallCapture();
    assert(install_capture.call_count == 1);
    assert(install_capture.request.class_name == "com.zj.wuaipojie.Demo");
    assert(install_capture.request.method_name == "a");
    assert(install_capture.request.signature == "(Ljava/lang/String;)Ljava/lang/String;");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaMethodOverloadReturnsPrimitiveDoubleSignatureBoundWrapper() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "var selected = TextFragment.formatBalance.overload('double');"
        "send({"
        "  type: 'send',"
        "  payload: typeof selected + ':' +"
        "           String(selected !== TextFragment.formatBalance) + ':' +"
        "           selected.$signature"
        "});";
    assert(registry.CreateScript("java_method_overload_returns_primitive_double_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true:(D)Ljava/lang/String;\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaMethodOverloadReturnsPrimitiveLongSignatureBoundWrapper() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "var selected = TextFragment.formatScaled.overload('long');"
        "send({"
        "  type: 'send',"
        "  payload: typeof selected + ':' +"
        "           String(selected !== TextFragment.formatScaled) + ':' +"
        "           selected.$signature"
        "});";
    assert(registry.CreateScript("java_method_overload_returns_primitive_long_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true:(J)Ljava/lang/String;\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaMethodOverloadReturnsPrimitiveFloatSignatureBoundWrapper() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "var selected = TextFragment.formatScaled.overload('float');"
        "send({"
        "  type: 'send',"
        "  payload: typeof selected + ':' +"
        "           String(selected !== TextFragment.formatScaled) + ':' +"
        "           selected.$signature"
        "});";
    assert(registry.CreateScript("java_method_overload_returns_primitive_float_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true:(F)Ljava/lang/String;\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticMethodOverloadReturnsExactSignatureBoundWrapper() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "var selected = MainActivity.incrementIntercept.overload('int');"
        "send({"
        "  type: 'send',"
        "  payload: typeof selected + ':' +"
        "           String(selected !== MainActivity.incrementIntercept) + ':' +"
        "           String(selected.$isStatic) + ':' +"
        "           selected.$signature"
        "});";
    assert(registry.CreateScript("java_static_method_overload_returns_exact_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true:true:(I)I\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticVoidMethodOverloadReturnsExactSignatureBoundWrapper() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "var selected = MainActivity.incrementIntercept.overload();"
        "send({"
        "  type: 'send',"
        "  payload: typeof selected + ':' +"
        "           String(selected !== MainActivity.incrementIntercept) + ':' +"
        "           String(selected.$isStatic) + ':' +"
        "           selected.$signature"
        "});";
    assert(registry.CreateScript("java_static_void_method_overload_returns_exact_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:true:true:()V\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaMethodWrappersAreCallableFunctions() {
    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "var selected = MainActivity.incrementIntercept.overload('int');"
        "send({"
        "  type: 'send',"
        "  payload: typeof LoginFragment.verifyPasswordNative + ':' +"
        "           typeof selected + ':' +"
        "           typeof selected.callOriginal"
        "});";
    assert(registry.CreateScript("java_method_wrappers_are_callable_functions.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticMethodWrapperCanInvokeOriginalDirectly() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "var addOne = MainActivity.incrementIntercept.overload('int');"
        "send({ type: 'send', payload: String(addOne(41)) });";
    assert(registry.CreateScript("java_static_method_wrapper_can_invoke_directly.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"42\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.MainActivity");
    assert(capture.record.method_name == "incrementIntercept");
    assert(capture.record.signature == "(I)I");
    assert(capture.record.is_static);
    assert(capture.receiver_handle == 0u);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 41.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticMethodWrapperCanResolveIntOverloadFromPlainNumberDirectly() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "send({ type: 'send', payload: String(MainActivity.incrementIntercept(41)) });";
    assert(registry.CreateScript("java_static_method_wrapper_resolves_int_from_plain_number.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"42\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.MainActivity");
    assert(capture.record.method_name == "incrementIntercept");
    assert(capture.record.signature == "(I)I");
    assert(capture.record.is_static);
    assert(capture.receiver_handle == 0u);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 41.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaInstanceMethodWrapperCanInvokeOriginalDirectlyInsideCallbackReceiver() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  return this.formatBalance(10.5);"
        "};";
    assert(registry.CreateScript("java_instance_method_wrapper_can_invoke_directly.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";

    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "instance-double:10.50");

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatBalance");
    assert(capture.record.signature == "(D)Ljava/lang/String;");
    assert(!capture.record.is_static);
    assert(capture.receiver_handle == 1u);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 10.5);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaInstanceMethodWrapperCanResolveDoubleOverloadFromIntegralNumberDirectlyInsideCallbackReceiver() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  return this.formatBalance(10.0);"
        "};";
    assert(registry.CreateScript(
        "java_instance_method_wrapper_resolves_double_from_integral_number.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";

    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "instance-double:10.00");

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatBalance");
    assert(capture.record.signature == "(D)Ljava/lang/String;");
    assert(!capture.record.is_static);
    assert(capture.receiver_handle == 1u);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 10.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaInstanceMethodWrapperCanResolveLongOverloadFromPlainIntDirectlyInsideCallbackReceiver() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  return this.formatScaled(42);"
        "};";
    assert(registry.CreateScript(
        "java_instance_method_wrapper_resolves_long_from_plain_int.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";

    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "instance-long:42");

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatScaled");
    assert(capture.record.signature == "(J)Ljava/lang/String;");
    assert(!capture.record.is_static);
    assert(capture.receiver_handle == 1u);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 42.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaInstanceMethodWrapperCanResolveFloatOverloadFromPlainDoubleDirectlyInsideCallbackReceiver() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.initView.overload('android.view.View').implementation = function (view) {"
        "  return this.formatScaled(3.5);"
        "};";
    assert(registry.CreateScript(
        "java_instance_method_wrapper_resolves_float_from_plain_double.js",
        source,
        &script_id,
        &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kObject;
    arg.object_handle = 0x1234u;
    arg.object_class_name = "android.view.View";

    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "instance-float:3.50");

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatScaled");
    assert(capture.record.signature == "(F)Ljava/lang/String;");
    assert(!capture.record.is_static);
    assert(capture.receiver_handle == 1u);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 3.5);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

struct FakeJavaAssignabilityGraph {
    std::unordered_map<std::string, std::vector<std::string>> sources_by_target;
};

void AddAssignableRelation(FakeJavaAssignabilityGraph* graph,
                           const std::string& target_descriptor,
                           const std::string& source_descriptor) {
    assert(graph != nullptr);
    graph->sources_by_target[target_descriptor].push_back(source_descriptor);
}

bool FakeDescriptorIsAssignableFrom(const std::string& target_descriptor,
                                    const std::string& source_descriptor,
                                    void* opaque) {
    if (target_descriptor == source_descriptor) {
        return true;
    }
    const auto* graph = static_cast<const FakeJavaAssignabilityGraph*>(opaque);
    if (graph == nullptr) {
        return false;
    }
    const auto found = graph->sources_by_target.find(target_descriptor);
    if (found == graph->sources_by_target.end()) {
        return false;
    }
    const auto& sources = found->second;
    return std::find(sources.begin(), sources.end(), source_descriptor) != sources.end();
}

FakeJavaAssignabilityGraph MakeReferenceSpecificityGraph() {
    FakeJavaAssignabilityGraph graph;
    AddAssignableRelation(&graph, "Ljava/lang/Object;", "Ljava/lang/String;");
    AddAssignableRelation(&graph, "Ljava/lang/Object;", "Ljava/lang/Integer;");
    AddAssignableRelation(&graph, "Ljava/lang/Object;", "Ljava/util/List;");
    AddAssignableRelation(&graph, "Ljava/lang/Object;", "Ljava/util/ArrayList;");
    AddAssignableRelation(&graph, "Ljava/util/List;", "Ljava/util/ArrayList;");
    return graph;
}

void TestJavaMethodSpecificityPrefersStringOverObject() {
    FakeJavaAssignabilityGraph graph = MakeReferenceSpecificityGraph();
    const auto comparison = CompareJavaMethodSpecificityForTesting(
        {"Ljava/lang/String;"},
        {"Ljava/lang/Object;"},
        &FakeDescriptorIsAssignableFrom,
        &graph);
    assert(comparison == JavaMethodSpecificityComparisonForTesting::kLeftMoreSpecific);
}

void TestJavaMethodSpecificityPrefersListOverObject() {
    FakeJavaAssignabilityGraph graph = MakeReferenceSpecificityGraph();
    const auto comparison = CompareJavaMethodSpecificityForTesting(
        {"Ljava/util/List;"},
        {"Ljava/lang/Object;"},
        &FakeDescriptorIsAssignableFrom,
        &graph);
    assert(comparison == JavaMethodSpecificityComparisonForTesting::kLeftMoreSpecific);
}

void TestJavaMethodSpecificityKeepsUnrelatedReferenceAmbiguity() {
    FakeJavaAssignabilityGraph graph = MakeReferenceSpecificityGraph();
    const auto comparison = CompareJavaMethodSpecificityForTesting(
        {"Ljava/lang/String;"},
        {"Ljava/lang/Integer;"},
        &FakeDescriptorIsAssignableFrom,
        &graph);
    assert(comparison == JavaMethodSpecificityComparisonForTesting::kIncomparable);
}

void TestJavaMethodSpecificityKeepsCrossParameterAmbiguity() {
    FakeJavaAssignabilityGraph graph = MakeReferenceSpecificityGraph();
    const auto comparison = CompareJavaMethodSpecificityForTesting(
        {"Ljava/lang/String;", "Ljava/lang/Object;"},
        {"Ljava/lang/Object;", "Ljava/lang/String;"},
        &FakeDescriptorIsAssignableFrom,
        &graph);
    assert(comparison == JavaMethodSpecificityComparisonForTesting::kIncomparable);
}

void TestJavaOverloadResolutionPrefersStringForNull() {
    FakeJavaAssignabilityGraph graph = MakeReferenceSpecificityGraph();
    size_t matched_index = static_cast<size_t>(-1);
    const auto result = ResolveMostSpecificJavaOverloadForTesting(
        {{"Ljava/lang/Object;"},
         {"Ljava/lang/String;"}},
        {"__nook_null__"},
        &FakeDescriptorIsAssignableFrom,
        &graph,
        &matched_index);
    assert(result == JavaOverloadMatchResultForTesting::kUniqueMatch);
    assert(matched_index == 1u);
}

void TestJavaDescriptorNormalizationConvertsObjectDescriptorForLookup() {
    std::string class_name;
    assert(NormalizeJavaDescriptorForClassLookupForTesting("Ljava/lang/String;", &class_name));
    assert(class_name == "java.lang.String");
}

void TestJavaDescriptorNormalizationPreservesArrayDescriptorForLookup() {
    std::string class_name;
    assert(NormalizeJavaDescriptorForClassLookupForTesting("[Ljava/lang/String;", &class_name));
    assert(class_name == "[Ljava.lang.String;");
}

void TestJavaStaticMethodWrapperCanResolveNullReferenceDirectly() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var NullableTarget = Java.use('com.demo.target.NullableTarget');"
        "send({ type: 'send', payload: String(NullableTarget.describe(null)) });";
    assert(registry.CreateScript("java_static_method_wrapper_resolves_null_reference.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"nullable:null\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.NullableTarget");
    assert(capture.record.method_name == "describe");
    assert(capture.record.signature == "(Ljava/lang/Object;)Ljava/lang/String;");
    assert(capture.record.is_static);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kUndefined);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticMethodWrapperRejectsNullForPrimitiveOnlyOverload() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "MainActivity.incrementIntercept(null);";
    assert(registry.CreateScript("java_static_method_wrapper_rejects_null_for_primitive.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Java invoke overload resolution failed") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticMethodWrapperCanResolveBoxedBooleanDirectly() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var BoxedTarget = Java.use('com.demo.target.BoxedTarget');"
        "send({ type: 'send', payload: String(BoxedTarget.acceptBoolean(true)) });";
    assert(registry.CreateScript("java_static_method_wrapper_resolves_boxed_boolean.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.BoxedTarget");
    assert(capture.record.method_name == "acceptBoolean");
    assert(capture.record.signature == "(Ljava/lang/Boolean;)Z");
    assert(capture.record.is_static);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kBoolean);
    assert(capture.args[0].bool_value);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticMethodWrapperCanResolveObjectOverloadFromStringDirectly() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var ObjectTarget = Java.use('com.demo.target.ObjectTarget');"
        "send({ type: 'send', payload: String(ObjectTarget.describe('hello')) });";
    assert(registry.CreateScript("java_static_method_wrapper_resolves_object_from_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object-string:hello\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.ObjectTarget");
    assert(capture.record.method_name == "describe");
    assert(capture.record.signature == "(Ljava/lang/Object;)Ljava/lang/String;");
    assert(capture.record.is_static);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kString);
    assert(capture.args[0].string_value == "hello");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticMethodWrapperCanResolveNumberOverloadFromPlainIntDirectly() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var NumberTarget = Java.use('com.demo.target.NumberTarget');"
        "send({ type: 'send', payload: String(NumberTarget.describe(41)) });";
    assert(registry.CreateScript("java_static_method_wrapper_resolves_number_from_plain_int.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"number:41\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.NumberTarget");
    assert(capture.record.method_name == "describe");
    assert(capture.record.signature == "(Ljava/lang/Number;)Ljava/lang/String;");
    assert(capture.record.is_static);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 41.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaInstanceMethodWrapperCanResolveObjectOverloadFromWrapperDirectly() {
    GetJavaMethodInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Looper = Java.use('android.os.Looper');"
        "var current = Looper.myLooper();"
        "send({ type: 'send', payload: String(current.equals(current)) });";
    assert(registry.CreateScript("java_instance_method_wrapper_resolves_object_from_wrapper.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    const JavaMethodInvokeCapture& capture = GetJavaMethodInvokeCapture();
    assert(capture.call_count >= 2);
    assert(capture.record.class_name == "android.os.Looper");
    assert(capture.record.method_name == "equals");
    assert(capture.record.signature == "(Ljava/lang/Object;)Z");
    assert(!capture.record.is_static);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kObject);
    assert(capture.args[0].object_class_name == "android.os.Looper");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaImplementationAssignmentInstallsBridgeHook() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "LoginFragment.verifyPasswordNative.implementation = function (password) {"
        "  return password;"
        "};"
        "send({ type: 'send', payload: 'implementation-installed' });";
    assert(registry.CreateScript("java_implementation_assignment_installs_hook.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"implementation-installed\"}");
    assert(received_data.empty());

    const JavaHookInstallCallCapture& capture = GetJavaHookInstallCallCapture();
    assert(capture.call_count == 1);
    assert(capture.request.class_name == "com.demo.target.LoginFragment");
    assert(capture.request.method_name == "verifyPasswordNative");
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOverloadImplementationInstallsExactSignatureHook() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "LoginFragment.verifyPasswordNative.overload('java.lang.String').implementation = function (password) {"
        "  return password;"
        "};";
    assert(registry.CreateScript("java_overload_implementation_installs_exact_hook.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    const JavaHookInstallCallCapture& capture = GetJavaHookInstallCallCapture();
    assert(capture.call_count == 1);
    assert(capture.request.class_name == "com.demo.target.LoginFragment");
    assert(capture.request.method_name == "verifyPasswordNative");
    assert(capture.request.signature == "(Ljava/lang/String;)Z");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaImplementationCallbackCanCallOriginal() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "LoginFragment.verifyPasswordNative.implementation = function (password) {"
        "  return this.verifyPasswordNative.callOriginal(password);"
        "};";
    assert(registry.CreateScript("java_implementation_call_original.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kString;
    arg.string_value = "jni-password";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original:jni-password");

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.LoginFragment");
    assert(capture.record.method_name == "verifyPasswordNative");
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kString);
    assert(capture.args[0].string_value == "jni-password");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaImplementationCallbackDirectSelfInvokeFallsBackToCallOriginal() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.ad2001.frida0x1.MainActivity');"
        "MainActivity.get_random.implementation = function () {"
        "  return this.get_random();"
        "};";
    assert(registry.CreateScript("java_implementation_direct_self_invoke_call_original.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, nullptr, 0u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original:");

    const JavaHookInstallCallCapture& install_capture = GetJavaHookInstallCallCapture();
    assert(install_capture.call_count == 1);
    assert(install_capture.request.class_name == "com.ad2001.frida0x1.MainActivity");
    assert(install_capture.request.method_name == "get_random");
    assert(install_capture.request.signature == "*");

    const JavaHookCallOriginalCapture& original_capture = GetJavaHookCallOriginalCapture();
    assert(original_capture.call_count == 1);
    assert(original_capture.record.class_name == "com.ad2001.frida0x1.MainActivity");
    assert(original_capture.record.method_name == "get_random");
    assert(original_capture.args.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOverloadCallbackCanCallOriginalWithExactSignature() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "LoginFragment.verifyPasswordNative.overload('java.lang.String').implementation = function (password) {"
        "  return this.verifyPasswordNative.callOriginal(password);"
        "};";
    assert(registry.CreateScript("java_overload_call_original_exact_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kString;
    arg.string_value = "exact-password";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original:exact-password");

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.LoginFragment");
    assert(capture.record.method_name == "verifyPasswordNative");
    assert(capture.record.signature == "(Ljava/lang/String;)Z");
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kString);
    assert(capture.args[0].string_value == "exact-password");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOverloadStringToStringCallbackCallOriginalUsesInstalledExactSignature() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Demo = Java.use('com.zj.wuaipojie.Demo');"
        "Demo.a.overload('java.lang.String').implementation = function (str) {"
        "  return this.a.callOriginal('52pojie');"
        "};";
    assert(registry.CreateScript("java_overload_string_to_string_call_original_exact_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kString;
    arg.string_value = "demo-input";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original:52pojie");

    const JavaHookInstallCallCapture& install_capture = GetJavaHookInstallCallCapture();
    assert(install_capture.call_count == 1);
    assert(install_capture.request.class_name == "com.zj.wuaipojie.Demo");
    assert(install_capture.request.method_name == "a");
    assert(install_capture.request.signature == "(Ljava/lang/String;)Ljava/lang/String;");

    const JavaHookCallOriginalCapture& original_capture = GetJavaHookCallOriginalCapture();
    assert(original_capture.call_count == 1);
    assert(original_capture.record.class_name == "com.zj.wuaipojie.Demo");
    assert(original_capture.record.method_name == "a");
    assert(original_capture.record.signature == "(Ljava/lang/String;)Ljava/lang/String;");
    assert(original_capture.args.size() == 1u);
    assert(original_capture.args[0].kind == JavaJsValueKind::kString);
    assert(original_capture.args[0].string_value == "52pojie");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaConstructorCallbackMethodAliasCallsOriginalInsteadOfReinvokingConstructor() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};
    GetJavaMethodInvokeCapture() = {};
    GetJavaConstructorInvokeCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    dependencies.invoke_method = &FakeInvokeJavaMethod;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var Demo = Java.use('com.zj.wuaipojie.Demo');"
        "Demo.$init.overload('java.lang.String').implementation = function (str) {"
        "  this.$init('52');"
        "};";
    assert(registry.CreateScript("java_constructor_callback_alias_calls_original.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kString;
    arg.string_value = "zj2595";
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));

    const JavaHookCallOriginalCapture& original_capture = GetJavaHookCallOriginalCapture();
    assert(original_capture.call_count == 1);
    assert(original_capture.record.class_name == "com.zj.wuaipojie.Demo");
    assert(original_capture.record.method_name == "<init>");
    assert(original_capture.record.signature == "(Ljava/lang/String;)V");
    assert(original_capture.args.size() == 1u);
    assert(original_capture.args[0].kind == JavaJsValueKind::kString);
    assert(original_capture.args[0].string_value == "52");

    const JavaMethodInvokeCapture& invoke_capture = GetJavaMethodInvokeCapture();
    assert(invoke_capture.call_count == 0);
    const JavaConstructorInvokeCapture& constructor_capture = GetJavaConstructorInvokeCapture();
    assert(constructor_capture.call_count == 0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOverloadCanInstallDistinctPrimitiveAndStringHooks() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.formatBalance.overload('double').implementation = function (amount) {"
        "  return 'double:' + amount;"
        "};"
        "TextFragment.formatBalance.overload('java.lang.String').implementation = function (amountText) {"
        "  return amountText;"
        "};";
    assert(registry.CreateScript("java_overload_installs_distinct_primitive_and_string_hooks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 2u));
    assert(GetInstalledJavaJsHookCountForTesting() == 2u);

    JavaJsHookRecord first = {};
    JavaJsHookRecord second = {};
    assert(GetJavaJsHookRecordForTesting(1u, &first));
    assert(GetJavaJsHookRecordForTesting(2u, &second));
    assert(first.class_name == "com.demo.target.TextFragment");
    assert(first.method_name == "formatBalance");
    assert(second.class_name == "com.demo.target.TextFragment");
    assert(second.method_name == "formatBalance");
    assert(first.signature == "(D)Ljava/lang/String;");
    assert(second.signature == "(Ljava/lang/String;)Ljava/lang/String;");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOverloadDoubleCallbackCanCallOriginalWithExactSignature() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.formatBalance.overload('double').implementation = function (amount) {"
        "  return this.formatBalance.callOriginal(amount);"
        "};";
    assert(registry.CreateScript("java_overload_double_call_original_exact_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kDouble;
    arg.double_value = 10.0;
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original-double:10.00");

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatBalance");
    assert(capture.record.signature == "(D)Ljava/lang/String;");
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 10.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOverloadLongCallbackCanCallOriginalWithExactSignature() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.formatScaled.overload('long').implementation = function (amount) {"
        "  return this.formatScaled.callOriginal(amount);"
        "};";
    assert(registry.CreateScript("java_overload_long_call_original_exact_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kInt64;
    arg.int64_value = 42;
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original-long:42");

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatScaled");
    assert(capture.record.signature == "(J)Ljava/lang/String;");
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 42.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaOverloadFloatCallbackCanCallOriginalWithExactSignature() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var TextFragment = Java.use('com.demo.target.TextFragment');"
        "TextFragment.formatScaled.overload('float').implementation = function (amount) {"
        "  return this.formatScaled.callOriginal(amount);"
        "};";
    assert(registry.CreateScript("java_overload_float_call_original_exact_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kFloat;
    arg.float_value = 3.5f;
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original-float:3.50");

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.TextFragment");
    assert(capture.record.method_name == "formatScaled");
    assert(capture.record.signature == "(F)Ljava/lang/String;");
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 3.5);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticOverloadImplementationInstallsExactSignatureHook() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "MainActivity.incrementIntercept.overload('int').implementation = function (value) {"
        "  return value + 1;"
        "};";
    assert(registry.CreateScript("java_static_overload_implementation_installs_exact_hook.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    const JavaHookInstallCallCapture& capture = GetJavaHookInstallCallCapture();
    assert(capture.call_count == 1);
    assert(capture.request.class_name == "com.demo.target.MainActivity");
    assert(capture.request.method_name == "incrementIntercept");
    assert(capture.request.signature == "(I)I");
    assert(capture.request.is_static);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticOverloadCallbackCanCallOriginalWithExactSignature() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "MainActivity.incrementIntercept.overload('int').implementation = function (value) {"
        "  return this.incrementIntercept.callOriginal(value);"
        "};";
    assert(registry.CreateScript("java_static_overload_call_original_exact_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kInt32;
    arg.int_value = 7;
    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kDouble);
    assert(result.double_value == 8.0);

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.MainActivity");
    assert(capture.record.method_name == "incrementIntercept");
    assert(capture.record.signature == "(I)I");
    assert(capture.record.is_static);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].kind == JavaJsValueKind::kDouble);
    assert(capture.args[0].double_value == 7.0);

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaStaticVoidOverloadCallbackCanCallOriginalWithExactSignature() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    dependencies.resolve_signature = &FakeResolveJavaMethodSignature;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var MainActivity = Java.use('com.demo.target.MainActivity');"
        "MainActivity.incrementIntercept.overload().implementation = function () {"
        "  send({ type: 'send', payload: 'static-void-enter' });"
        "  this.incrementIntercept.callOriginal();"
        "  send({ type: 'send', payload: 'static-void-leave:' + String(arguments.length) });"
        "};";
    assert(registry.CreateScript("java_static_void_overload_call_original_exact_signature.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue result = {};
    assert(JsRuntimeInvokeJavaHookCallbackForTesting(
        script_id, 1u, nullptr, 0u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kUndefined);
    assert(received_json == "{\"type\":\"send\",\"payload\":\"static-void-leave:0\"}");

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.record.class_name == "com.demo.target.MainActivity");
    assert(capture.record.method_name == "incrementIntercept");
    assert(capture.record.signature == "()V");
    assert(capture.record.is_static);
    assert(capture.args.empty());

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestJavaBridgeDispatchInvokesInstalledJsCallback() {
    ResetJavaJsHookRegistryForTesting();
    GetJavaHookInstallCallCapture() = {};
    GetJavaHookCallOriginalCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.call_original_hook = &FakeCallOriginalJavaHook;
    JsRuntimeSetJavaHookInstallerDependenciesForTesting(dependencies);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var LoginFragment = Java.use('com.demo.target.LoginFragment');"
        "LoginFragment.verifyPasswordNative.implementation = function (password) {"
        "  return this.verifyPasswordNative.callOriginal(password);"
        "};";
    assert(registry.CreateScript("java_bridge_dispatches_to_runtime.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasJavaHookCallbackForTesting(script_id, 1u));

    JavaJsValue arg = {};
    arg.kind = JavaJsValueKind::kString;
    arg.string_value = "bridge-password";
    JavaJsValue result = {};
    assert(DispatchJavaJsHookInvocationForTesting(1u, &arg, 1u, &result, &error_message));
    assert(result.kind == JavaJsValueKind::kString);
    assert(result.string_value == "original:bridge-password");

    const JavaHookCallOriginalCapture& capture = GetJavaHookCallOriginalCapture();
    assert(capture.call_count == 1);
    assert(capture.args.size() == 1u);
    assert(capture.args[0].string_value == "bridge-password");

    registry.Clear();
    JsRuntime::Shutdown();
    JsRuntimeResetJavaHookInstallerDependenciesForTesting();
}

void TestNativeAttachRequiresObjectOptions() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    assert(registry.CreateScript("native_attach_requires_object.js",
                                 "Nook.Native.attach();",
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("attach options must be an object") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeAttachRejectsUnsupportedType() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Nook.Native.attach({"
        "  type: 'plt',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("native_attach_unsupported_type.js", source, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("not implemented yet") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeAttachRequiresModuleAndFunctionCallbacks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* missing_module =
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  symbol: 'target',"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("native_attach_missing_module.js", missing_module, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("attach module is required") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();

    error_message.clear();
    assert(JsRuntime::Initialize(&error_message));

    script_id = 0;
    const char* bad_callback =
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: 1,"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("native_attach_bad_callback.js", bad_callback, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("attach onEnter must be a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeAttachRegistersCallbacksAndReturnsHookId() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    GetInlineHookUnhookCallCount() = 0;
    GetLastSnapshotRequests().clear();
    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
         "  symbol: 'target',"
         "  onEnter: function(args) {},"
         "  onLeave: function(retval) {}"
         "});"
         "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("native_attach_registers_callbacks.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(GetLastSnapshotRequests().empty());

    assert(registry.UnloadScript(script_id, &error_message));
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeAttachPassesSnapshotConfigToInstaller() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    GetLastSnapshotRequests().clear();
    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  snapshot: [{ index: 2, type: 'jstringUtf8' }],"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("native_attach_snapshot_config.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(GetLastSnapshotRequests().size() == 1u);
    assert(GetLastSnapshotRequests()[0].type == "jstringUtf8");
    assert(GetLastSnapshotRequests()[0].argument_index == 2u);
    assert(GetLastSnapshotRequests()[0].env_index == 0u);

    registry.Clear();
    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    JsRuntime::Shutdown();
}

void TestNativeAttachPassesCStringSnapshotConfigToInstaller() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    GetLastSnapshotRequests().clear();
    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  snapshot: [{ index: 1, type: 'cstringUtf8' }],"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("native_attach_cstring_snapshot_config.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(GetLastSnapshotRequests().size() == 1u);
    assert(GetLastSnapshotRequests()[0].type == "cstringUtf8");
    assert(GetLastSnapshotRequests()[0].argument_index == 1u);

    registry.Clear();
    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    JsRuntime::Shutdown();
}

void TestNativeAttachReturnsDeferredWhenModuleIsNotLoadedYet() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libnative-lib.so',"
        "  symbol: 'Java_com_demo_target_LoginFragment_verifyPasswordNative',"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("native_attach_deferred.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:true\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 0);

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindExportByNameReturnsResolvedAddress() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var address = Module.findExportByName('libdemo.so', 'target');"
        "send({ type: 'send', payload: String(address) });";
    assert(registry.CreateScript("module_find_export.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0x10000000\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleAttachExportRegistersCallbacksAndReturnsHookId() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Module.attachExport('libdemo.so', 'target', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("module_attach_export_registers_callbacks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    assert(registry.UnloadScript(script_id, &error_message));
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleAttachExportPassesSnapshotConfigToInstaller() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    GetLastSnapshotRequests().clear();
    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Module.attachExport('libdemo.so', 'target', {"
        "  snapshot: [{ index: 3, type: 'jstringUtf8', envIndex: 1 }],"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("module_attach_export_snapshot_config.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(GetLastSnapshotRequests().size() == 1u);
    assert(GetLastSnapshotRequests()[0].type == "jstringUtf8");
    assert(GetLastSnapshotRequests()[0].argument_index == 3u);
    assert(GetLastSnapshotRequests()[0].env_index == 1u);

    registry.Clear();
    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    JsRuntime::Shutdown();
}

void TestModuleAttachExportReturnsDeferredWhenModuleIsNotLoadedYet() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Module.attachExport('libnative-lib.so',"
        "                               'Java_com_demo_target_LoginFragment_verifyPasswordNative',"
        "                               {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("module_attach_export_deferred.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:true\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 0);

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerBasics() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = ptr('0x10');"
        "send({"
        "  type: 'send',"
        "  payload: typeof NativePointer + ':' +"
        "           typeof ptr + ':' +"
        "           String(NULL) + ':' +"
        "           String(NULL.isNull()) + ':' +"
        "           String(p) + ':' +"
        "           String(p.add(4)) + ':' +"
        "           String(p.sub(8)) + ':' +"
        "           String(p.isNull())"
        "});";
    assert(registry.CreateScript("native_pointer_basics.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function:0x0:true:0x10:0x14:0x8:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerToInt32AndUInt32() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = ptr('0xffffffff');"
        "send({"
        "  type: 'send',"
        "  payload: String(p.toInt32()) + ':' + String(p.toUInt32())"
        "});";
    assert(registry.CreateScript("native_pointer_to_int32_uint32.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"-1:4294967295\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerEquals() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = ptr('0x1234');"
        "var q = ptr('0x1234');"
        "var r = ptr('0x1235');"
        "var invalid;"
        "try {"
        "  p.equals('nope');"
        "} catch (e) {"
        "  invalid = String(e).indexOf('pointer value') >= 0 ? 'invalid' : String(e);"
        "}"
        "send({"
        "  type: 'send',"
        "  payload: String(p.equals(q)) + ':' +"
        "           String(p.equals(r)) + ':' +"
        "           String(p.equals('0x1234')) + ':' +"
        "           String(p.equals(0x1234)) + ':' +"
        "           invalid"
        "});";
    assert(registry.CreateScript("native_pointer_equals.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:false:true:true:invalid\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerCompare() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = ptr('0x1234');"
        "var q = ptr('0x1234');"
        "var hi = ptr('0x1235');"
        "var lo = ptr('0x1233');"
        "var invalid;"
        "try {"
        "  p.compare('nope');"
        "} catch (e) {"
        "  invalid = String(e).indexOf('pointer value') >= 0 ? 'invalid' : String(e);"
        "}"
        "send({"
        "  type: 'send',"
        "  payload: String(p.compare(q)) + ':' +"
        "           String(p.compare(hi)) + ':' +"
        "           String(p.compare(lo)) + ':' +"
        "           String(p.compare('0x1234')) + ':' +"
        "           String(p.compare(0x1235)) + ':' +"
        "           invalid"
        "});";
    assert(registry.CreateScript("native_pointer_compare.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0:-1:1:0:-1:invalid\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerBitwiseOps() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = ptr('0x12f0');"
        "var invalid;"
        "try {"
        "  p.and('nope');"
        "} catch (e) {"
        "  invalid = String(e).indexOf('pointer value') >= 0 ? 'invalid' : String(e);"
        "}"
        "send({"
        "  type: 'send',"
        "  payload: String(p.and('0xff')) + ':' +"
        "           String(p.or(0x0f)) + ':' +"
        "           String(p.xor(ptr('0xff'))) + ':' +"
        "           invalid"
        "});";
    assert(registry.CreateScript("native_pointer_bitwise_ops.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0xf0:0x12ff:0x120f:invalid\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadUtf8String() {
    static const char kText[] = "hello-nook";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(kText) + "');"
        "send({"
        "  type: 'send',"
        "  payload: p.readUtf8String() + ':' + p.readUtf8String(5)"
        "});";
    assert(registry.CreateScript("native_pointer_read_utf8.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-nook:hello\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryReadUtf8String() {
    static const char kText[] = "hello-memory";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(kText) + "');"
        "send({"
        "  type: 'send',"
        "  payload: Memory.readUtf8String(p) + ':' + Memory.readUtf8String(p, 5) + ':' +"
        "           Memory.readCString(p, 5) + ':' + Memory.readAnsiString(p, 5)"
        "});";
    assert(registry.CreateScript("memory_read_utf8.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-memory:hello:hello:hello\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryReadUtf8StringNormalizesTaggedPointer() {
    static const char kText[] = "hello-tagged";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    const uint64_t raw = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(kText));
    const uint64_t tagged = raw | 0xb400000000000000ull;
    std::ostringstream tagged_stream;
    tagged_stream << "0x" << std::hex << tagged;

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + tagged_stream.str() + "');"
        "send({"
        "  type: 'send',"
        "  payload: Memory.readUtf8String(p) + ':' + Memory.readUtf8String(p, 5)"
        "});";
    assert(registry.CreateScript("memory_read_utf8_tagged.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-tagged:hello\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadUtf8StringUsesSingleReadableProbeForContiguousBuffer() {
    static const char kText[] = "hot-path-readable-probe";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    JsRuntimeResetReadableMemoryProbeCountForTesting();

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(kText) + "');"
        "send({"
        "  type: 'send',"
        "  payload: p.readUtf8String()"
        "});";
    assert(registry.CreateScript("native_pointer_read_utf8_probe_count.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hot-path-readable-probe\"}");
    assert(received_data.empty());
    assert(JsRuntimeGetReadableMemoryProbeCountForTesting() <= 3u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryReadUtf8StringRepeatedReadsUseConstantReadableProbeCount() {
    static const char kText[] = "hello-memory-repeat";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    JsRuntimeResetReadableMemoryProbeCountForTesting();
    JsRuntimeResetReadableMappingLookupCountForTesting();

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(kText) + "');"
        "send({"
        "  type: 'send',"
        "  payload: Memory.readUtf8String(p) + ':' + Memory.readUtf8String(p, 5)"
        "});";
    assert(registry.CreateScript("memory_read_utf8_probe_count.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-memory-repeat:hello\"}");
    assert(received_data.empty());
    assert(JsRuntimeGetReadableMappingLookupCountForTesting() <= 1u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadUtf16String() {
    static const uint16_t kText[] = {0x0041u, 0x4f60u, 0x597du, 0x0000u};

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(kText) + "');"
        "var text = p.readUtf16String();"
        "var shortText = p.readUtf16String(1);"
        "send({"
        "  type: 'send',"
        "  payload: text.length + ':' + text.charCodeAt(0) + ':' + text.charCodeAt(1) + ':' + text.charCodeAt(2) + ':' + shortText.charCodeAt(0)"
        "});";
    assert(registry.CreateScript("native_pointer_read_utf16.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"3:65:20320:22909:65\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadCStringAlias() {
    static const char kText[] = "hello-cstring";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(kText) + "');"
        "send({"
        "  type: 'send',"
        "  payload: p.readCString() + ':' + p.readCString(5)"
        "});";
    assert(registry.CreateScript("native_pointer_read_cstring.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-cstring:hello\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadCStringStopsAtTerminatorBeforeGuardPage() {
    ScopedTestGuardedPageMapping mapping;
    assert(mapping.address() != nullptr);
    assert(mapping.page_size() > 8u);

    char* text = static_cast<char*>(mapping.address()) + mapping.page_size() - 3;
    text[0] = 'o';
    text[1] = 'k';
    text[2] = '\0';

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(text) + "');"
        "send({"
        "  type: 'send',"
        "  payload: p.readCString() + ':' + p.readUtf8String()"
        "});";
    assert(registry.CreateScript("native_pointer_read_cstring_guard_page.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"ok:ok\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadAnsiStringAlias() {
    static const char kText[] = "hello-ansi";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(kText) + "');"
        "send({"
        "  type: 'send',"
        "  payload: p.readAnsiString() + ':' + p.readAnsiString(5)"
        "});";
    assert(registry.CreateScript("native_pointer_read_ansi_string.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-ansi:hello\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadUtf8StringRejectsUnreadablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  ptr('0x1').readUtf8String(16);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unreadable pointer') >= 0 ? 'unreadable' : String(e) });"
        "}";
    assert(registry.CreateScript("native_pointer_read_utf8_unreadable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unreadable\"}");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadUtf16StringRejectsUnreadablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  ptr('0x1').readUtf16String(16);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unreadable pointer') >= 0 ? 'unreadable' : String(e) });"
        "}";
    assert(registry.CreateScript("native_pointer_read_utf16_unreadable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unreadable\"}");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteUtf8StringWritesCStringAndReturnsPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(32);"
        "var q = p.writeUtf8String('hello-write');"
        "send({"
        "  type: 'send',"
        "  payload: String(q.sub(p).isNull()) + ':' + q.readCString() + ':' + p.readUtf8String(5)"
        "});";
    assert(registry.CreateScript("native_pointer_write_utf8_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:hello-write:hello\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteUtf16StringWritesWideStringAndReturnsPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(32);"
        "var q = p.writeUtf16String(String.fromCharCode(0x41, 0x4f60, 0x597d));"
        "var text = p.readUtf16String();"
        "send({"
        "  type: 'send',"
        "  payload: String(q.sub(p).isNull()) + ':' + p.readU16() + ':' + p.add(2).readU16() + ':' + p.add(4).readU16() + ':' + p.add(6).readU16() + ':' + text.length + ':' + text.charCodeAt(1)"
        "});";
    assert(registry.CreateScript("native_pointer_write_utf16_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:65:20320:22909:0:3:20320\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteAnsiStringAlias() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(32);"
        "var q = p.writeAnsiString('ansi-write');"
        "send({"
        "  type: 'send',"
        "  payload: String(q.sub(p).isNull()) + ':' + q.readAnsiString() + ':' + p.readUtf8String(4)"
        "});";
    assert(registry.CreateScript("native_pointer_write_ansi_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:ansi-write:ansi\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteUtf8StringRejectsUnwritablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  ptr('0x1').writeUtf8String('x');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unwritable pointer') >= 0 ? 'unwritable' : String(e) });"
        "}";
    assert(registry.CreateScript("native_pointer_write_utf8_string_unwritable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unwritable\"}");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteUtf16StringRejectsUnwritablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  ptr('0x1').writeUtf16String('x');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unwritable pointer') >= 0 ? 'unwritable' : String(e) });"
        "}";
    assert(registry.CreateScript("native_pointer_write_utf16_string_unwritable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unwritable\"}");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadByteArrayReturnsArrayBuffer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.allocUtf8String('ABC');"
        "var blob = p.readByteArray(3);"
        "var bytes = new Uint8Array(blob);"
        "send({ type: 'send', payload: blob.byteLength + ':' + bytes[0] + ':' + bytes[1] + ':' + bytes[2] });";
    assert(registry.CreateScript("native_pointer_read_byte_array.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"3:65:66:67\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteByteArrayFromArrayBuffer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(4);"
        "var src = new Uint8Array([1, 2, 255, 0]).buffer;"
        "p.writeByteArray(src);"
        "var blob = p.readByteArray(4);"
        "var bytes = new Uint8Array(blob);"
        "send({ type: 'send', payload: bytes[0] + ':' + bytes[1] + ':' + bytes[2] + ':' + bytes[3] });";
    assert(registry.CreateScript("native_pointer_write_byte_array_buffer.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:2:255:0\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteByteArrayFromNumberArray() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(3);"
        "p.writeByteArray([16, 32, 48]);"
        "var blob = p.readByteArray(3);"
        "var bytes = new Uint8Array(blob);"
        "send({ type: 'send', payload: bytes[0] + ':' + bytes[1] + ':' + bytes[2] });";
    assert(registry.CreateScript("native_pointer_write_byte_array_numbers.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"16:32:48\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadByteArrayRejectsUnreadablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  ptr('0x1').readByteArray(4);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unreadable pointer') >= 0 ? 'unreadable' : String(e) });"
        "}";
    assert(registry.CreateScript("native_pointer_read_byte_array_unreadable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unreadable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteByteArrayRejectsUnwritablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  ptr('0x1').writeByteArray([1, 2, 3]);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unwritable pointer') >= 0 ? 'unwritable' : String(e) });"
        "}";
    assert(registry.CreateScript("native_pointer_write_byte_array_unwritable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unwritable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryAllocAndNativePointerReadWrite() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(24);"
        "var text = Memory.allocUtf8String('hello-memory');"
        "p.writeU8(18);"
        "p.add(2).writeU16(13398);"
        "p.add(8).writeU32(2023406814);"
        "p.add(16).writePointer(text);"
        "send({"
        "  type: 'send',"
        "  payload: typeof Memory.alloc + ':' +"
        "           String(p.readU8()) + ':' +"
        "           String(p.add(2).readU16()) + ':' +"
        "           String(p.add(8).readU32()) + ':' +"
        "           p.add(16).readPointer().readUtf8String() + ':' +"
        "           Memory.allocUtf8String('ok').readUtf8String()"
        "});";
    assert(registry.CreateScript("memory_alloc_native_read_write.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:18:13398:2023406814:hello-memory:ok\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryAllocUtf16String() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.allocUtf16String(String.fromCharCode(0x41, 0x4f60, 0x597d));"
        "var text = p.readUtf16String();"
        "send({"
        "  type: 'send',"
        "  payload: p.readU16() + ':' + p.add(2).readU16() + ':' + p.add(4).readU16() + ':' + p.add(6).readU16() + ':' + text.length + ':' + text.charCodeAt(2)"
        "});";
    assert(registry.CreateScript("memory_alloc_utf16_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"65:20320:22909:0:3:22909\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryAllocAnsiString() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.allocAnsiString('ansi-memory');"
        "send({"
        "  type: 'send',"
        "  payload: typeof Memory.allocAnsiString + ':' + p.readAnsiString() + ':' + p.readU8() + ':' + p.add(4).readU8()"
        "});";
    assert(registry.CreateScript("memory_alloc_ansi_string.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:ansi-memory:97:45\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadWriteU64() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(16);"
        "p.writeU64(305419896);"
        "var q = p.writeU64(4660);"
        "send({ type: 'send', payload: String(p.readU64()) + ':' + String(q.readU64()) });";
    assert(registry.CreateScript("native_pointer_read_write_u64.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"4660:4660\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryCopyCopiesBetweenValidRanges() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello-copy');"
        "var dst = Memory.alloc(32);"
        "Memory.copy(dst, src, 11);"
        "send({ type: 'send', payload: dst.readUtf8String() });";
    assert(registry.CreateScript("memory_copy_valid_ranges.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"hello-copy\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryCopyHandlesOverlap() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.allocUtf8String('abcdef');"
        "Memory.copy(p.add(1), p, 5);"
        "send({ type: 'send', payload: p.readUtf8String() });";
    assert(registry.CreateScript("memory_copy_overlap.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"aabcde\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryPatchCodeCommitsPatchedBytesAndRestoresProtection() {
    ScopedTestPageMapping mapping;
    assert(mapping.address() != nullptr);
    std::memcpy(mapping.address(), "ABCD", 5u);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var p = ptr('" + FormatTestPointer(mapping.address()) + "');"
        "Memory.protect(p, 4, 'r-x');"
        "Memory.patchCode(p, 4, function(code, size) {"
        "  code.writeByteArray([97, 98, 99, 100]);"
        "  send({"
        "    type: 'send',"
        "    payload: 'inside:' + String(code.equals(p)) + ':' + String(size) + ':' + code.readUtf8String(4) + ':' + p.readUtf8String(4)"
        "  });"
        "});"
        "var range = Process.findRangeByAddress(p);"
        "send({ type: 'send', payload: 'after:' + p.readUtf8String(4) + ':' + (range === null ? 'null' : range.protection) });";
    assert(registry.CreateScript("memory_patch_code_commit.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(sent_messages.size() == 2u);
    assert(sent_messages[0] == "{\"type\":\"send\",\"payload\":\"inside:false:4:abcd:ABCD\"}");
    assert(sent_messages[1] == "{\"type\":\"send\",\"payload\":\"after:abcd:r-x\"}");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryCopyRejectsUnreadableSource() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var dst = Memory.alloc(8);"
        "  Memory.copy(dst, ptr('0x1'), 4);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('source unreadable') >= 0 ? 'source-unreadable' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_copy_unreadable_source.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"source-unreadable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryCopyRejectsUnwritableDestination() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var src = Memory.alloc(8);"
        "  Memory.copy(ptr('0x1'), src, 4);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('destination unwritable') >= 0 ? 'destination-unwritable' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_copy_unwritable_destination.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"destination-unwritable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryDupReturnsArrayBufferLength() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello-memory');"
        "var blob = Memory.dup(src, 5);"
        "send({ type: 'send', payload: String(blob.byteLength) });";
    assert(registry.CreateScript("memory_dup_length.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"5\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryDupPreservesByteContent() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('ABC');"
        "var blob = Memory.dup(src, 3);"
        "var bytes = new Uint8Array(blob);"
        "send({ type: 'send', payload: String(bytes[0]) + ':' + String(bytes[1]) + ':' + String(bytes[2]) });";
    assert(registry.CreateScript("memory_dup_content.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"65:66:67\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryDupRejectsUnreadableSource() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Memory.dup(ptr('0x1'), 4);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unreadable source') >= 0 ? 'dup-unreadable' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_dup_unreadable_source.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"dup-unreadable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpArrayBufferSingleLine() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello');"
        "var blob = Memory.dup(src, 5);"
        "send({ type: 'send', payload: hexdump(blob) });";
    assert(registry.CreateScript("hexdump_array_buffer_single_line.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("68 65 6c 6c 6f") != std::string::npos);
    assert(received_json.find("hello") != std::string::npos);
    assert(received_json.find("payload\":\"00000000") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpArrayBufferMultiLine() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(20);"
        "for (var i = 0; i < 20; i++) p.add(i).writeU8(i);"
        "var blob = Memory.dup(p, 20);"
        "send({ type: 'send', payload: hexdump(blob) });";
    assert(registry.CreateScript("hexdump_array_buffer_multi_line.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f") !=
           std::string::npos);
    assert(received_json.find("\\n00000010") != std::string::npos);
    assert(received_json.find("10 11 12 13") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpNativePointerWithLength() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello');"
        "send({ type: 'send', payload: hexdump(src, { length: 5 }) });";
    assert(registry.CreateScript("hexdump_native_pointer_length.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("68 65 6c 6c 6f") != std::string::npos);
    assert(received_json.find("hello") != std::string::npos);
    assert(received_json.find("payload\":\"0") != std::string::npos ||
           received_json.find("payload\":\"7") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpNativePointerWithOffset() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello');"
        "send({ type: 'send', payload: hexdump(src, { offset: 1, length: 4 }) });";
    assert(registry.CreateScript("hexdump_native_pointer_offset.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("65 6c 6c 6f") != std::string::npos);
    assert(received_json.find("ello") != std::string::npos);
    assert(received_json.find("payload\":\"0") != std::string::npos ||
           received_json.find("payload\":\"7") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpArrayBufferWithHeaderIncludesAscii() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello');"
        "var blob = Memory.dup(src, 5);"
        "send({ type: 'send', payload: hexdump(blob, { header: true }) });";
    assert(registry.CreateScript("hexdump_array_buffer_header_ascii.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("00 01 02 03 04") != std::string::npos);
    assert(received_json.find("68 65 6c 6c 6f") != std::string::npos);
    assert(received_json.find("hello") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpNativePointerWithHeaderIncludesAddressAndAscii() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello');"
        "send({ type: 'send', payload: hexdump(src, { length: 5, header: true }) });";
    assert(registry.CreateScript("hexdump_native_pointer_header_ascii.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("00 01 02 03 04") != std::string::npos);
    assert(received_json.find("68 65 6c 6c 6f") != std::string::npos);
    assert(received_json.find("hello") != std::string::npos);
    assert(received_json.find("\\n000") != std::string::npos ||
           received_json.find("\\n7") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpNativePointerRequiresLength() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var src = Memory.allocUtf8String('hello');"
        "  hexdump(src);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('requires length') >= 0 ? 'length-required' : String(e) });"
        "}";
    assert(registry.CreateScript("hexdump_native_pointer_requires_length.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"length-required\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpRejectsUnreadablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  hexdump(ptr('0x1'), { length: 4 });"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unreadable pointer') >= 0 ? 'unreadable' : String(e) });"
        "}";
    assert(registry.CreateScript("hexdump_unreadable_pointer.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unreadable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpRejectsOutOfBoundsArrayBufferRange() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var src = Memory.allocUtf8String('hello');"
        "  var blob = Memory.dup(src, 5);"
        "  hexdump(blob, { offset: 4, length: 2 });"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('out of bounds') >= 0 ? 'bounds' : String(e) });"
        "}";
    assert(registry.CreateScript("hexdump_array_buffer_bounds.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"bounds\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpAnsiOutputIncludesEscapeCodes() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var src = Memory.allocUtf8String('hello');"
        "var blob = Memory.dup(src, 5);"
        "send({ type: 'send', payload: hexdump(blob, { ansi: true }) });";
    assert(registry.CreateScript("hexdump_ansi_output.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("68 65 6c 6c 6f") != std::string::npos);
    assert(received_json.find("hello") != std::string::npos);
    assert(received_json.find("\\u001b[") != std::string::npos ||
           received_json.find("\\x1b[") != std::string::npos ||
           received_json.find("\u001b[") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestHexdumpMixedPrintableAndNonPrintableAscii() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(6);"
        "p.add(0).writeU8(0x41);"
        "p.add(1).writeU8(0x42);"
        "p.add(2).writeU8(0x00);"
        "p.add(3).writeU8(0x7f);"
        "p.add(4).writeU8(0x43);"
        "p.add(5).writeU8(0x44);"
        "var blob = Memory.dup(p, 6);"
        "send({ type: 'send', payload: hexdump(blob, { header: true }) });";
    assert(registry.CreateScript("hexdump_mixed_ascii.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("41 42 00 7f 43 44") != std::string::npos);
    assert(received_json.find("AB..CD") != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteRejectsUnwritablePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  ptr('0x1').writeU8(1);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unwritable pointer') >= 0 ? 'unwritable' : String(e) });"
        "}";
    assert(registry.CreateScript("native_pointer_write_unwritable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unwritable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNormalizeTaggedProcessAddressForRangeCheck() {
    const uint64_t tagged = 0xb400007ac003c3c0ull;
    const uint64_t normalized = JsRuntimeNormalizeProcessAddressForTesting(tagged);
    assert(normalized == 0x0000007ac003c3c0ull);
}

void TestSyntheticModuleLookupDoesNotMergeDisjointMappingsByPath() {
    const JsRuntimeTestModuleMapping mappings[] = {
        {"memfd:jit-cache (deleted)", 0x1000u, 0x2000u},
        {"memfd:jit-cache (deleted)", 0x9000u, 0xa000u},
        {"/data/local/tmp/nook/libnook-agent.so", 0x5000u, 0x6000u},
    };

    JsRuntimeTestModuleRecord record = {};
    assert(JsRuntimeGetModuleByAddressForTesting(mappings,
                                                 sizeof(mappings) / sizeof(mappings[0]),
                                                 0x5000u,
                                                 &record));
    assert(record.name == "libnook-agent.so");
    assert(record.path == "/data/local/tmp/nook/libnook-agent.so");
    assert(record.base == 0x5000u);
    assert(record.size == 0x1000u);
}

void TestUInt64AndInt64Basics() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof uint64 + ':' +"
        "           uint64('18446744073709551615').toString() + ':' +"
        "           int64('-1').toString()"
        "});";
    assert(registry.CreateScript("uint64_int64_basics.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:18446744073709551615:-1\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadWriteU64SupportsUInt64Objects() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(16);"
        "p.writeU64(uint64('18446744073709551615'));"
        "send({ type: 'send', payload: p.readU64().toString() });";
    assert(registry.CreateScript("native_pointer_u64_object.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"18446744073709551615\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadSignedScalars() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(16);"
        "p.writeU8(255);"
        "p.add(2).writeU16(65534);"
        "p.add(4).writeU32(4294967293);"
        "send({ type: 'send', payload: "
        "String(p.readS8()) + ':' + "
        "String(p.add(2).readS16()) + ':' + "
        "String(p.add(4).readS32()) });";
    assert(registry.CreateScript("native_pointer_read_signed_scalars.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"-1:-2:-3\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteSignedScalars() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(16);"
        "p.writeS8(-1);"
        "p.add(2).writeS16(-2);"
        "p.add(4).writeS32(-3);"
        "send({ type: 'send', payload: "
        "String(p.readU8()) + ':' + "
        "String(p.add(2).readU16()) + ':' + "
        "String(p.add(4).readU32()) });";
    assert(registry.CreateScript("native_pointer_write_signed_scalars.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"255:65534:4294967293\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadWriteS64() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(8);"
        "p.writeU64(uint64('18446744073709551615'));"
        "var a = p.readS64().toString();"
        "p.writeS64(int64('-1'));"
        "send({ type: 'send', payload: a + ':' + p.readS64().toString() + ':' + p.readU64().toString() });";
    assert(registry.CreateScript("native_pointer_read_write_s64.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"-1:-1:18446744073709551615\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerReadWriteFloatAndDouble() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(16);"
        "p.writeFloat(1.25);"
        "p.add(8).writeDouble(2.5);"
        "send({ type: 'send', payload: p.readFloat().toFixed(2) + ':' + p.add(8).readDouble().toFixed(2) });";
    assert(registry.CreateScript("native_pointer_read_write_float_double.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1.25:2.50\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativePointerWriteFloatAndDoubleReturnsSamePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var p = Memory.alloc(16);"
        "var q = p.writeFloat(3.5);"
        "var r = p.add(8).writeDouble(4.25);"
        "send({ type: 'send', payload: ["
        "  String(q.equals(p)),"
        "  String(r.equals(p.add(8))),"
        "  q.readFloat().toFixed(2),"
        "  r.readDouble().toFixed(2)"
        "].join(':') });";
    assert(registry.CreateScript("native_pointer_write_float_double_return.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:true:3.50:4.25\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryProtectRejectsMissingArguments() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Memory.protect();"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('requires address, size, and protection') >= 0 ? 'missing' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_protect_missing_arguments.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"missing\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryProtectRejectsNullPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Memory.protect(NULL, 4096, 'rw-');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('non-zero pointer') >= 0 ? 'null' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_protect_null_pointer.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"null\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryProtectRejectsZeroSize() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var p = Memory.alloc(16);"
        "  Memory.protect(p, 0, 'rw-');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('positive number') >= 0 ? 'size' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_protect_zero_size.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"size\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryProtectRejectsInvalidProtectionString() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var p = Memory.alloc(16);"
        "  Memory.protect(p, 16, 'abc');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('r, w, x, and -') >= 0 ? 'prot' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_protect_invalid_protection.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"prot\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryProtectTogglesReadOnlyAndReadWrite() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var page = ptr('" + FormatTestPointer(page.address()) + "');"
        "send({"
        "  type: 'send',"
        "  payload: String(Memory.protect(page, " + std::to_string(page.size()) + ", 'r--')) + ':' +"
        "           String(Memory.protect(page, " + std::to_string(page.size()) + ", 'rw-'))"
        "});";
    assert(registry.CreateScript("memory_protect_toggle_rw.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryProtectAcceptsExecutableProtection() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var page = ptr('" + FormatTestPointer(page.address()) + "');"
        "send({ type: 'send', payload: String(Memory.protect(page, " + std::to_string(page.size()) + ", 'r-x')) });";
    assert(registry.CreateScript("memory_protect_rx.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryProtectAcceptsAllPermissionTriples() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var page = ptr('" + FormatTestPointer(page.address()) + "');"
        "send({"
        "  type: 'send',"
        "  payload: ["
        "    Memory.protect(page, " + std::to_string(page.size()) + ", '---'),"
        "    Memory.protect(page, " + std::to_string(page.size()) + ", 'r--'),"
        "    Memory.protect(page, " + std::to_string(page.size()) + ", 'rw-'),"
        "    Memory.protect(page, " + std::to_string(page.size()) + ", 'r-x'),"
        "    Memory.protect(page, " + std::to_string(page.size()) + ", 'rwx')"
        "  ].join(':')"
        "});";
    assert(registry.CreateScript("memory_protect_all_triplets.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:true:true:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanSyncFindsExactPatternTwice() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var text = Memory.allocUtf8String('hello hello');"
        "var matches = Memory.scanSync(text, 11, '68 65 6c 6c 6f');"
        "send({"
        "  type: 'send',"
        "  payload: matches.length + ':' +"
        "           String(matches[0].address.sub(text)) + ':' +"
        "           String(matches[1].address.sub(text)) + ':' +"
        "           String(matches[0].size)"
        "});";
    assert(registry.CreateScript("memory_scan_sync_exact_twice.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"2:0x0:0x6:5\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanSyncSupportsWildcardBytes() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var text = Memory.allocUtf8String('hello');"
        "var matches = Memory.scanSync(text, 5, '68 65 ?? 6c 6f');"
        "send({ type: 'send', payload: matches.length + ':' + String(matches[0].size) });";
    assert(registry.CreateScript("memory_scan_sync_wildcard.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:5\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanSyncReturnsEmptyArrayOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var text = Memory.allocUtf8String('hello');"
        "var matches = Memory.scanSync(text, 5, '66 6f 6f');"
        "send({ type: 'send', payload: String(matches.length) });";
    assert(registry.CreateScript("memory_scan_sync_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanSyncRejectsInvalidPattern() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var text = Memory.allocUtf8String('hello');"
        "  Memory.scanSync(text, 5, 'GG');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('invalid token') >= 0 ? 'pattern' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_scan_sync_invalid_pattern.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"pattern\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanSyncRejectsUnreadableRange() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Memory.scanSync(ptr('0x1'), 4, '41');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('unreadable range') >= 0 ? 'unreadable' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_scan_sync_unreadable.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"unreadable\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanInvokesOnMatchAndOnComplete() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var text = Memory.allocUtf8String('hello hello');"
        "var events = [];"
        "Memory.scan(text, 11, '68 65 6c 6c 6f', {"
        "  onMatch: function(address, size) {"
        "    events.push('match:' + String(address.sub(text)) + ':' + String(size));"
        "  },"
        "  onComplete: function() {"
        "    events.push('complete');"
        "    send({ type: 'send', payload: events.join('|') });"
        "  }"
        "});";
    assert(registry.CreateScript("memory_scan_callbacks.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"match:0x0:5|match:0x6:5|complete\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanSupportsStopFromOnMatch() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var text = Memory.allocUtf8String('hello hello');"
        "var events = [];"
        "Memory.scan(text, 11, '68 65 6c 6c 6f', {"
        "  onMatch: function(address, size) {"
        "    events.push('match:' + String(address.sub(text)));"
        "    return 'stop';"
        "  },"
        "  onComplete: function() {"
        "    events.push('complete');"
        "    send({ type: 'send', payload: events.join('|') });"
        "  }"
        "});";
    assert(registry.CreateScript("memory_scan_stop.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"match:0x0|complete\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanInvokesOnErrorAndOnCompleteForUnreadableRange() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Memory.scan(ptr('0x1'), 4, '41', {"
        "  onMatch: function(address, size) {"
        "    events.push('match');"
        "  },"
        "  onError: function(reason) {"
        "    events.push('error:' + reason);"
        "  },"
        "  onComplete: function() {"
        "    events.push('complete');"
        "    send({ type: 'send', payload: events.join('|') });"
        "  }"
        "});";
    assert(registry.CreateScript("memory_scan_unreadable_callbacks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"error:Memory.scan unreadable range|complete\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestMemoryScanRejectsInvalidCallbacksObject() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  var text = Memory.allocUtf8String('hello');"
        "  Memory.scan(text, 5, '68', 1);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('callbacks must be an object') >= 0 ? 'callbacks' : String(e) });"
        "}";
    assert(registry.CreateScript("memory_scan_invalid_callbacks.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"callbacks\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessEnumerateRangesBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Process + ':' + typeof Process.enumerateRanges"
        "});";
    assert(registry.CreateScript("process_enumerate_ranges_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessStaticPropertiesExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: ["
        "    typeof Process.pointerSize,"
        "    typeof Process.pageSize,"
        "    typeof Process.arch,"
        "    typeof Process.platform"
        "  ].join(':')"
        "});";
    assert(registry.CreateScript("process_static_properties_exist.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"number:number:string:string\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessStaticPropertiesValues() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: ["
        "    String(Process.pointerSize),"
        "    String(Process.pageSize > 0),"
        "    Process.arch,"
        "    Process.platform"
        "  ].join(':')"
        "});";
    assert(registry.CreateScript("process_static_properties_values.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
#if defined(_WIN32)
    assert(received_json == "{\"type\":\"send\",\"payload\":\"8:true:x64:windows\"}");
#elif defined(__ANDROID__)
    assert(received_json == "{\"type\":\"send\",\"payload\":\"8:true:arm64:linux\"}");
#else
    assert(received_json == "{\"type\":\"send\",\"payload\":\"8:true:x64:linux\"}");
#endif
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessIdentityBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: [typeof Process.id, typeof Process.isDebuggerAttached].join(':')"
        "});";
    assert(registry.CreateScript("process_identity_bindings_exist.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"number:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessIdentityValues() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: [String(Process.id > 0), typeof Process.isDebuggerAttached(), String(Process.isDebuggerAttached())].join(':')"
        "});";
    assert(registry.CreateScript("process_identity_values.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:boolean:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessThreadBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: [typeof Process.getCurrentThreadId, typeof Process.enumerateThreads].join(':')"
        "});";
    assert(registry.CreateScript("process_thread_bindings_exist.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessThreadApisReturnCurrentThread() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var current = Process.getCurrentThreadId();"
        "var threads = Process.enumerateThreads();"
        "var found = threads.find(function (thread) { return thread.id === current; });"
        "send({"
        "  type: 'send',"
        "  payload: [String(current > 0), String(threads.length > 0), String(found !== undefined), found === undefined ? 'missing' : typeof found.state, found === undefined ? 'missing' : typeof found.name].join(':')"
        "});";
    assert(registry.CreateScript("process_thread_values.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:true:true:string:string\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadIdMatchesCurrentThreadId() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: [typeof Thread.id, String(Thread.id > 0), String(Thread.id === Process.getCurrentThreadId())].join(':')"
        "});";
    assert(registry.CreateScript("thread_id_matches_current.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"number:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadSleepBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Thread.sleep"
        "});";
    assert(registry.CreateScript("thread_sleep_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadSleepDelaysExecution() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Thread.sleep(0.02);"
        "send({ type: 'send', payload: typeof Thread.sleep });";
    assert(registry.CreateScript("thread_sleep_delays.js",
                                 source,
                                 &script_id,
                                 &error_message));

    const auto started_at = std::chrono::steady_clock::now();
    assert(registry.LoadScript(script_id, &error_message));
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();

    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());
    assert(elapsed_ms >= 10);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadSleepRejectsNegativeSeconds() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Thread.sleep(-1);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('seconds must be') >= 0 ? 'negative-seconds' : String(e) });"
        "}";
    assert(registry.CreateScript("thread_sleep_negative.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"negative-seconds\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessEnumerateModulesBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Process.enumerateModules });";
    assert(registry.CreateScript("process_enumerate_modules_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessEnumerateModulesFindsCurrentExecutable() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var modules = Process.enumerateModules();"
        "var found = modules.find(function (m) { return m.name === '" + module_name + "'; });"
        "send({"
        "  type: 'send',"
        "  payload: String(modules.length > 0) + ':' + String(found !== undefined) + ':' +"
        "           (found === undefined ? 'missing' : String(found.base.isNull()) + ':' + String(found.size > 0))"
        "});";
    assert(registry.CreateScript("process_enumerate_modules_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"true:true:false:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessFindModuleByNameAndGetModuleByName() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var found = Process.findModuleByName('" + module_name + "');"
        "var got = Process.getModuleByName('" + module_name + "');"
        "send({"
        "  type: 'send',"
        "  payload: ["
        "    typeof Process.findModuleByName,"
        "    typeof Process.getModuleByName,"
        "    found === null ? 'null' : found.name,"
        "    got.name,"
        "    String(String(found.base) === String(got.base))"
        "  ].join(':')"
        "});";
    assert(registry.CreateScript("process_find_get_module_by_name.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"function:function:") +
               module_name + ":" + module_name + ":true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessMainModuleResolvesCurrentExecutable() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* prefix =
        "var mainModule = Process.mainModule;"
        "send({"
        "  type: 'send',"
        "  payload: mainModule === null ? 'null' : ";
    const char* suffix =
        "});";
    const std::string source =
        std::string(prefix) +
        "'"
        + module_name +
        "' === mainModule.name ? mainModule.name + ':' + String(mainModule.base.isNull()) + ':' + String(mainModule.size > 0) : mainModule.name" +
        suffix;
    assert(registry.CreateScript("process_main_module.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"") +
               module_name + ":false:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleLoadBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.load });";
    assert(registry.CreateScript("module_load_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleLoadReturnsModuleObject() {
#if defined(_WIN32)
    const std::string module_name = "kernel32.dll";
#else
    const std::string module_name = "libc.so.6";
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var loaded = Module.load('" + module_name + "');"
        "var found = Process.findModuleByName(loaded.name);"
        "send({"
        "  type: 'send',"
        "  payload: [typeof Module.load, String(loaded.name.length > 0), String(loaded.base.isNull()), String(loaded.size > 0), String(found !== null && String(found.base) === String(loaded.base))].join(':')"
        "});";
    assert(registry.CreateScript("module_load_returns_object.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:true:false:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnsureInitializedBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.ensureInitialized });";
    assert(registry.CreateScript("module_ensure_initialized_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnsureInitializedLoadedModuleReturnsUndefined() {
#if defined(_WIN32)
    const std::string module_name = "kernel32.dll";
#else
    const std::string module_name = "libc.so.6";
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var loaded = Module.load('" + module_name + "');"
        "var result = Module.ensureInitialized(loaded.name);"
        "send({"
        "  type: 'send',"
        "  payload: [typeof Module.ensureInitialized, String(result === undefined), String(Process.findModuleByName(loaded.name) !== null)].join(':')"
        "});";
    assert(registry.CreateScript("module_ensure_initialized_loaded.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnsureInitializedThrowsOnMissingModule() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Module.ensureInitialized('DefinitelyMissingModule123');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('module not found') >= 0 ? 'module-not-found' : String(e) });"
        "}";
    assert(registry.CreateScript("module_ensure_initialized_missing.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"module-not-found\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateExportsBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.enumerateExports });";
    assert(registry.CreateScript("module_enumerate_exports_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateExportsFindsSmokeExport() {
    std::string module_name;
    std::string export_name;
#if defined(_WIN32)
    module_name = "kernel32.dll";
    export_name = "GetCurrentProcessId";
#else
    module_name = GetCurrentTestModuleName();
    export_name = "NookNativeFunctionSmokeAdd";
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var exports = Module.enumerateExports('" + module_name + "');"
        "var hit = exports.find(function (entry) { return entry.name === '" + export_name + "'; });"
        "send({"
        "  type: 'send',"
        "  payload: hit === undefined ? 'missing' : [hit.type, hit.name, String(hit.address.isNull())].join(':')"
        "});";
    assert(registry.CreateScript("module_enumerate_exports_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"function:") +
               export_name + ":false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindAndGetSymbolByName() {
    std::string module_name;
    std::string symbol_name;
#if defined(_WIN32)
    module_name = "kernel32.dll";
    symbol_name = "GetCurrentProcessId";
#else
    module_name = GetCurrentTestModuleName();
    symbol_name = "NookNativeFunctionSmokeAdd";
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var found = Module.findSymbolByName('" + module_name + "', '" + symbol_name + "');"
        "var got = Module.getSymbolByName('" + module_name + "', '" + symbol_name + "');"
        "send({"
        "  type: 'send',"
        "  payload: [typeof Module.findSymbolByName, typeof Module.getSymbolByName, String(found === null), String(got.isNull()), String(found !== null && found.equals(got))].join(':')"
        "});";
    assert(registry.CreateScript("module_find_get_symbol_by_name.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:function:false:false:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetSymbolByNameThrowsOnMiss() {
    std::string module_name;
#if defined(_WIN32)
    module_name = "kernel32.dll";
#else
    module_name = GetCurrentTestModuleName();
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "try {"
        "  Module.getSymbolByName('" + module_name + "', 'DefinitelyMissingSymbol123');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('symbol not found') >= 0 ? 'symbol-not-found' : String(e) });"
        "}";
    assert(registry.CreateScript("module_get_symbol_by_name_miss.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"symbol-not-found\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateSymbolsBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.enumerateSymbols });";
    assert(registry.CreateScript("module_enumerate_symbols_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateSymbolsFindsKnownSymbol() {
    std::string module_name;
    std::string symbol_name;
#if defined(_WIN32)
    module_name = "kernel32.dll";
    symbol_name = "GetCurrentProcessId";
#else
    module_name = GetCurrentTestModuleName();
    symbol_name = "NookNativeFunctionSmokeAdd";
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var symbols = Module.enumerateSymbols('" + module_name + "');"
        "var hit = symbols.find(function (entry) { return entry.name === '" + symbol_name + "'; });"
        "send({"
        "  type: 'send',"
        "  payload: hit === undefined ? 'missing' : [String(symbols.length > 0), hit.name, String(hit.address.isNull())].join(':')"
        "});";
    assert(registry.CreateScript("module_enumerate_symbols_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"true:") +
               symbol_name + ":false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateImportsBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.enumerateImports });";
    assert(registry.CreateScript("module_enumerate_imports_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateImportsFindsImportedEntry() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var imports = Module.enumerateImports('" + module_name + "');"
        "var hit = imports.find(function (entry) {"
        "  return typeof entry.name === 'string' && entry.name.length > 0;"
        "});"
        "send({"
        "  type: 'send',"
        "  payload: hit === undefined ? 'missing' : [String(imports.length > 0), hit.type, typeof hit.module, String(hit.slot.isNull()), String(hit.address.isNull())].join(':')"
        "});";
    assert(registry.CreateScript("module_enumerate_imports_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"true:function:string:false:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindAndGetImportByName() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var imports = Module.enumerateImports('" + module_name + "');"
        "var hit = imports.find(function (entry) {"
        "  return typeof entry.name === 'string' && entry.name.length > 0;"
        "});"
        "if (hit === undefined) {"
        "  send({ type: 'send', payload: 'missing-import' });"
        "} else {"
        "  var found = Module.findImportByName('" + module_name + "', hit.name);"
        "  var got = Module.getImportByName('" + module_name + "', hit.name);"
        "  send({"
        "    type: 'send',"
        "    payload: [typeof Module.findImportByName, typeof Module.getImportByName, String(found === null), String(got.isNull()), String(found !== null && found.equals(got))].join(':')"
        "  });"
        "}";
    assert(registry.CreateScript("module_find_get_import_by_name.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:function:false:false:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetImportByNameThrowsOnMiss() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "try {"
        "  Module.getImportByName('" + module_name + "', 'DefinitelyMissingImport123');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('import not found') >= 0 ? 'import-not-found' : String(e) });"
        "}";
    assert(registry.CreateScript("module_get_import_by_name_miss.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"import-not-found\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessEnumerateRangesRejectsInvalidProtection() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Process.enumerateRanges('abc');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('must be exactly 3 characters') >= 0 ? 'protection' : String(e) });"
        "}";
    assert(registry.CreateScript("process_enumerate_ranges_invalid.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"protection\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessEnumerateRangesFindsReadWriteMapping() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var page = ptr('" + FormatTestPointer(page.address()) + "');"
        "var ranges = Process.enumerateRanges('rw-');"
        "var match = null;"
        "for (var i = 0; i !== ranges.length; i++) {"
        "  if (String(ranges[i].base) === String(page)) {"
        "    match = ranges[i];"
        "    break;"
        "  }"
        "}"
        "send({"
        "  type: 'send',"
        "  payload: match === null ? 'missing' : String(match.base) + ':' + String(match.size) + ':' + match.protection"
        "});";
    assert(registry.CreateScript("process_enumerate_ranges_rw.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    const std::string expected =
        std::string("{\"type\":\"send\",\"payload\":\"") +
        FormatTestPointer(page.address()) + ":" + std::to_string(page.size()) + ":rw-\"}";
    assert(received_json == expected);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessEnumerateRangesFindsReadOnlyMappingAfterProtect() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var page = ptr('" + FormatTestPointer(page.address()) + "');"
        "Memory.protect(page, " + std::to_string(page.size()) + ", 'r--');"
        "var ranges = Process.enumerateRanges('r--');"
        "var match = null;"
        "for (var i = 0; i !== ranges.length; i++) {"
        "  if (String(ranges[i].base) === String(page)) {"
        "    match = ranges[i];"
        "    break;"
        "  }"
        "}"
        "send({"
        "  type: 'send',"
        "  payload: match === null ? 'missing' : String(match.base) + ':' + String(match.size) + ':' + match.protection"
        "});";
    assert(registry.CreateScript("process_enumerate_ranges_ro.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    const std::string expected =
        std::string("{\"type\":\"send\",\"payload\":\"") +
        FormatTestPointer(page.address()) + ":" + std::to_string(page.size()) + ":r--\"}";
    assert(received_json == expected);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessFindRangeByAddressBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Process.findRangeByAddress"
        "});";
    assert(registry.CreateScript("process_find_range_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessFindRangeByAddressFindsMapping() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var page = ptr('" + FormatTestPointer(page.address()) + "');"
        "var range = Process.findRangeByAddress(page);"
        "send({"
        "  type: 'send',"
        "  payload: range === null ? 'missing' : String(range.base) + ':' + String(range.size) + ':' + range.protection"
        "});";
    assert(registry.CreateScript("process_find_range_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    const std::string expected =
        std::string("{\"type\":\"send\",\"payload\":\"") +
        FormatTestPointer(page.address()) + ":" + std::to_string(page.size()) + ":rw-\"}";
    assert(received_json == expected);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessFindRangeByAddressRejectsInvalidPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Process.findRangeByAddress('abc');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('pointer value') >= 0 ? 'pointer' : String(e) });"
        "}";
    assert(registry.CreateScript("process_find_range_invalid.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"pointer\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessFindRangeByAddressReturnsNullOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var range = Process.findRangeByAddress(ptr('0x1'));"
        "send({ type: 'send', payload: String(range === null) });";
    assert(registry.CreateScript("process_find_range_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessFindRangeByAddressNormalizesTaggedPointer() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    const uint64_t raw = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(page.address()));
    const uint64_t tagged = raw | 0xb400000000000000ull;

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    std::ostringstream tagged_stream;
    tagged_stream << "0x" << std::hex << tagged;
    const std::string source =
        "var tagged = ptr('" + tagged_stream.str() + "');"
        "var range = Process.findRangeByAddress(tagged);"
        "send({"
        "  type: 'send',"
        "  payload: range === null ? 'missing' : String(range.base) + ':' + range.protection"
        "});";
    assert(registry.CreateScript("process_find_range_tagged.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    const std::string expected =
        std::string("{\"type\":\"send\",\"payload\":\"") +
        FormatTestPointer(page.address()) + ":rw-\"}";
    assert(received_json == expected);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessGetModuleByAddressBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Process.getModuleByAddress });";
    assert(registry.CreateScript("process_get_module_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessGetModuleByAddressFindsCurrentExecutable() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var base = Module.getBaseAddress('" + module_name + "');"
        "var module = Process.getModuleByAddress(base);"
        "send({"
        "  type: 'send',"
        "  payload: module === null ? 'missing' : module.name + ':' + String(module.base.isNull()) + ':' + String(module.size > 0) + ':' + String(module.path.length > 0)"
        "});";
    assert(registry.CreateScript("process_get_module_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"") +
               module_name + ":false:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessGetModuleByAddressRejectsInvalidPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Process.getModuleByAddress('abc');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('pointer value') >= 0 ? 'pointer' : String(e) });"
        "}";
    assert(registry.CreateScript("process_get_module_invalid.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"pointer\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessGetModuleByAddressReturnsNullOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var module = Process.getModuleByAddress(ptr('0x1'));"
        "send({ type: 'send', payload: String(module === null) });";
    assert(registry.CreateScript("process_get_module_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessGetModuleByAddressNormalizesTaggedPointer() {
    const std::string module_name = GetCurrentTestModuleName();
    const uint64_t base = GetCurrentTestModuleBaseAddress();
    const uint64_t tagged = base | 0xb400000000000000ull;

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    std::ostringstream tagged_stream;
    tagged_stream << "0x" << std::hex << tagged;
    const std::string source =
        "var tagged = ptr('" + tagged_stream.str() + "');"
        "var module = Process.getModuleByAddress(tagged);"
        "send({ type: 'send', payload: module === null ? 'missing' : module.name });";
    assert(registry.CreateScript("process_get_module_tagged.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"") + module_name + "\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindRangeByAddressBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.findRangeByAddress });";
    assert(registry.CreateScript("module_find_range_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindRangeByAddressFindsMapping() {
    ScopedTestPageMapping page;
    assert(page.address() != nullptr);

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var page = ptr('" + FormatTestPointer(page.address()) + "');"
        "var range = Module.findRangeByAddress(page);"
        "send({"
        "  type: 'send',"
        "  payload: range === null ? 'missing' : String(range.base) + ':' + String(range.size) + ':' + range.protection"
        "});";
    assert(registry.CreateScript("module_find_range_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    const std::string expected =
        std::string("{\"type\":\"send\",\"payload\":\"") +
        FormatTestPointer(page.address()) + ":" + std::to_string(page.size()) + ":rw-\"}";
    assert(received_json == expected);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindRangeByAddressReturnsNullOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var range = Module.findRangeByAddress(ptr('0x1'));"
        "send({ type: 'send', payload: String(range === null) });";
    assert(registry.CreateScript("module_find_range_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleBaseAddressBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: typeof Module.findBaseAddress + ':' + typeof Module.getBaseAddress"
        "});";
    assert(registry.CreateScript("module_find_base_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindBaseAddressReturnsNativePointerForLoadedModule() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var base = Module.findBaseAddress('" + module_name + "');"
        "send({"
        "  type: 'send',"
        "  payload: base === null ? 'missing' : String(base.isNull()) + ':' + String(base)"
        "});";
    assert(registry.CreateScript("module_find_base_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json.find("{\"type\":\"send\",\"payload\":\"false:0x") == 0);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindBaseAddressReturnsNullOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var base = Module.findBaseAddress('missing-module-for-test.so');"
        "send({ type: 'send', payload: String(base === null) });";
    assert(registry.CreateScript("module_find_base_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetBaseAddressThrowsOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Module.getBaseAddress('missing-module-for-test.so');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('module not found') >= 0 ? 'module-not-found' : String(e) });"
        "}";
    assert(registry.CreateScript("module_get_base_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"module-not-found\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetExportByNameBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.getExportByName });";
    assert(registry.CreateScript("module_get_export_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetExportByNameReturnsNativePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var address = Module.getExportByName('libdemo.so', 'target');"
        "send({"
        "  type: 'send',"
        "  payload: String(address) + ':' + String(address.add(4)) + ':' + String(address.isNull())"
        "});";
    assert(registry.CreateScript("module_get_export_native_pointer.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0x10000000:0x10000004:false\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetExportByNameThrowsOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Module.getExportByName('libdemo.so', 'missing');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('export not found') >= 0 ? 'export-not-found' : String(e) });"
        "}";
    assert(registry.CreateScript("module_get_export_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"export-not-found\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetExportByNameFallsBackWhenLoadedResolverMisses() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    SetNativeJsResolveSymbolAddressForTesting(&FakeResolveSymbolAddressFallback);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var address = Module.getExportByName('libc.so', 'strcmp');"
        "send({ type: 'send', payload: String(address) });";
    assert(registry.CreateScript("module_get_export_fallback.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0x70000000\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsResolveSymbolAddressForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGlobalExportBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.findGlobalExportByName + ':' + typeof Module.getGlobalExportByName });";
    assert(registry.CreateScript("module_global_export_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindAndGetGlobalExportByName() {
#if defined(_WIN32)
    const std::string symbol_name = "GetCurrentProcessId";
#else
    const std::string symbol_name = "NookNativeFunctionSmokeAdd";
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var found = Module.findGlobalExportByName('" + symbol_name + "');"
        "var got = Module.getGlobalExportByName('" + symbol_name + "');"
        "send({"
        "  type: 'send',"
        "  payload: [typeof Module.findGlobalExportByName, typeof Module.getGlobalExportByName, String(found === null), String(got.isNull()), String(found !== null && found.equals(got))].join(':')"
        "});";
    assert(registry.CreateScript("module_find_get_global_export_by_name.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:function:false:false:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleGetGlobalExportByNameThrowsOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "try {"
        "  Module.getGlobalExportByName('DefinitelyMissingGlobalExport123');"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({ type: 'send', payload: String(e).indexOf('export not found') >= 0 ? 'export-not-found' : String(e) });"
        "}";
    assert(registry.CreateScript("module_get_global_export_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"export-not-found\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessAttachModuleObserverBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Process.attachModuleObserver });";
    assert(registry.CreateScript("process_attach_module_observer_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessAttachModuleObserverReplaysExistingModules() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string module_name = GetCurrentTestModuleName();
    const std::string source =
        "var seen = [];"
        "Process.attachModuleObserver({"
        "  onAdded: function (module) {"
        "    seen.push(module.name + ':' + String(module.base.isNull()) + ':' + String(module.path.length > 0));"
        "  }"
        "});"
        "send({"
        "  type: 'send',"
        "  payload: seen.some(function (entry) { return entry.indexOf('" + module_name + ":false:true') === 0; }) ? 'replayed' : JSON.stringify(seen)"
        "});";
    assert(registry.CreateScript("process_attach_module_observer_replay.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"replayed\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessAttachModuleObserverReceivesLoadedModuleEvent() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = [];"
        "Process.attachModuleObserver({"
        "  onAdded: function (module) {"
        "    if (module.name === 'libnative-lib.so') {"
        "      seen.push(module.name + ':' + String(module.base.isNull()) + ':' + String(module.path.indexOf('libnative-lib.so') !== -1));"
        "    }"
        "  }"
        "});"
        "rpc.exports.getseen = function () { return seen; };";
    assert(registry.CreateScript("process_attach_module_observer_loaded_event.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::string notify_error;
    const size_t installed = NotifyNativeJsHookModuleLoaded("/data/app/test/lib/arm64/libnative-lib.so",
                                                            &notify_error);
    assert(installed == 0u);
    assert(notify_error.empty());
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getseen", "[]", &result_json, &error_message));
    assert(result_json == "[\"libnative-lib.so:false:true\"]");

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessAttachModuleObserverProvidesFridaStyleModuleMethods() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string module_name = GetCurrentTestModuleName();
    const std::string source =
        "var shape = 'missing';"
        "Process.attachModuleObserver({"
        "  onAdded: function (module) {"
        "    if (module.name === '" + module_name + "') {"
        "      shape = ["
        "        typeof module.getExportByName,"
        "        typeof module.findExportByName,"
        "        typeof module.enumerateImports,"
        "        typeof module.getBaseAddress"
        "      ].join(':');"
        "    }"
        "  }"
        "});"
        "send({ type: 'send', payload: shape });";
    assert(registry.CreateScript("process_attach_module_observer_module_methods.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"function:function:function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestProcessAttachModuleObserverModuleMethodsUseInstanceReceiver() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string module_name = GetCurrentTestModuleName();
    const std::string source =
        "var outcome = 'missing';"
        "Process.attachModuleObserver({"
        "  onAdded: function (module) {"
        "    if (outcome !== 'missing' || module.name !== '" + module_name + "') {"
        "      return;"
        "    }"
        "    var address = module.getExportByName('target');"
        "    outcome = String(address !== null && address !== undefined && !address.isNull());"
        "  }"
        "});"
        "send({ type: 'send', payload: outcome });";
    assert(registry.CreateScript("process_attach_module_observer_instance_receiver.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDebugSymbolBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof DebugSymbol + ':' + typeof DebugSymbol.fromAddress });";
    assert(registry.CreateScript("debug_symbol_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"object:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDebugSymbolFromAddressResolvesKnownExport() {
    const std::string module_name = GetCurrentTestModuleName();
    const std::string symbol_name = "NookNativeFunctionSmokeAdd";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var address = Module.getGlobalExportByName('" + symbol_name + "');"
        "var info = DebugSymbol.fromAddress(address);"
        "send({"
        "  type: 'send',"
        "  payload: [typeof DebugSymbol.fromAddress, String(info.address.equals(address)), info.name, String(info.moduleName !== null && info.moduleName.toLowerCase() === '" + module_name + "'.toLowerCase()), String(info.toString().indexOf(info.name) >= 0)].join(':')"
        "});";
    assert(registry.CreateScript("debug_symbol_from_address_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"function:true:") +
               symbol_name + ":true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDebugSymbolFromAddressReturnsUnknownObjectForMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var info = DebugSymbol.fromAddress(ptr('0x1'));"
        "send({"
        "  type: 'send',"
        "  payload: [String(info.address.toString()), String(info.name === null), String(info.moduleName === null), String(info.toString().indexOf('0x1') >= 0)].join(':')"
        "});";
    assert(registry.CreateScript("debug_symbol_from_address_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"0x1:true:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadBacktraceBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({"
        "  type: 'send',"
        "  payload: [typeof Thread, typeof Thread.backtrace, String(Backtracer.ACCURATE !== undefined), String(Backtracer.FUZZY !== undefined)].join(':')"
        "});";
    assert(registry.CreateScript("thread_backtrace_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           "{\"type\":\"send\",\"payload\":\"object:function:true:true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadBacktraceAcceptsBacktracerAsFirstArgument() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var frames = Thread.backtrace(Backtracer.ACCURATE);"
        "send({"
        "  type: 'send',"
        "  payload: [String(frames.length > 0), String(frames[0].isNull())].join(':')"
        "});";
    assert(registry.CreateScript("thread_backtrace_mode_only.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadBacktraceAcceptsNoArguments() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var frames = Thread.backtrace();"
        "send({"
        "  type: 'send',"
        "  payload: [String(frames.length > 0), String(frames[0].isNull())].join(':')"
        "});";
    assert(registry.CreateScript("thread_backtrace_no_args.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestThreadBacktraceAcceptsFuzzyBacktracerAsFirstArgument() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var frames = Thread.backtrace(Backtracer.FUZZY);"
        "send({"
        "  type: 'send',"
        "  payload: [String(frames.length > 0), String(frames[0].isNull())].join(':')"
        "});";
    assert(registry.CreateScript("thread_backtrace_fuzzy_mode_only.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsThreadBacktraceFromContext() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    TestFrameRecord frame1 = {};
    frame1.previous_frame = 0u;
    frame1.saved_link_register =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddDouble));

    TestFrameRecord frame0 = {};
    frame0.previous_frame = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&frame1));
    frame0.saved_link_register =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddU64));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    var frames = Thread.backtrace(this.context, Backtracer.ACCURATE);"
        "    events.push(frames.slice(0, 4).map(function (address) {"
        "      var info = DebugSymbol.fromAddress(address);"
        "      return info.name === null ? String(address) : info.name;"
        "    }).join(','));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("thread_backtrace_from_context.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.invocation_id = 9u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 1u;
    enter_event.argument_values[0] = 0x1234u;
    enter_event.thread_id = 7u;
    enter_event.return_address =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeBoolNot));
    enter_event.stack_pointer = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&frame0));
    enter_event.frame_pointer = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&frame0));
    enter_event.link_register =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeBoolNot));
    enter_event.program_counter =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAdd));
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json ==
           "[\"NookNativeFunctionSmokeAdd,NookNativeFunctionSmokeBoolNot,"
           "NookNativeFunctionSmokeAddU64,NookNativeFunctionSmokeAddDouble\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsThreadBacktraceFuzzyUsesStackPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    uint64_t fuzzy_stack[4] = {};
    fuzzy_stack[0] = 0x1111u;
    fuzzy_stack[1] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAdd));
    fuzzy_stack[2] = 0x2222u;
    fuzzy_stack[3] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddU64));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    function names(frames, limit) {"
        "      return frames.slice(0, limit).map(function (address) {"
        "        var info = DebugSymbol.fromAddress(address);"
        "        return info.name === null ? String(address) : info.name;"
        "      });"
        "    }"
        "    events.push(JSON.stringify({"
        "      accurate: names(Thread.backtrace(this.context, Backtracer.ACCURATE), 4),"
        "      fuzzy: names(Thread.backtrace(this.context, Backtracer.FUZZY), 6)"
        "    }));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("thread_backtrace_fuzzy_from_context.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.invocation_id = 10u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 1u;
    enter_event.argument_values[0] = 0x5678u;
    enter_event.thread_id = 8u;
    enter_event.return_address =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddDouble));
    enter_event.stack_pointer = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&fuzzy_stack[0]));
    enter_event.frame_pointer = 0u;
    enter_event.link_register =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddDouble));
    enter_event.program_counter =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeBoolNot));
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json.find(
               "\\\"accurate\\\":[\\\"NookNativeFunctionSmokeBoolNot\\\","
               "\\\"NookNativeFunctionSmokeAddDouble\\\"]") != std::string::npos);
    assert(result_json.find(
               "\\\"fuzzy\\\":[\\\"NookNativeFunctionSmokeBoolNot\\\","
               "\\\"NookNativeFunctionSmokeAddDouble\\\","
               "\\\"NookNativeFunctionSmokeAdd\\\","
               "\\\"NookNativeFunctionSmokeAddU64\\\"") != std::string::npos);

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsThreadBacktraceFuzzyScansBeyondInitialWindow() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    std::vector<uint64_t> fuzzy_stack(2048u, 0u);
    fuzzy_stack[0] = 0x1111u;
    fuzzy_stack[1] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAdd));
    fuzzy_stack[1025] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddU64));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    var fuzzy = Thread.backtrace(this.context, Backtracer.FUZZY).map(function (address) {"
        "      var info = DebugSymbol.fromAddress(address);"
        "      return info.name === null ? String(address) : info.name;"
        "    });"
        "    events.push(JSON.stringify(fuzzy));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("thread_backtrace_fuzzy_long_scan.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.invocation_id = 11u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 1u;
    enter_event.argument_values[0] = 0x9999u;
    enter_event.thread_id = 9u;
    enter_event.return_address =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddDouble));
    enter_event.stack_pointer = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fuzzy_stack.data()));
    enter_event.frame_pointer = 0u;
    enter_event.link_register =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeAddDouble));
    enter_event.program_counter =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&NookNativeFunctionSmokeBoolNot));
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json.find("\\\"NookNativeFunctionSmokeAddU64\\\"") != std::string::npos);

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateModulesBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Module.enumerateModules });";
    assert(registry.CreateScript("module_enumerate_modules_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleEnumerateModulesIncludesCurrentExecutable() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var modules = Module.enumerateModules();"
        "var found = null;"
        "for (var i = 0; i < modules.length; i++) {"
        "  if (modules[i].name === '" + module_name + "') {"
        "    found = modules[i];"
        "    break;"
        "  }"
        "}"
        "send({"
        "  type: 'send',"
        "  payload: modules.length + ':' +"
        "           (found === null ? 'missing' : found.name + ':' + String(found.base.isNull()) + ':' + String(found.size > 0) + ':' + String(found.path.length > 0))"
        "});";
    assert(registry.CreateScript("module_enumerate_modules_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    const std::string prefix = "{\"type\":\"send\",\"payload\":\"";
    assert(received_json.find(prefix) == 0);
    const std::string payload = received_json.substr(prefix.size(),
                                                     received_json.size() - prefix.size() - 2);
    assert(payload.find(":missing") == std::string::npos);
    const size_t first_colon = payload.find(':');
    assert(first_colon != std::string::npos);
    assert(std::stoul(payload.substr(0, first_colon)) > 0u);
    assert(payload.find(module_name + ":false:true:true", first_colon + 1) != std::string::npos);
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleMapBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof ModuleMap });";
    assert(registry.CreateScript("module_map_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleMapResolvesCurrentExecutable() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var map = new ModuleMap();"
        "var base = Module.getBaseAddress('" + module_name + "');"
        "var found = map.find(base);"
        "var got = map.get(base);"
        "var values = map.values();"
        "send({"
        "  type: 'send',"
        "  payload: String(map.has(base)) + ':' +"
        "           (found === null ? 'missing' : found.name) + ':' +"
        "           got.name + ':' +"
        "           String(values.length > 0)"
        "});";
    assert(registry.CreateScript("module_map_hit.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"true:") +
               module_name + ":" + module_name + ":true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleMapMissBehavior() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var map = new ModuleMap();"
        "var probe = ptr('0x1');"
        "var found = map.find(probe);"
        "try {"
        "  map.get(probe);"
        "  send({ type: 'send', payload: 'unexpected-success' });"
        "} catch (e) {"
        "  send({"
        "    type: 'send',"
        "    payload: String(map.has(probe)) + ':' + String(found === null) + ':' +"
        "             (String(e).indexOf('module not found') >= 0 ? 'module-not-found' : String(e))"
        "  });"
        "}";
    assert(registry.CreateScript("module_map_miss.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"false:true:module-not-found\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleMapUpdateBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var map = new ModuleMap();"
        "send({ type: 'send', payload: typeof map.update });";
    assert(registry.CreateScript("module_map_update_exists.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleMapUpdateReturnsSameObjectAndRefreshesSnapshot() {
    const std::string module_name = GetCurrentTestModuleName();

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var map = new ModuleMap();"
        "var same = map.update() === map;"
        "var base = Module.getBaseAddress('" + module_name + "');"
        "var found = map.find(base);"
        "var values = map.values();"
        "send({"
        "  type: 'send',"
        "  payload: String(same) + ':' +"
        "           (found === null ? 'missing' : found.name) + ':' +"
        "           String(values.length > 0)"
        "});";
    assert(registry.CreateScript("module_map_update_refresh.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"true:") +
               module_name + ":true\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof NativeFunction });";
    assert(registry.CreateScript("native_function_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionRejectsUnsupportedReturnType() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "new NativeFunction(ptr('" + FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAdd)) +
        "'), 'bytes', ['int', 'int']);";
    assert(registry.CreateScript("native_function_bad_return.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("unsupported return type") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionRejectsUnsupportedArgumentType() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "new NativeFunction(ptr('" + FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAdd)) +
        "'), 'int', ['int', 'bytes']);";
    assert(registry.CreateScript("native_function_bad_arg.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("unsupported argument type") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionRejectsNonArrayArgumentTypes() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "new NativeFunction(ptr('" + FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAdd)) +
        "'), 'int', 'int');";
    assert(registry.CreateScript("native_function_non_array_args.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("argTypes must be an array") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionCallsIntAdd() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var add = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAdd)) +
        "'), 'int', ['int', 'int']);"
        "send({ type: 'send', payload: String(add(7, 35)) });";
    assert(registry.CreateScript("native_function_add.js", source.c_str(), &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"42\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionEchoesPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* probe = "0x12345678";
    const std::string source =
        "var echo = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionEchoPointer)) +
        "'), 'pointer', ['pointer']);"
        "var value = echo(ptr('" + std::string(probe) + "'));"
        "send({ type: 'send', payload: String(value) + ':' + String(value.isNull()) });";
    assert(registry.CreateScript("native_function_echo_pointer.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json ==
           std::string("{\"type\":\"send\",\"payload\":\"") + probe + ":false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionCallsVoidSink() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetNativeFunctionSinkValue() = 0u;

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var sink = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionSinkU32)) +
        "'), 'void', ['uint32']);"
        "var result = sink(305419896);"
        "send({ type: 'send', payload: typeof result });";
    assert(registry.CreateScript("native_function_void_sink.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"undefined\"}");
    assert(received_data.empty());
    assert(GetNativeFunctionSinkValue() == 305419896u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionCallsExtendedScalarTypes() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var boolNot = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionBoolNot)) +
        "'), 'bool', ['bool']);"
        "var addS16 = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAddS16)) +
        "'), 'int16', ['int16', 'int16']);"
        "var addU64 = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAddU64)) +
        "'), 'uint64', ['uint64', 'uint64']);"
        "var addFloat = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAddFloat)) +
        "'), 'float', ['float', 'float']);"
        "var addDouble = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAddDouble)) +
        "'), 'double', ['double', 'double']);"
        "var u64 = addU64(uint64('4294967296'), 10);"
        "send({ type: 'send', payload: ["
        "  typeof boolNot(true),"
        "  String(boolNot(true)),"
        "  String(addS16(-7, 35)),"
        "  u64.toString(),"
        "  addFloat(1.25, 2.5).toFixed(2),"
        "  addDouble(1.5, 2.25).toFixed(2)"
        "].join(':') });";
    assert(registry.CreateScript("native_function_extended_scalars.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"boolean:false:28:4294967306:3.75:3.75\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionCallsMixedAbiScalarTypes() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var mixU64Double = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionMixU64Double)) +
        "'), 'uint64', ['uint64', 'double']);"
        "var mixFloatU32 = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionMixFloatU32)) +
        "'), 'float', ['float', 'uint32']);"
        "var mixDoubleU32 = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionMixDoubleU32)) +
        "'), 'double', ['double', 'uint32']);"
        "send({ type: 'send', payload: ["
        "  mixU64Double(uint64('4294967296'), 2.5).toString(),"
        "  mixFloatU32(1.25, 2).toFixed(2),"
        "  mixDoubleU32(4.5, 2).toFixed(2)"
        "].join(':') });";
    assert(registry.CreateScript("native_function_mixed_scalars.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"4294967321:3.25:6.75\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionRejectsWrongArgumentCount() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var add = new NativeFunction(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionAdd)) +
        "'), 'int', ['int', 'int']);"
        "add(1);";
    assert(registry.CreateScript("native_function_bad_arity.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("wrong number of arguments") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackBindingExists() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof NativeCallback });";
    assert(registry.CreateScript("native_callback_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackRejectsNonFunction() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "new NativeCallback(123, 'uint32', ['uint32']);";
    assert(registry.CreateScript("native_callback_non_function.js", source, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("NativeCallback first argument must be a function") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackRejectsUnsupportedReturnType() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "new NativeCallback(function () { return 1; }, 'bytes', ['uint32']);";
    assert(registry.CreateScript("native_callback_bad_return.js", source, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("NativeCallback unsupported return type") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackRejectsUnsupportedArgumentType() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "new NativeCallback(function (value) { return value; }, 'uint32', ['bytes']);";
    assert(registry.CreateScript("native_callback_bad_arg.js", source, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("NativeCallback unsupported argument type") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackRoundtripUInt32() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var cb = new NativeCallback(function (left, right) { return left + right; }, 'uint32', ['uint32', 'uint32']);"
        "var invoke = new NativeFunction(cb, 'uint32', ['uint32', 'uint32']);"
        "send({ type: 'send', payload: String(invoke(7, 35)) });";
    assert(registry.CreateScript("native_callback_roundtrip_u32.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"42\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackRoundtripPointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var cb = new NativeCallback(function (value) { return value; }, 'pointer', ['pointer']);"
        "var invoke = new NativeFunction(cb, 'pointer', ['pointer']);"
        "var value = invoke(ptr('0x12345678'));"
        "send({ type: 'send', payload: String(value) + ':' + String(value.isNull()) });";
    assert(registry.CreateScript("native_callback_roundtrip_pointer.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0x12345678:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackRoundtripVoid() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var seen = 0;"
        "var cb = new NativeCallback(function (value) { seen = value; }, 'void', ['uint32']);"
        "var invoke = new NativeFunction(cb, 'void', ['uint32']);"
        "var result = invoke(305419896);"
        "send({ type: 'send', payload: typeof result + ':' + String(seen) });";
    assert(registry.CreateScript("native_callback_roundtrip_void.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"undefined:305419896\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackRoundtripExtendedScalarTypes() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var boolCb = new NativeCallback(function (value) { return !value; }, 'bool', ['bool']);"
        "var boolInvoke = new NativeFunction(boolCb, 'bool', ['bool']);"
        "var u64Cb = new NativeCallback(function (left, right) {"
        "  return (left.toString() === '4294967296' && right === 9) ? uint64('4294967305') : uint64('0');"
        "}, 'uint64', ['uint64', 'uint32']);"
        "var u64Invoke = new NativeFunction(u64Cb, 'uint64', ['uint64', 'uint32']);"
        "var floatCb = new NativeCallback(function (left, right) { return left + right; }, 'float', ['float', 'float']);"
        "var floatInvoke = new NativeFunction(floatCb, 'float', ['float', 'float']);"
        "var doubleCb = new NativeCallback(function (left, right) { return left + right; }, 'double', ['double', 'double']);"
        "var doubleInvoke = new NativeFunction(doubleCb, 'double', ['double', 'double']);"
        "send({ type: 'send', payload: ["
        "  typeof boolInvoke(true),"
        "  String(boolInvoke(true)),"
        "  u64Invoke(uint64('4294967296'), 9).toString(),"
        "  floatInvoke(1.25, 2.5).toFixed(2),"
        "  doubleInvoke(1.5, 2.25).toFixed(2)"
        "].join(':') });";
    assert(registry.CreateScript("native_callback_extended_scalars.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"boolean:false:4294967305:3.75:3.75\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeCallbackCleanupOnUnload() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var cb = new NativeCallback(function () { return 1; }, 'uint32', []);"
        "rpc.exports.getcallback = function () { return String(cb); };";
    assert(registry.CreateScript("native_callback_cleanup.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeGetNativeCallbackCountForTesting(script_id) == 1u);
    assert(registry.UnloadScript(script_id, &error_message));
    assert(JsRuntimeGetNativeCallbackCountForTesting(script_id) == 0u);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceAndRevertBindingsExist() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "send({ type: 'send', payload: typeof Interceptor.replace + ':' + typeof Interceptor.revert });";
    assert(registry.CreateScript("interceptor_replace_exists.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"function:function\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceRejectsInvalidTarget() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var cb = new NativeCallback(function (left, right) { return left + right; }, 'uint32', ['uint32', 'uint32']);"
        "Interceptor.replace('bad-target', cb);";
    assert(registry.CreateScript("interceptor_replace_bad_target.js", source, &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Interceptor.replace target must be a non-zero pointer value") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceRejectsNonNativeCallbackReplacement() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "Interceptor.replace(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "'), ptr('0x1234'));";
    assert(registry.CreateScript("interceptor_replace_bad_replacement.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Interceptor.replace replacement must be a NativeCallback") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceChangesBehaviorAndRevertRestoresIt() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetReplaceThunk() = nullptr;
    GetReplaceHookHandle() = nullptr;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeReplaceInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeReplaceInlineHookUnhookInvoker);

    assert(TestNativeFunctionReplaceableAdd(7, 35) == 42u);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var target = ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "');"
        "var replacement = new NativeCallback(function (left, right) { return left + right + 1; }, 'uint32', ['uint32', 'uint32']);"
        "Interceptor.replace(target, replacement);";
    assert(registry.CreateScript("interceptor_replace_behavior.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(TestNativeFunctionReplaceableAdd(7, 35) == 43u);

    const std::string revert_source =
        "Interceptor.revert(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "'));";
    uint32_t revert_script_id = 0;
    assert(registry.CreateScript("interceptor_revert_behavior.js",
                                 revert_source.c_str(),
                                 &revert_script_id,
                                 &error_message));
    assert(registry.LoadScript(revert_script_id, &error_message));
    assert(TestNativeFunctionReplaceableAdd(7, 35) == 42u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestNativeFunctionInvokeBypassesReplacedTargetBody() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetReplaceThunk() = nullptr;
    GetReplaceHookHandle() = nullptr;
    GetReplaceableAddEntryCount() = 0u;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeReplaceInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeReplaceInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var target = ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "');"
        "var add = new NativeFunction(target, 'uint32', ['uint32', 'uint32']);"
        "var replacement = new NativeCallback(function (left, right) { return left + right + 1; }, 'uint32', ['uint32', 'uint32']);"
        "Interceptor.replace(target, replacement);"
        "send({ type: 'send', payload: String(add(7, 35)) });";

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    assert(registry.CreateScript("native_function_replace_direct_dispatch.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"43\"}");
    assert(received_data.empty());
    assert(GetReplaceableAddEntryCount() == 0u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceAcceptsNativeFunctionTargetAndPlainJsReplacement() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetReplaceThunk() = nullptr;
    GetReplaceHookHandle() = nullptr;
    GetReplaceableAddEntryCount() = 0u;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeReplaceInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeReplaceInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var target = ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "');"
        "var add = new NativeFunction(target, 'uint32', ['uint32', 'uint32']);"
        "Interceptor.replace(add, function (left, right) { return left + right + 1; });"
        "send({ type: 'send', payload: String(add(7, 35)) });";

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    assert(registry.CreateScript("interceptor_replace_native_function_target.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"43\"}");
    assert(received_data.empty());
    assert(GetReplaceableAddEntryCount() == 0u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceExposesOriginalOnNativeFunctionTarget() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetReplaceThunk() = nullptr;
    GetReplaceHookHandle() = nullptr;
    GetReplaceableAddEntryCount() = 0u;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeReplaceInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeReplaceInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var target = ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "');"
        "var add = new NativeFunction(target, 'uint32', ['uint32', 'uint32']);"
        "Interceptor.replace(add, function (left, right) { return add.original(left, right) + 1; });"
        "send({ type: 'send', payload: String(add(7, 35)) + ':' + typeof add.original });";

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    assert(registry.CreateScript("interceptor_replace_original_property.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"43:function\"}");
    assert(received_data.empty());
    assert(GetReplaceableAddEntryCount() == 0u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceRejectsDuplicateTarget() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetReplaceThunk() = nullptr;
    GetReplaceHookHandle() = nullptr;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeReplaceInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeReplaceInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var target = ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "');"
        "var replacement = new NativeCallback(function (left, right) { return left + right + 1; }, 'uint32', ['uint32', 'uint32']);"
        "Interceptor.replace(target, replacement);"
        "Interceptor.replace(target, replacement);";
    assert(registry.CreateScript("interceptor_replace_duplicate.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Interceptor.replace target already replaced") != std::string::npos);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorRevertRejectsMissingTarget() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "Interceptor.revert(ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "'));";
    assert(registry.CreateScript("interceptor_revert_missing.js", source.c_str(), &script_id, &error_message));
    assert(!registry.LoadScript(script_id, &error_message));
    assert(error_message.find("Interceptor.revert target is not replaced") != std::string::npos);

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorReplaceCleanupOnUnloadRestoresBehavior() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    GetReplaceThunk() = nullptr;
    GetReplaceHookHandle() = nullptr;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeReplaceInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeReplaceInlineHookUnhookInvoker);

    assert(TestNativeFunctionReplaceableAdd(7, 35) == 42u);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var target = ptr('" +
        FormatTestPointer(reinterpret_cast<void*>(&TestNativeFunctionReplaceableAdd)) +
        "');"
        "var replacement = new NativeCallback(function (left, right) { return left + right + 1; }, 'uint32', ['uint32', 'uint32']);"
        "Interceptor.replace(target, replacement);";
    assert(registry.CreateScript("interceptor_replace_cleanup.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(TestNativeFunctionReplaceableAdd(7, 35) == 43u);
    assert(registry.UnloadScript(script_id, &error_message));
    assert(TestNativeFunctionReplaceableAdd(7, 35) == 42u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindExportByNameReturnsNativePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var address = Module.findExportByName('libdemo.so', 'target');"
        "send({"
        "  type: 'send',"
        "  payload: String(address) + ':' + String(address.add(4)) + ':' + String(address.isNull())"
        "});";
    assert(registry.CreateScript("module_find_export_native_pointer.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0x10000000:0x10000004:false\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindExportByNameReturnsNullOnMiss() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var address = Module.findExportByName('libdemo.so', 'missing');"
        "send({ type: 'send', payload: String(address === null) });";
    assert(registry.CreateScript("module_find_export_miss.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"true\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindExportByNameWithNullModuleUsesGlobalLookup() {
#if defined(_WIN32)
    const std::string symbol_name = "GetCurrentProcessId";
#else
    const std::string symbol_name = "NookNativeFunctionSmokeAdd";
#endif

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var address = Module.findExportByName(null, '" + symbol_name + "');"
        "send({ type: 'send', payload: [String(address === null), String(address !== null && address.isNull())].join(':') });";
    assert(registry.CreateScript("module_find_export_null_module_global.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"false:false\"}");
    assert(received_data.empty());

    registry.Clear();
    JsRuntime::Shutdown();
}

void TestModuleFindExportByNameStillReturnsAddressForUnsafeInlineHookSymbol() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);
    SetNativeJsInlineHookSymbolSafetyCheckerForTesting(&UnsafeInlineHookSymbolChecker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var address = Module.findExportByName('libc.so', 'strcmp');"
        "send({ type: 'send', payload: String(address) });";
    assert(registry.CreateScript("module_find_export_unsafe_resolves.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"0x10000000\"}");
    assert(received_data.empty());

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookSymbolSafetyCheckerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachRegistersCallbacksByAddress() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_address.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachAcceptsNativePointer() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var target = ptr('0x20000000');"
        "var result = Interceptor.attach(target, {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_native_pointer.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachAcceptsModuleAndSymbolOptions() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Interceptor.attach({ module: 'libdemo.so', symbol: 'target' }, {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_module_symbol.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachPassesSnapshotConfigToInstaller() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    GetLastSnapshotRequests().clear();
    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach({ module: 'libdemo.so', symbol: 'target' }, {"
        "  snapshot: [{ index: 2, type: 'jstringUtf8' }],"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("interceptor_attach_snapshot_config.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(GetLastSnapshotRequests().size() == 1u);
    assert(GetLastSnapshotRequests()[0].type == "jstringUtf8");
    assert(GetLastSnapshotRequests()[0].argument_index == 2u);
    assert(GetLastSnapshotRequests()[0].env_index == 0u);

    registry.Clear();
    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachAcceptsDeferredModuleAndSymbolOptions() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Interceptor.attach({"
        "  module: 'libnative-lib.so',"
        "  symbol: 'Java_com_demo_target_LoginFragment_verifyPasswordNative'"
        "}, {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_module_symbol_deferred.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:true\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 0);

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachAcceptsSingleCallback() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* on_enter_only =
        "var result = Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_on_enter_only.js",
                                 on_enter_only,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    {
        NativeJsHookRecord record = {};
        assert(GetNativeJsHookRecordForTesting(1u, &record));
        assert(record.blocking);
    }

    registry.Clear();
    ResetNativeJsHookRegistryForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);
    received_json.clear();

    const char* on_leave_only =
        "var result = Interceptor.attach('0x30000000', {"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_on_leave_only.js",
                                 on_leave_only,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    {
        NativeJsHookRecord record = {};
        assert(GetNativeJsHookRecordForTesting(1u, &record));
        assert(record.blocking);
    }

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachAcceptsNonblockingOption() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Interceptor.attach('0x20000000', {"
        "  blocking: false,"
        "  onEnter: function(args) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_nonblocking.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    {
        NativeJsHookRecord record = {};
        assert(GetNativeJsHookRecordForTesting(1u, &record));
        assert(!record.blocking);
    }

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorAttachAcceptsBlockingOption() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var result = Interceptor.attach('0x20000000', {"
        "  blocking: true,"
        "  onEnter: function(args) {}"
        "});"
        "send({ type: 'send', payload: String(result.hookId) + ':' + String(result.deferred) });";
    assert(registry.CreateScript("interceptor_attach_blocking.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:false\"}");
    assert(received_data.empty());
    {
        NativeJsHookRecord record = {};
        assert(GetNativeJsHookRecordForTesting(1u, &record));
        assert(record.blocking);
    }

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorDetachRemovesOnlyRequestedHook() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var first = Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "var second = Interceptor.attach('0x30000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({"
        "  type: 'send',"
        "  payload: String(first.hookId) + ':' + String(second.hookId) + ':' +"
        "           String(Interceptor.detach(first.hookId))"
        "});";
    assert(registry.CreateScript("interceptor_detach_one.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"1:2:true\"}");
    assert(received_data.empty());
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 2u));
    assert(GetInstalledNativeJsHookCountForTesting() == 1u);
    assert(GetInlineHookUnhookCallCount() == 1);

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 2);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorDetachAllOnlyRemovesCurrentScriptHooks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t first_script_id = 0;
    const char* first_source =
        "var keep = Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "rpc.exports.gethookid = function() { return keep.hookId; };";
    assert(registry.CreateScript("interceptor_detach_all_keep.js",
                                 first_source,
                                 &first_script_id,
                                 &error_message));
    assert(registry.LoadScript(first_script_id, &error_message));

    std::string received_json;
    std::vector<uint8_t> received_data;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        received_json = json;
        received_data = data;
        return true;
    });

    uint32_t second_script_id = 0;
    const char* second_source =
        "Interceptor.attach('0x30000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "Interceptor.attach('0x40000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "send({ type: 'send', payload: String(Interceptor.detachAll()) });";
    assert(registry.CreateScript("interceptor_detach_all_current.js",
                                 second_source,
                                 &second_script_id,
                                 &error_message));
    assert(registry.LoadScript(second_script_id, &error_message));

    assert(received_json == "{\"type\":\"send\",\"payload\":\"2\"}");
    assert(received_data.empty());
    assert(JsRuntimeHasNativeHookCallbacksForTesting(first_script_id, 1u));
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(second_script_id, 2u));
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(second_script_id, 3u));
    assert(GetInstalledNativeJsHookCountForTesting() == 1u);
    assert(GetInlineHookUnhookCallCount() == 2);

    assert(registry.UnloadScript(second_script_id, &error_message));
    assert(registry.UnloadScript(first_script_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 3);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorListenerDetachWorksFromRpcCallback() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var listener = Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "rpc.exports.unhook = function() {"
        "  return listener.detach();"
        "};";
    assert(registry.CreateScript("interceptor_listener_detach_rpc.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "unhook", "[]", &result_json, &error_message));
    assert(result_json == "true");
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInlineHookUnhookCallCount() == 1);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorListenerDetachWorksFromPostCallback() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::string received_json;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        received_json = json;
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var listener = Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});"
        "recv(function(message) {"
        "  send({ type: 'send', payload: 'post-detach:' + String(listener.detach()) });"
        "});";
    assert(registry.CreateScript("interceptor_listener_detach_post.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    assert(JsRuntime::DispatchMessage(script_id,
                                      "{\"type\":\"post\",\"payload\":\"detach\"}",
                                      {},
                                      &error_message));
    assert(received_json == "{\"type\":\"send\",\"payload\":\"post-detach:true\"}");
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInlineHookUnhookCallCount() == 1);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorListenerDetachWorksFromOnEnterCallback() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookUnhookCallCount() = 0;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "var listener = Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {"
        "    events.push('enter');"
        "    listener.detach();"
        "  },"
        "  onLeave: function(retval) { events.push('leave'); }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("interceptor_listener_detach_on_enter.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));
    assert(JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 0u;
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(!JsRuntimeHasNativeHookCallbacksForTesting(script_id, 1u));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCount() == 1);

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"enter\"]");

    assert(registry.UnloadScript(script_id, &error_message));
    assert(GetInlineHookUnhookCallCount() == 1);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsInvokesMatchingCallbacks() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) { events.push('enter:' + args[0]); },"
        "  onLeave: function(retval) { events.push('leave:' + retval); }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_events.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 1u;
    enter_event.argument_values[0] = 0x1234u;
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    HookEvent leave_event = {};
    leave_event.hook_id = 1u;
    leave_event.phase = HookEventPhase::kLeave;
    leave_event.return_value = 0x5678u;
    assert(EnqueueNativeJsHookEvent(leave_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"enter:0x1234\",\"leave:0x5678\"]");

    registry.Clear();
    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsExposeNativePointerArgsAndReturnValue() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    events.push(String(args[0]) + ':' + String(args[0].add(4)) + ':' + String(args[1].isNull()));"
        "  },"
        "  onLeave: function(retval) {"
        "    events.push(String(retval) + ':' + String(retval.add(1)) + ':' + String(retval.isNull()));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_pointer_events.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 2u;
    enter_event.argument_values[0] = 0x1234u;
    enter_event.argument_values[1] = 0u;
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    HookEvent leave_event = {};
    leave_event.hook_id = 1u;
    leave_event.phase = HookEventPhase::kLeave;
    leave_event.return_value = 0x5678u;
    assert(EnqueueNativeJsHookEvent(leave_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"0x1234:0x1238:true\",\"0x5678:0x5679:false\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsExposeJniUtf8Snapshots() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libnative-lib.so',"
        "  symbol: 'Java_com_demo_target_LoginFragment_verifyPasswordNative',"
        "  onEnter: function(args) {"
        "    events.push(String(args[2]) + ':' + String(args[2].$jniUtf8));"
        "  },"
        "  onLeave: function(retval) {}"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_jni_snapshot.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 3u;
    enter_event.argument_values[0] = 0x1111u;
    enter_event.argument_values[1] = 0x2222u;
    enter_event.argument_values[2] = 0x3333u;
    enter_event.jni_utf8_snapshot_count = 1u;
    enter_event.jni_utf8_snapshots[0].argument_index = 2u;
    enter_event.jni_utf8_snapshots[0].property_name = "$jniUtf8";
    enter_event.jni_utf8_snapshots[0].utf8 = "secret";
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"0x3333:secret\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsExposeCStringUtf8Snapshots() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    events.push(String(args[0]) + ':' + String(args[0].$utf8));"
        "  },"
        "  onLeave: function(retval) {}"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_cstring_snapshot.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 1u;
    enter_event.argument_values[0] = 0x4444u;
    enter_event.jni_utf8_snapshot_count = 1u;
    enter_event.jni_utf8_snapshots[0].argument_index = 0u;
    enter_event.jni_utf8_snapshots[0].property_name = "$utf8";
    enter_event.jni_utf8_snapshots[0].utf8 = "hello-cstr";
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"0x4444:hello-cstr\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationMemoryReadUtf8StringWorksInOnEnter() {
    static const char kLeft[] = "Hello from hook";
    static const char kRight[] = "flag-value";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var events = [];"
        "Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {"
        "    var left = Memory.readUtf8String(args[0]);"
        "    var right = Memory.readUtf8String(args[1]);"
        "    events.push(left + ':' + right);"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("interceptor_memory_read_utf8_in_on_enter.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = reinterpret_cast<uint64_t>(kLeft);
    arguments[1] = reinterpret_cast<uint64_t>(kRight);
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"Hello from hook:flag-value\"]");

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationMemoryReadUtf8StringUsesCachedMappingsAndPointerMetadata() {
    static const char kLeft[] = "Hello from hook";
    static const char kRight[] = "flag-value";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var events = [];"
        "Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {"
        "    var leftA = Memory.readUtf8String(args[0]);"
        "    var leftB = Memory.readUtf8String(args[0], 5);"
        "    var right = Memory.readUtf8String(args[1]);"
        "    events.push(leftA + ':' + leftB + ':' + right);"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("interceptor_memory_read_utf8_cached_mappings.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    JsRuntimeResetReadableMappingLookupCountForTesting();

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = reinterpret_cast<uint64_t>(kLeft);
    arguments[1] = reinterpret_cast<uint64_t>(kRight);
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"Hello from hook:Hello:flag-value\"]");
    assert(JsRuntimeGetReadableMappingLookupCountForTesting() <= 1u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHighFrequencyHookStillDeliversInitialOnEnter() {
    static const char kLeft[] = "Hello from hook";
    static const char kRight[] = "flag-value";

    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const std::string source =
        "var events = [];"
        "Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {"
        "    var left = Memory.readUtf8String(args[0]);"
        "    if (left.indexOf('Hello') !== -1) {"
        "      events.push(left);"
        "    }"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("interceptor_high_frequency_initial_on_enter.js",
                                 source.c_str(),
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = reinterpret_cast<uint64_t>(kLeft);
    arguments[1] = reinterpret_cast<uint64_t>(kRight);
    uint64_t return_value = 0u;
    for (size_t i = 0; i < 64u; ++i) {
        std::atomic<bool> done = false;
        std::thread invoke_thread([&]() {
            assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
            done.store(true);
        });
        DriveNativeHookDispatchUntil(done);
        invoke_thread.join();
        assert(done.load());
    }

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json.find("Hello from hook") != std::string::npos);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsExposeInvocationContextAndSharedThis() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    this.note = 'persisted';"
        "    events.push('enter:' +"
        "      String(this.threadId) + ':' +"
        "      String(this.returnAddress) + ':' +"
        "      String(this.context.x0) + ':' +"
        "      String(this.context.sp) + ':' +"
        "      String(this.context.pc));"
        "  },"
        "  onLeave: function(retval) {"
        "    events.push('leave:' +"
        "      String(this.note) + ':' +"
        "      String(this.threadId) + ':' +"
        "      String(this.context.lr) + ':' +"
        "      String(this.context.pc) + ':' +"
        "      String(retval));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_invocation_context.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.invocation_id = 7u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 1u;
    enter_event.argument_values[0] = 0x1234u;
    enter_event.thread_id = 99u;
    enter_event.return_address = 0x8888u;
    enter_event.stack_pointer = 0x7770u;
    enter_event.frame_pointer = 0x7760u;
    enter_event.link_register = 0x8899u;
    enter_event.program_counter = 0x9999u;
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    HookEvent leave_event = {};
    leave_event.hook_id = 1u;
    leave_event.invocation_id = 7u;
    leave_event.phase = HookEventPhase::kLeave;
    leave_event.return_value = 0x5678u;
    leave_event.thread_id = 99u;
    leave_event.return_address = 0x8888u;
    leave_event.stack_pointer = 0x7770u;
    leave_event.frame_pointer = 0x7760u;
    leave_event.link_register = 0x8899u;
    leave_event.program_counter = 0x9999u;
    assert(EnqueueNativeJsHookEvent(leave_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json ==
           "[\"enter:99:0x8888:0x1234:0x7770:0x9999\","
           "\"leave:persisted:99:0x8899:0x9999:0x5678\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsReuseInvocationWrappers() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "var enterThis = null;"
        "var enterArgs = null;"
        "var leaveThis = null;"
        "var leaveRetval = null;"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    if (enterThis === null) {"
        "      enterThis = this;"
        "      enterArgs = args;"
        "      events.push('enter:first');"
        "    } else {"
        "      events.push('enter:reuse:' + String(enterThis === this) + ':' + String(enterArgs === args));"
        "    }"
        "  },"
        "  onLeave: function(retval) {"
        "    if (leaveThis === null) {"
        "      leaveThis = this;"
        "      leaveRetval = retval;"
        "      events.push('leave:first');"
        "    } else {"
        "      events.push('leave:reuse:' + String(leaveThis === this) + ':' + String(leaveRetval === retval));"
        "    }"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_wrapper_reuse.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event_first = {};
    enter_event_first.hook_id = 1u;
    enter_event_first.invocation_id = 1u;
    enter_event_first.phase = HookEventPhase::kEnter;
    enter_event_first.argument_count = 1u;
    enter_event_first.argument_values[0] = 0x1234u;
    assert(EnqueueNativeJsHookEvent(enter_event_first, &error_message));

    HookEvent leave_event_first = {};
    leave_event_first.hook_id = 1u;
    leave_event_first.invocation_id = 1u;
    leave_event_first.phase = HookEventPhase::kLeave;
    leave_event_first.return_value = 0x5678u;
    assert(EnqueueNativeJsHookEvent(leave_event_first, &error_message));

    HookEvent enter_event_second = {};
    enter_event_second.hook_id = 1u;
    enter_event_second.invocation_id = 2u;
    enter_event_second.phase = HookEventPhase::kEnter;
    enter_event_second.argument_count = 1u;
    enter_event_second.argument_values[0] = 0x9999u;
    assert(EnqueueNativeJsHookEvent(enter_event_second, &error_message));

    HookEvent leave_event_second = {};
    leave_event_second.hook_id = 1u;
    leave_event_second.invocation_id = 2u;
    leave_event_second.phase = HookEventPhase::kLeave;
    leave_event_second.return_value = 0xaaaau;
    assert(EnqueueNativeJsHookEvent(leave_event_second, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json ==
           "[\"enter:first\",\"leave:first\","
           "\"enter:reuse:true:true\",\"leave:reuse:true:true\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsClearsSharedThisBetweenInvocations() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "var count = 0;"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    count++;"
        "    events.push('enter:' + count + ':' + String(this.note));"
        "    this.note = 'n' + count;"
        "  },"
        "  onLeave: function(retval) {"
        "    events.push('leave:' + count + ':' + String(this.note));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_clear_shared_this.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event_first = {};
    enter_event_first.hook_id = 1u;
    enter_event_first.invocation_id = 1u;
    enter_event_first.phase = HookEventPhase::kEnter;
    enter_event_first.argument_count = 1u;
    enter_event_first.argument_values[0] = 0x1111u;
    assert(EnqueueNativeJsHookEvent(enter_event_first, &error_message));

    HookEvent leave_event_first = {};
    leave_event_first.hook_id = 1u;
    leave_event_first.invocation_id = 1u;
    leave_event_first.phase = HookEventPhase::kLeave;
    leave_event_first.return_value = 0x2222u;
    assert(EnqueueNativeJsHookEvent(leave_event_first, &error_message));

    HookEvent enter_event_second = {};
    enter_event_second.hook_id = 1u;
    enter_event_second.invocation_id = 2u;
    enter_event_second.phase = HookEventPhase::kEnter;
    enter_event_second.argument_count = 1u;
    enter_event_second.argument_values[0] = 0x3333u;
    assert(EnqueueNativeJsHookEvent(enter_event_second, &error_message));

    HookEvent leave_event_second = {};
    leave_event_second.hook_id = 1u;
    leave_event_second.invocation_id = 2u;
    leave_event_second.phase = HookEventPhase::kLeave;
    leave_event_second.return_value = 0x4444u;
    assert(EnqueueNativeJsHookEvent(leave_event_second, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json ==
           "[\"enter:1:undefined\",\"leave:1:n1\","
           "\"enter:2:undefined\",\"leave:2:n2\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInvokeInstalledNativeHookDoesNotDependOnBridgeMutexAfterInstall() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookInvokerCapture() = {};
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvokerCaptureReplacement);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  blocking: false,"
        "  onEnter: function(args) {},"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("native_attach_bridge_mutex_independent.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    using ReplacementEntry = uint64_t (*)(uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t);
    auto replacement =
        reinterpret_cast<ReplacementEntry>(GetInlineHookInvokerCapture().replacement);
    assert(replacement != nullptr);

    const auto start = std::chrono::steady_clock::now();
    uint64_t return_value = 0u;
    RunWithInlineHookBridgeMutexHeldForTesting([&]() {
        return_value = replacement(1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u);
    });
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    assert(return_value == 0x4242u);
    assert(elapsed_ms < 50);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsReuseInvocationContextPointers() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "var firstReturnAddress = null;"
        "var firstX0 = null;"
        "var firstPc = null;"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) {"
        "    if (firstReturnAddress === null) {"
        "      firstReturnAddress = this.returnAddress;"
        "      firstX0 = this.context.x0;"
        "      firstPc = this.context.pc;"
        "      events.push('first:' + String(this.returnAddress) + ':' + String(this.context.x0));"
        "    } else {"
        "      events.push('reuse:' +"
        "        String(firstReturnAddress === this.returnAddress) + ':' +"
        "        String(firstX0 === this.context.x0) + ':' +"
        "        String(firstPc === this.context.pc) + ':' +"
        "        String(this.returnAddress) + ':' +"
        "        String(this.context.x0));"
        "    }"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_context_pointer_reuse.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event_first = {};
    enter_event_first.hook_id = 1u;
    enter_event_first.invocation_id = 1u;
    enter_event_first.phase = HookEventPhase::kEnter;
    enter_event_first.argument_count = 1u;
    enter_event_first.argument_values[0] = 0x1234u;
    enter_event_first.return_address = 0x8888u;
    enter_event_first.program_counter = 0x9999u;
    assert(EnqueueNativeJsHookEvent(enter_event_first, &error_message));

    HookEvent enter_event_second = {};
    enter_event_second.hook_id = 1u;
    enter_event_second.invocation_id = 2u;
    enter_event_second.phase = HookEventPhase::kEnter;
    enter_event_second.argument_count = 1u;
    enter_event_second.argument_values[0] = 0x7777u;
    enter_event_second.return_address = 0xaaaau;
    enter_event_second.program_counter = 0xbbbbu;
    assert(EnqueueNativeJsHookEvent(enter_event_second, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json ==
           "[\"first:0x8888:0x1234\","
           "\"reuse:true:true:true:0xaaaa:0x7777\"]");

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationIgnoresCorruptedBaselineMetadata() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvokerCaptureX0);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var count = 0;"
        "var events = [];"
        "Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) {"
        "    count++;"
        "    if (count === 1) {"
        "      this.context.__nookContextX0Baseline = 'broken';"
        "    }"
        "    events.push('enter:' + String(args[0]));"
        "  },"
        "  onLeave: function(retval) {}"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("interceptor_corrupted_baseline_metadata.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;

    uint64_t return_value = 0u;
    std::atomic<bool> first_done = false;
    std::thread first_invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        first_done.store(true);
    });
    DriveNativeHookDispatchUntil(first_done);
    first_invoke_thread.join();
    assert(first_done.load());

    std::atomic<bool> second_done = false;
    std::thread second_invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        second_done.store(true);
    });
    DriveNativeHookDispatchUntil(second_done);
    second_invoke_thread.join();
    assert(second_done.load());

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"enter:0x1234\",\"enter:0x1234\"]");
    for (const std::string& message : sent_messages) {
        assert(message.find("baseline pointer value is invalid") == std::string::npos);
        assert(message.find("native hook onEnter callback failed") == std::string::npos);
    }

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationCanOverrideArgumentRegisters() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetLastInlineHookOriginalX0() = 0u;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvokerCaptureX0);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) { this.context.x0 = ptr('0x7777'); },"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("interceptor_context_writeback_enter.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    assert(done.load());
    assert(GetLastInlineHookOriginalX0() == 0x7777u);
    assert(return_value == 0x7778u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationCanOverrideArgumentRegistersWithArgsReplace() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetLastInlineHookOriginalX0() = 0u;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvokerCaptureX0);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  onEnter: function(args) { args[0].replace(ptr('0x8888')); },"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("interceptor_args_replace_enter.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    assert(done.load());
    assert(GetLastInlineHookOriginalX0() == 0x8888u);
    assert(return_value == 0x8889u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationCanOverrideReturnValue() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  onLeave: function(retval) { retval.replace(ptr('0x9999')); }"
        "});";
    assert(registry.CreateScript("interceptor_retval_replace.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    assert(done.load());
    assert(return_value == 0x9999u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationCanOverrideReturnValueWithNumber() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  onLeave: function(retval) { retval.replace(1); }"
        "});";
    assert(registry.CreateScript("interceptor_retval_replace_number.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    assert(done.load());
    assert(return_value == 0x1u);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationPreservesHighPointerPrecisionAfterReplace() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Interceptor.attach('0x20000000', {"
        "  onLeave: function(retval) {"
        "    retval.replace(ptr('0x1234567890abc123'));"
        "    events.push(String(retval));"
        "    events.push(String(this.context.x0));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("interceptor_retval_replace_high_pointer_precision.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    assert(done.load());
    assert(return_value == 0x1234567890abc123ull);

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"0x1234567890abc123\",\"0x1234567890abc123\"]");

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationUpdatesRetvalAndContextViewAfterReplace() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Interceptor.attach('0x20000000', {"
        "  onLeave: function(retval) {"
        "    events.push('before:' + String(retval) + ':' + String(this.context.x0));"
        "    retval.replace(1);"
        "    events.push('after:' + String(retval) + ':' + String(this.context.x0));"
        "  }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("interceptor_retval_replace_updates_view.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    assert(done.load());
    assert(return_value == 0x1u);

    std::string result_json;
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"before:0x4242:0x4242\",\"after:0x1:0x1\"]");

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorHookInvocationSkipsMissingPhaseCallback() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  onLeave: function(retval) { retval.replace(ptr('0x9999')); }"
        "});";
    assert(registry.CreateScript("interceptor_retval_replace_on_leave_only.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    std::atomic<bool> done = false;
    std::thread invoke_thread([&]() {
        assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
        done.store(true);
    });
    DriveNativeHookDispatchUntil(done);
    invoke_thread.join();

    assert(done.load());
    assert(return_value == 0x9999u);
    for (const std::string& message : sent_messages) {
        assert(message.find("not a function") == std::string::npos);
    }

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorNonblockingArgsReplaceWarnsAndDoesNotMutate() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetLastInlineHookOriginalX0() = 0u;
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvokerCaptureX0);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  blocking: false,"
        "  onEnter: function(args) { args[0].replace(ptr('0x8888')); },"
        "  onLeave: function(retval) {}"
        "});";
    assert(registry.CreateScript("interceptor_args_replace_nonblocking_warn.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
    for (size_t index = 0; index < 200u && sent_messages.empty(); ++index) {
        assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(GetLastInlineHookOriginalX0() == 0x1234u);
    assert(return_value == 0x1235u);
    assert(!sent_messages.empty());
    assert(sent_messages.back().find("\"type\":\"log\"") != std::string::npos);
    assert(sent_messages.back().find("\"level\":\"warn\"") != std::string::npos);
    assert(sent_messages.back().find("argument mutation ignored in observer mode") != std::string::npos);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestInterceptorNonblockingRetvalReplaceWarnsAndDoesNotMutate() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));

    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "Interceptor.attach('0x20000000', {"
        "  blocking: false,"
        "  onLeave: function(retval) { retval.replace(ptr('0x9999')); }"
        "});";
    assert(registry.CreateScript("interceptor_retval_replace_nonblocking_warn.js",
                                 source,
                                 &script_id,
                                 &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    std::array<uint64_t, 8> arguments = {};
    arguments[0] = 0x1234u;
    uint64_t return_value = 0u;
    assert(InvokeInstalledNativeJsHookForTesting(1u, arguments, &return_value));
    for (size_t index = 0; index < 200u && sent_messages.empty(); ++index) {
        assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(return_value == 0x4242u);
    assert(!sent_messages.empty());
    assert(sent_messages.back().find("\"type\":\"log\"") != std::string::npos);
    assert(sent_messages.back().find("\"level\":\"warn\"") != std::string::npos);
    assert(sent_messages.back().find("return mutation ignored in observer mode") != std::string::npos);

    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsInlineHookUnhookInvokerForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsContinuesAfterCallbackException() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;
    JsRuntimeSetNativeHookInstallerDependenciesForTesting(dependencies);

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    ScriptRegistry registry;
    uint32_t script_id = 0;
    const char* source =
        "var events = [];"
        "Nook.Native.attach({"
        "  type: 'inline',"
        "  module: 'libdemo.so',"
        "  symbol: 'target',"
        "  onEnter: function(args) { throw new Error('boom'); },"
        "  onLeave: function(retval) { events.push('leave:' + retval); }"
        "});"
        "rpc.exports.getevents = function() { return events; };";
    assert(registry.CreateScript("native_attach_dispatch_exception.js", source, &script_id, &error_message));
    assert(registry.LoadScript(script_id, &error_message));

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 1u;
    enter_event.argument_values[0] = 0x1111u;
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    HookEvent leave_event = {};
    leave_event.hook_id = 1u;
    leave_event.phase = HookEventPhase::kLeave;
    leave_event.return_value = 0x2222u;
    assert(EnqueueNativeJsHookEvent(leave_event, &error_message));

    std::string result_json;
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));
    assert(JsRuntime::CallRpc(script_id, "getevents", "[]", &result_json, &error_message));
    assert(result_json == "[\"leave:0x2222\"]");
    assert(!sent_messages.empty());
    assert(sent_messages.back().find("\"type\":\"log\"") != std::string::npos);
    assert(sent_messages.back().find("\"level\":\"error\"") != std::string::npos);
    assert(sent_messages.back().find("boom") != std::string::npos);

    JsRuntimeResetNativeHookInstallerDependenciesForTesting();
    registry.Clear();
    JsRuntime::Shutdown();
}

void TestDispatchPendingNativeHookEventsForwardsDeferredHookStatus() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();

    std::vector<std::string> sent_messages;
    JsRuntime::SetSendCallback([&](const std::string& json, const std::vector<uint8_t>& data) {
        (void)data;
        sent_messages.push_back(json);
        return true;
    });

    SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(&FakeEnsureInlineHookModuleObserverAsync);
    GetEnsureObserverAsyncStatus() = NOOK_STATUS_OK;

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libnative-lib.so";
    request.symbol_name = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

    NativeJsHookRecord record = {};
    assert(InstallNativeJsHook(request, NativeJsHookInstallerDependencies{}, &record, &error_message));
    assert(JsRuntime::DispatchPendingNativeHookEvents(&error_message));

    assert(!sent_messages.empty());
    assert(sent_messages.back().find("\"type\":\"hook-status\"") != std::string::npos);
    assert(sent_messages.back().find("\"state\":\"pending\"") != std::string::npos);
    assert(sent_messages.back().find("\"hookId\":1") != std::string::npos);
    assert(sent_messages.back().find("libnative-lib.so") != std::string::npos);

    ResetNativeJsResolveLoadedSymbolAddressForTesting();
    ResetNativeJsInlineHookAddressInvokerForTesting();
    ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
    JsRuntime::Shutdown();
}

}  // namespace

int main() {
    TestNativeAttachBindingExists();
    TestJniBindingExists();
    TestJniReadJStringUtf8ReturnsDecodedString();
    TestJniReadJStringUtf8RejectsNullArguments();
    TestJniReadJStringUtf8RejectsAsyncRuntimeUsage();
    TestModuleAndInterceptorBindingsExist();
    TestJavaBindingsExist();
    TestJavaPerformInvokesCallbackSynchronously();
    TestJavaPerformRejectsNonFunction();
    TestJavaPerformDelegatesToJavaVmPerformWhenReady();
    TestJavaUseReturnsClassAndMethodWrappers();
    TestJavaCastBindingExists();
    TestJavaCastReturnsRewrappedObjectWithNewClassName();
    TestJavaCastWrapperCanInvokeTargetClassMethodDirectly();
    TestJavaCastRejectsNonJavaObject();
    TestJavaCastRejectsNonClassWrapper();
    TestJavaRetainBindingExists();
    TestJavaRetainReturnsRewrappedObjectWithRetainedHandle();
    TestJavaRetainRejectsNonJavaObject();
    TestJavaRetainRejectsNullHandleObject();
    TestJavaDisposeReleasesOwnedRetainedHandleOnce();
    TestJavaOwnedHandleCleanupOnUnloadReleasesRetainedHandle();
    TestJavaOwnedHandleCleanupOnRegistryClearReleasesRetainedHandle();
    TestJavaOwnedHandleCleanupOnShutdownReleasesRetainedHandle();
    TestJavaOwnedHandleCleanupAfterExplicitDisposeDoesNotDoubleRelease();
    TestJavaOwnedHandleCleanupOnGcReleasesRetainedHandle();
    TestJavaClassFactoryRetainDisposeReleasesRetainedHandle();
    TestScriptWeakBindingExists();
    TestScriptWeakBindingGcApiExists();
    TestScriptWeakBindingFiresOnGc();
    TestScriptWeakBindingFiresOnScriptGcApi();
    TestScriptWeakBindingUnbindFiresCallbackImmediately();
    TestScriptWeakBindingFiresOnUnload();
    TestScriptPinBindingsExist();
    TestScriptPinPreventsUnloadUntilFullyUnpinned();
    TestScriptUnpinRejectsUnderflow();
    TestTimersBindingsExist();
    TestSetImmediateExecutesAsynchronously();
    TestPumpPendingTasksExecutesQueuedSetImmediateCallbacks();
    TestSetTimeoutExecutesAsynchronously();
    TestClearTimeoutCancelsPendingTimer();
    TestSetIntervalRepeatsUntilCleared();
    TestTimersAreClearedOnUnload();
    TestJavaChooseBindingExists();
    TestJavaChooseRejectsNonStringClassName();
    TestJavaChooseRejectsNonObjectCallbacks();
    TestJavaChooseRejectsMissingOnMatch();
    TestJavaChooseRejectsMissingOnComplete();
    TestJavaChooseDispatchesMatchesAndComplete();
    TestJavaEnumerateLoadedClassesBindingExists();
    TestJavaEnumerateLoadedClassesRejectsNonObjectCallbacks();
    TestJavaEnumerateLoadedClassesRejectsMissingOnMatch();
    TestJavaEnumerateLoadedClassesRejectsMissingOnComplete();
    TestJavaEnumerateLoadedClassesDispatchesDeduplicatedMatchesAndComplete();
    TestJavaEnumerateClassLoadersBindingExists();
    TestJavaEnumerateClassLoadersRejectsNonObjectCallbacks();
    TestJavaEnumerateClassLoadersRejectsMissingOnMatch();
    TestJavaEnumerateClassLoadersRejectsMissingOnComplete();
    TestJavaEnumerateClassLoadersDispatchesDeduplicatedMatchesAndComplete();
    TestJavaEnumerateClassLoadersWorksInsideReadyImmediate();
    TestJavaReadyDeferredCallbackPreservesScriptContextForImplementationInstall();
    TestJavaRegisterClassBindingExists();
    TestJavaRegisterClassReturnsClassLikeObjectWithNew();
    TestJavaRegisterClassNewForwardsInterfacesAndMethodsToBridge();
    TestJavaRegisterClassCallbackDispatchesByMethodName();
    TestJavaRegisterClassAcceptsMethodDeclarationObject();
    TestJavaRegisterClassAcceptsSingleEntryMethodDeclarationArray();
    TestJavaRegisterClassRejectsMethodDeclarationObjectWithoutImplementation();
    TestJavaRegisterClassRejectsMultipleMethodDeclarations();
    TestJavaRegisterClassAcceptsMultipleMethodDeclarationsWithDistinctSignatures();
    TestJavaRegisterClassRejectsDuplicateMethodDeclarationSignatures();
    TestJavaRegisterClassRejectsUnsupportedFieldsSpec();
    TestJavaRegisterClassRejectsUnsupportedSuperClassSpec();
    TestJavaPerformNowBindingExists();
    TestJavaPerformNowRejectsNonFunction();
    TestJavaPerformNowExecutesImmediately();
    TestJavaPerformNowDelegatesToVmPerform();
    TestJavaVmPerformBindingExists();
    TestJavaVmPerformRejectsNonFunction();
    TestJavaVmPerformExecutesImmediately();
    TestJavaVmPerformCanUseJavaBridgeImmediately();
    TestJavaVmGetEnvBindingExists();
    TestJavaVmGetEnvReturnsEnvWrapper();
    TestJavaVmGetEnvWorksInsideVmPerform();
    TestJavaVmTryGetEnvBindingExists();
    TestJavaVmTryGetEnvReturnsEnvWrapperWhenAvailable();
    TestJavaVmTryGetEnvReturnsNullWhenUnavailable();
    TestJavaVmTryGetEnvWorksInsideVmPerform();
    TestJavaVmPerformMakesTryGetEnvAvailableInsideCallback();
    TestJavaVmGetEnvWrapperExceptionCheckReturnsBoolean();
    TestJavaVmGetEnvWrapperExceptionOccurredReturnsPointer();
    TestJavaVmGetEnvWrapperExceptionClearReturnsTrue();
    TestJavaVmEnvMethodsRequeryLiveEnvPointer();
    TestJavaVmGetEnvWrapperGetObjectClassReturnsPointer();
    TestJavaVmGetEnvWrapperGetSuperclassReturnsClassWrapper();
    TestJavaVmGetEnvWrapperGetSuperclassReturnsNullWithoutParent();
    TestJavaVmGetEnvWrapperIsSameObjectReturnsBoolean();
    TestJavaVmGetEnvWrapperFindClassReturnsPointer();
    TestJavaVmGetEnvWrapperIsInstanceOfReturnsBoolean();
    TestJavaVmGetEnvWrapperIsAssignableFromReturnsBoolean();
    TestJavaVmGetEnvWrapperGetObjectRefTypeReturnsGlobal();
    TestJavaVmGetEnvWrapperGetObjectRefTypeReturnsInvalid();
    TestJavaVmGetEnvWrapperNewStringUtfReturnsPointer();
    TestJavaVmGetEnvWrapperGetStringUtfCharsReturnsPointer();
    TestJavaVmGetEnvWrapperReleaseStringUtfCharsReturnsTrue();
    TestJavaVmGetEnvWrapperNewGlobalRefReturnsPointer();
    TestJavaVmGetEnvWrapperDeleteGlobalRefReturnsTrue();
    TestJavaVmGetEnvWrapperNewWeakGlobalRefReturnsPointer();
    TestJavaVmGetEnvWrapperDeleteWeakGlobalRefReturnsTrue();
    TestJavaVmGetEnvWrapperDoesNotExposeMonitorEnterOrExit();
    TestJavaVmGetEnvWrapperIsSameObjectRejectsNonJavaObject();
    TestJavaVmGetEnvWrapperIsInstanceOfRejectsNonJavaObject();
    TestJavaVmGetEnvWrapperIsInstanceOfRejectsNonClassWrapper();
    TestJavaVmGetEnvWrapperGetSuperclassRejectsNonClassWrapper();
    TestJavaVmGetEnvWrapperIsAssignableFromRejectsNonClassWrapper();
    TestJavaVmGetEnvWrapperGetObjectRefTypeRejectsNonJavaObject();
    TestJavaVmGetEnvWrapperNewStringUtfRejectsNonString();
    TestJavaVmGetEnvWrapperGetStringUtfCharsRejectsNonPointer();
    TestJavaVmGetEnvWrapperReleaseStringUtfCharsRejectsNonPointer();
    TestJavaVmGetEnvWrapperNewGlobalRefRejectsNonJavaObject();
    TestJavaVmGetEnvWrapperDeleteGlobalRefRejectsNonPointer();
    TestJavaVmGetEnvWrapperNewWeakGlobalRefRejectsNonJavaObject();
    TestJavaVmGetEnvWrapperDeleteWeakGlobalRefRejectsNonPointer();
    TestJavaVmGetEnvWrapperGetObjectClassRejectsNonJavaObject();
    TestJavaVmGetEnvWrapperFindClassRejectsNonString();
    TestJavaMainThreadBindingsExist();
    TestJavaScheduleOnMainThreadRejectsNonFunction();
    TestJavaIsMainThreadComparesLooperHandles();
    TestJavaIsMainThreadUsesJavaObjectEqualityInsteadOfReferenceValue();
    TestJavaScheduleOnMainThreadUsesHandlerPostAndRunnable();
    TestJavaScheduleOnMainThreadDefersUntilReadyWhenApplicationUnavailable();
    TestJavaArrayBindingExists();
    TestJavaArrayOverloadSupportsArrayTypeNames();
    TestJavaUseSupportsPrototypeNamedMethodOverload();
    TestJavaInvokeInfersPrimitiveArrayOverload();
    TestJavaInvokeInfersStringArrayOverload();
    TestJavaInvokeInfersObjectArrayOverload();
    TestJavaInvokeInfersInt2dArrayOverload();
    TestJavaInvokeInfersBoolean2dArrayOverload();
    TestJavaInvokeInfersByte2dArrayOverload();
    TestJavaInvokeInfersShort2dArrayOverload();
    TestJavaInvokeInfersChar2dArrayOverload();
    TestJavaInvokeInfersLong2dArrayOverload();
    TestJavaInvokeInfersFloat2dArrayOverload();
    TestJavaInvokeInfersDouble2dArrayOverload();
    TestJavaInvokeInfersString2dArrayOverload();
    TestJavaInvokeInfersObject2dArrayOverload();
    TestJavaArrayDefaultInvokeParsesArraySignature();
    TestJavaInvokeInfersBooleanArrayOverload();
    TestJavaInvokeInfersByteArrayOverload();
    TestJavaInvokeInfersShortArrayOverload();
    TestJavaInvokeInfersCharArrayOverload();
    TestJavaInvokeInfersLongArrayOverload();
    TestJavaInvokeInfersFloatArrayOverload();
    TestJavaInvokeInfersDoubleArrayOverload();
    TestJavaInvokePreservesPrimitiveArrayMutation();
    TestJavaInvokePreservesObjectArrayMutation();
    TestJavaInvokePreservesNestedArrayMutation();
    TestJavaClassFactoryBindingExists();
    TestJavaClassFactoryGetRejectsNonLoaderObject();
    TestJavaClassFactoryGetReturnsFactoryWithUse();
    TestJavaClassFactoryGetReturnsFactoryWithChoose();
    TestJavaClassFactoryGetReturnsFactoryWithCast();
    TestJavaClassFactoryGetReturnsFactoryWithRetain();
    TestJavaClassFactoryGetReturnsFactoryWithNew();
    TestJavaClassFactoryUseReturnsWrapperWithNew();
    TestJavaClassFactoryUseForwardsLoaderHandleToMethodResolveAndInvoke();
    TestJavaClassFactoryUseNewForwardsLoaderHandleToConstructorResolveAndInvoke();
    TestJavaClassFactoryNewForwardsLoaderHandleToConstructorResolveAndInvoke();
    TestJavaClassFactoryCastForwardsLoaderHandleToTargetWrapperAndInvoke();
    TestJavaClassFactoryRetainPreservesLoaderHandleAndInvoke();
    TestJavaClassFactoryChooseForwardsLoaderHandleToEnumerationAndMatches();
    TestJavaClassFactoryUseForwardsLoaderHandleToImplementationInstall();
    TestJavaSetClassLoaderBindingExists();
    TestJavaSetClassLoaderRejectsNonLoaderObject();
    TestJavaSetClassLoaderMakesSubsequentUseLoaderAware();
    TestJavaSetClassLoaderDoesNotRetroactivelyModifyExistingWrapper();
    TestJavaSetClassLoaderMakesChooseLoaderAware();
    TestJavaSetClassLoaderMakesRetainLoaderAware();
    TestJavaOpenClassFileBindingExists();
    TestJavaOpenClassFileLoadSetsDefaultLoaderForSubsequentUse();
    TestJavaClassFactoryGetReturnsFactoryWithOpenClassFile();
    TestJavaClassFactoryOpenClassFileRejectsNonString();
    TestJavaClassFactoryOpenClassFileLoadReturnsScopedLoaderAndFactoryUse();
    TestJavaClassFactoryOpenClassFileLoadDoesNotSetDefaultLoader();
    TestJavaUseReturnsStaticFieldWrapper();
    TestJavaStaticFieldValueCanBeWritten();
    TestJavaInstanceFieldValueCanBeReadAndWrittenInsideCallbackReceiver();
    TestJavaInstanceFieldValueCanBeWrittenOnConstructedObject();
    TestJavaChooseInstanceFieldValueCanBeReadAndWritten();
    TestJavaChoosePrivateFieldAliasValueCanBeReadAndWritten();
    TestJavaChooseRetainedSameObjectFieldMutationPersists();
    TestJavaWrapperStringifyUsesClassNameFallback();
    TestJavaInstanceWrapperStringifyUsesObjectFallback();
    TestJavaMethodAndFieldWrapperStringifyUseFallbackText();
    TestJavaReflectionMethodArrayConsoleLogFallsBackToReadableText();
    TestJavaReturnArrayForTestingUsesArrayKindAndTypeName();
    TestJavaMethodWrapperExposesMinimalOverloadsArray();
    TestJavaMethodWrapperOverloadsAllowImplementationInstall();
    TestJavaMethodOverloadReturnsSignatureBoundWrapper();
    TestJavaMethodOverloadReturnsPrimitiveDoubleSignatureBoundWrapper();
    TestJavaMethodOverloadReturnsPrimitiveLongSignatureBoundWrapper();
    TestJavaMethodOverloadReturnsPrimitiveFloatSignatureBoundWrapper();
    TestJavaStaticMethodOverloadReturnsExactSignatureBoundWrapper();
    TestJavaStaticVoidMethodOverloadReturnsExactSignatureBoundWrapper();
    TestJavaMethodWrappersAreCallableFunctions();
    TestJavaStaticMethodWrapperCanInvokeOriginalDirectly();
    TestJavaStaticMethodWrapperCanResolveIntOverloadFromPlainNumberDirectly();
    TestJavaInstanceMethodWrapperCanInvokeOriginalDirectlyInsideCallbackReceiver();
    TestJavaInstanceMethodWrapperCanResolveDoubleOverloadFromIntegralNumberDirectlyInsideCallbackReceiver();
    TestJavaInstanceMethodWrapperCanResolveLongOverloadFromPlainIntDirectlyInsideCallbackReceiver();
    TestJavaInstanceMethodWrapperCanResolveFloatOverloadFromPlainDoubleDirectlyInsideCallbackReceiver();
    TestJavaMethodSpecificityPrefersStringOverObject();
    TestJavaMethodSpecificityPrefersListOverObject();
    TestJavaMethodSpecificityKeepsUnrelatedReferenceAmbiguity();
    TestJavaMethodSpecificityKeepsCrossParameterAmbiguity();
    TestJavaOverloadResolutionPrefersStringForNull();
    TestJavaDescriptorNormalizationConvertsObjectDescriptorForLookup();
    TestJavaDescriptorNormalizationPreservesArrayDescriptorForLookup();
    TestJavaStaticMethodWrapperCanResolveNullReferenceDirectly();
    TestJavaStaticMethodWrapperRejectsNullForPrimitiveOnlyOverload();
    TestJavaStaticMethodWrapperCanResolveBoxedBooleanDirectly();
    TestJavaStaticMethodWrapperCanResolveObjectOverloadFromStringDirectly();
    TestJavaStaticMethodWrapperCanResolveNumberOverloadFromPlainIntDirectly();
    TestJavaInstanceMethodWrapperCanResolveObjectOverloadFromWrapperDirectly();
    TestJavaImplementationAssignmentInstallsBridgeHook();
    TestJavaOverloadImplementationInstallsExactSignatureHook();
    TestJavaImplementationCallbackCanCallOriginal();
    TestJavaImplementationCallbackDirectSelfInvokeFallsBackToCallOriginal();
    TestJavaOverloadCallbackCanCallOriginalWithExactSignature();
    TestJavaOverloadStringToStringCallbackCallOriginalUsesInstalledExactSignature();
    TestJavaConstructorCallbackMethodAliasCallsOriginalInsteadOfReinvokingConstructor();
    TestJavaOverloadCanInstallDistinctPrimitiveAndStringHooks();
    TestJavaOverloadDoubleCallbackCanCallOriginalWithExactSignature();
    TestJavaOverloadLongCallbackCanCallOriginalWithExactSignature();
    TestJavaOverloadFloatCallbackCanCallOriginalWithExactSignature();
    TestJavaStaticOverloadImplementationInstallsExactSignatureHook();
    TestJavaStaticOverloadCallbackCanCallOriginalWithExactSignature();
    TestJavaStaticVoidOverloadCallbackCanCallOriginalWithExactSignature();
    TestJavaBridgeDispatchInvokesInstalledJsCallback();
    TestNativeAttachRequiresObjectOptions();
    TestNativeAttachRejectsUnsupportedType();
    TestNativeAttachRequiresModuleAndFunctionCallbacks();
    TestNativeAttachRegistersCallbacksAndReturnsHookId();
    TestNativeAttachPassesSnapshotConfigToInstaller();
    TestNativeAttachPassesCStringSnapshotConfigToInstaller();
    TestNativeAttachReturnsDeferredWhenModuleIsNotLoadedYet();
    TestModuleFindExportByNameReturnsResolvedAddress();
    TestModuleAttachExportRegistersCallbacksAndReturnsHookId();
    TestModuleAttachExportPassesSnapshotConfigToInstaller();
    TestModuleAttachExportReturnsDeferredWhenModuleIsNotLoadedYet();
    TestNativePointerBasics();
    TestNativePointerToInt32AndUInt32();
    TestNativePointerEquals();
    TestNativePointerCompare();
    TestNativePointerBitwiseOps();
    TestNativePointerReadUtf8String();
    TestMemoryReadUtf8String();
    TestMemoryReadUtf8StringNormalizesTaggedPointer();
    TestNativePointerReadUtf8StringUsesSingleReadableProbeForContiguousBuffer();
    TestMemoryReadUtf8StringRepeatedReadsUseConstantReadableProbeCount();
    TestNativePointerReadUtf16String();
    TestNativePointerReadCStringAlias();
    TestNativePointerReadCStringStopsAtTerminatorBeforeGuardPage();
    TestNativePointerReadAnsiStringAlias();
    TestNativePointerReadUtf8StringRejectsUnreadablePointer();
    TestNativePointerReadUtf16StringRejectsUnreadablePointer();
    TestNativePointerWriteUtf8StringWritesCStringAndReturnsPointer();
    TestNativePointerWriteUtf16StringWritesWideStringAndReturnsPointer();
    TestNativePointerWriteAnsiStringAlias();
    TestNativePointerWriteUtf8StringRejectsUnwritablePointer();
    TestNativePointerWriteUtf16StringRejectsUnwritablePointer();
    TestNativePointerReadByteArrayReturnsArrayBuffer();
    TestNativePointerWriteByteArrayFromArrayBuffer();
    TestNativePointerWriteByteArrayFromNumberArray();
    TestNativePointerReadByteArrayRejectsUnreadablePointer();
    TestNativePointerWriteByteArrayRejectsUnwritablePointer();
    TestMemoryAllocAndNativePointerReadWrite();
    TestMemoryAllocUtf16String();
    TestMemoryAllocAnsiString();
    TestNativePointerReadWriteU64();
    TestMemoryCopyCopiesBetweenValidRanges();
    TestMemoryCopyHandlesOverlap();
    TestMemoryCopyRejectsUnreadableSource();
    TestMemoryCopyRejectsUnwritableDestination();
    TestMemoryDupReturnsArrayBufferLength();
    TestMemoryDupPreservesByteContent();
    TestMemoryDupRejectsUnreadableSource();
    TestHexdumpArrayBufferSingleLine();
    TestHexdumpArrayBufferMultiLine();
    TestHexdumpNativePointerWithLength();
    TestHexdumpNativePointerWithOffset();
    TestHexdumpArrayBufferWithHeaderIncludesAscii();
    TestHexdumpNativePointerWithHeaderIncludesAddressAndAscii();
    TestHexdumpNativePointerRequiresLength();
    TestHexdumpRejectsUnreadablePointer();
    TestHexdumpRejectsOutOfBoundsArrayBufferRange();
    TestHexdumpAnsiOutputIncludesEscapeCodes();
    TestHexdumpMixedPrintableAndNonPrintableAscii();
    TestNativePointerWriteRejectsUnwritablePointer();
    TestNormalizeTaggedProcessAddressForRangeCheck();
    TestSyntheticModuleLookupDoesNotMergeDisjointMappingsByPath();
    TestUInt64AndInt64Basics();
    TestNativePointerReadWriteU64SupportsUInt64Objects();
    TestNativePointerReadSignedScalars();
    TestNativePointerWriteSignedScalars();
    TestNativePointerReadWriteS64();
    TestNativePointerReadWriteFloatAndDouble();
    TestNativePointerWriteFloatAndDoubleReturnsSamePointer();
    TestMemoryProtectRejectsMissingArguments();
    TestMemoryProtectRejectsNullPointer();
    TestMemoryProtectRejectsZeroSize();
    TestMemoryProtectRejectsInvalidProtectionString();
    TestMemoryProtectTogglesReadOnlyAndReadWrite();
    TestMemoryProtectAcceptsExecutableProtection();
    TestMemoryProtectAcceptsAllPermissionTriples();
    TestMemoryPatchCodeCommitsPatchedBytesAndRestoresProtection();
    TestMemoryScanSyncFindsExactPatternTwice();
    TestMemoryScanSyncSupportsWildcardBytes();
    TestMemoryScanSyncReturnsEmptyArrayOnMiss();
    TestMemoryScanSyncRejectsInvalidPattern();
    TestMemoryScanSyncRejectsUnreadableRange();
    TestMemoryScanInvokesOnMatchAndOnComplete();
    TestMemoryScanSupportsStopFromOnMatch();
    TestMemoryScanInvokesOnErrorAndOnCompleteForUnreadableRange();
    TestMemoryScanRejectsInvalidCallbacksObject();
    TestProcessEnumerateRangesBindingExists();
    TestProcessStaticPropertiesExist();
    TestProcessStaticPropertiesValues();
    TestProcessIdentityBindingsExist();
    TestProcessIdentityValues();
    TestProcessThreadBindingsExist();
    TestProcessThreadApisReturnCurrentThread();
    TestThreadIdMatchesCurrentThreadId();
    TestThreadSleepBindingExists();
    TestThreadSleepDelaysExecution();
    TestThreadSleepRejectsNegativeSeconds();
    TestProcessEnumerateModulesBindingExists();
    TestProcessEnumerateModulesFindsCurrentExecutable();
    TestProcessFindModuleByNameAndGetModuleByName();
    TestProcessMainModuleResolvesCurrentExecutable();
    TestModuleLoadBindingExists();
    TestModuleLoadReturnsModuleObject();
    TestModuleEnsureInitializedBindingExists();
    TestModuleEnsureInitializedLoadedModuleReturnsUndefined();
    TestModuleEnsureInitializedThrowsOnMissingModule();
    TestModuleEnumerateExportsBindingExists();
    TestModuleEnumerateExportsFindsSmokeExport();
    TestModuleFindAndGetSymbolByName();
    TestModuleGetSymbolByNameThrowsOnMiss();
    TestModuleEnumerateSymbolsBindingExists();
    TestModuleEnumerateSymbolsFindsKnownSymbol();
    TestModuleEnumerateImportsBindingExists();
    TestModuleEnumerateImportsFindsImportedEntry();
    TestModuleFindAndGetImportByName();
    TestModuleGetImportByNameThrowsOnMiss();
    TestProcessEnumerateRangesRejectsInvalidProtection();
    TestProcessEnumerateRangesFindsReadWriteMapping();
    TestProcessEnumerateRangesFindsReadOnlyMappingAfterProtect();
    TestProcessFindRangeByAddressBindingExists();
    TestProcessFindRangeByAddressFindsMapping();
    TestProcessFindRangeByAddressRejectsInvalidPointer();
    TestProcessFindRangeByAddressReturnsNullOnMiss();
    TestProcessFindRangeByAddressNormalizesTaggedPointer();
    TestProcessGetModuleByAddressBindingExists();
    TestProcessGetModuleByAddressFindsCurrentExecutable();
    TestProcessGetModuleByAddressRejectsInvalidPointer();
    TestProcessGetModuleByAddressReturnsNullOnMiss();
    TestProcessGetModuleByAddressNormalizesTaggedPointer();
    TestModuleFindRangeByAddressBindingExists();
    TestModuleFindRangeByAddressFindsMapping();
    TestModuleFindRangeByAddressReturnsNullOnMiss();
    TestModuleBaseAddressBindingsExist();
    TestModuleFindBaseAddressReturnsNativePointerForLoadedModule();
    TestModuleFindBaseAddressReturnsNullOnMiss();
    TestModuleGetBaseAddressThrowsOnMiss();
    TestModuleGetExportByNameBindingExists();
    TestModuleGetExportByNameReturnsNativePointer();
    TestModuleGetExportByNameThrowsOnMiss();
    TestModuleGetExportByNameFallsBackWhenLoadedResolverMisses();
    TestModuleGlobalExportBindingsExist();
    TestModuleFindAndGetGlobalExportByName();
    TestModuleGetGlobalExportByNameThrowsOnMiss();
    TestProcessAttachModuleObserverBindingExists();
    TestProcessAttachModuleObserverReplaysExistingModules();
    TestProcessAttachModuleObserverReceivesLoadedModuleEvent();
    TestDebugSymbolBindingExists();
    TestDebugSymbolFromAddressResolvesKnownExport();
    TestDebugSymbolFromAddressReturnsUnknownObjectForMiss();
    TestThreadBacktraceBindingExists();
    TestThreadBacktraceAcceptsBacktracerAsFirstArgument();
    TestThreadBacktraceAcceptsNoArguments();
    TestThreadBacktraceAcceptsFuzzyBacktracerAsFirstArgument();
    TestDispatchPendingNativeHookEventsThreadBacktraceFromContext();
    TestDispatchPendingNativeHookEventsThreadBacktraceFuzzyUsesStackPointer();
    TestDispatchPendingNativeHookEventsThreadBacktraceFuzzyScansBeyondInitialWindow();
    TestModuleEnumerateModulesBindingExists();
    TestModuleEnumerateModulesIncludesCurrentExecutable();
    TestModuleMapBindingExists();
    TestModuleMapResolvesCurrentExecutable();
    TestModuleMapMissBehavior();
    TestModuleMapUpdateBindingExists();
    TestModuleMapUpdateReturnsSameObjectAndRefreshesSnapshot();
    TestNativeFunctionBindingExists();
    TestNativeFunctionRejectsUnsupportedReturnType();
    TestNativeFunctionRejectsUnsupportedArgumentType();
    TestNativeFunctionRejectsNonArrayArgumentTypes();
    TestNativeFunctionCallsIntAdd();
    TestNativeFunctionEchoesPointer();
    TestNativeFunctionCallsVoidSink();
    TestNativeFunctionCallsExtendedScalarTypes();
    TestNativeFunctionCallsMixedAbiScalarTypes();
    TestNativeFunctionRejectsWrongArgumentCount();
    TestNativeCallbackBindingExists();
    TestNativeCallbackRejectsNonFunction();
    TestNativeCallbackRejectsUnsupportedReturnType();
    TestNativeCallbackRejectsUnsupportedArgumentType();
    TestNativeCallbackRoundtripUInt32();
    TestNativeCallbackRoundtripPointer();
    TestNativeCallbackRoundtripVoid();
    TestNativeCallbackRoundtripExtendedScalarTypes();
    TestNativeCallbackCleanupOnUnload();
    TestInterceptorReplaceAndRevertBindingsExist();
    TestInterceptorReplaceRejectsInvalidTarget();
    TestInterceptorReplaceRejectsNonNativeCallbackReplacement();
    TestInterceptorReplaceChangesBehaviorAndRevertRestoresIt();
    TestNativeFunctionInvokeBypassesReplacedTargetBody();
    TestInterceptorReplaceAcceptsNativeFunctionTargetAndPlainJsReplacement();
    TestInterceptorReplaceExposesOriginalOnNativeFunctionTarget();
    TestInterceptorReplaceRejectsDuplicateTarget();
    TestInterceptorRevertRejectsMissingTarget();
    TestInterceptorReplaceCleanupOnUnloadRestoresBehavior();
    TestModuleFindExportByNameReturnsNativePointer();
    TestModuleFindExportByNameReturnsNullOnMiss();
    TestModuleFindExportByNameWithNullModuleUsesGlobalLookup();
    TestModuleFindExportByNameStillReturnsAddressForUnsafeInlineHookSymbol();
    TestInterceptorAttachRegistersCallbacksByAddress();
    TestInterceptorAttachAcceptsNativePointer();
    TestInterceptorAttachAcceptsModuleAndSymbolOptions();
    TestInterceptorAttachPassesSnapshotConfigToInstaller();
    TestInterceptorAttachAcceptsDeferredModuleAndSymbolOptions();
    TestInterceptorAttachAcceptsSingleCallback();
    TestInterceptorAttachAcceptsNonblockingOption();
    TestInterceptorAttachAcceptsBlockingOption();
    TestInterceptorDetachRemovesOnlyRequestedHook();
    TestInterceptorDetachAllOnlyRemovesCurrentScriptHooks();
    TestInterceptorListenerDetachWorksFromRpcCallback();
    TestInterceptorListenerDetachWorksFromPostCallback();
    TestInterceptorListenerDetachWorksFromOnEnterCallback();
    TestDispatchPendingNativeHookEventsInvokesMatchingCallbacks();
    TestDispatchPendingNativeHookEventsExposeNativePointerArgsAndReturnValue();
    TestDispatchPendingNativeHookEventsExposeJniUtf8Snapshots();
    TestDispatchPendingNativeHookEventsExposeCStringUtf8Snapshots();
    TestInterceptorHookInvocationMemoryReadUtf8StringWorksInOnEnter();
    TestInterceptorHookInvocationMemoryReadUtf8StringUsesCachedMappingsAndPointerMetadata();
    TestInterceptorHighFrequencyHookStillDeliversInitialOnEnter();
    TestDispatchPendingNativeHookEventsExposeInvocationContextAndSharedThis();
    TestDispatchPendingNativeHookEventsReuseInvocationWrappers();
    TestDispatchPendingNativeHookEventsClearsSharedThisBetweenInvocations();
    TestInvokeInstalledNativeHookDoesNotDependOnBridgeMutexAfterInstall();
    TestDispatchPendingNativeHookEventsReuseInvocationContextPointers();
    TestInterceptorHookInvocationIgnoresCorruptedBaselineMetadata();
    TestInterceptorHookInvocationCanOverrideArgumentRegisters();
    TestInterceptorHookInvocationCanOverrideArgumentRegistersWithArgsReplace();
    TestInterceptorHookInvocationCanOverrideReturnValue();
    TestInterceptorHookInvocationCanOverrideReturnValueWithNumber();
    TestInterceptorHookInvocationPreservesHighPointerPrecisionAfterReplace();
    TestInterceptorHookInvocationUpdatesRetvalAndContextViewAfterReplace();
    TestInterceptorHookInvocationSkipsMissingPhaseCallback();
    TestInterceptorNonblockingArgsReplaceWarnsAndDoesNotMutate();
    TestInterceptorNonblockingRetvalReplaceWarnsAndDoesNotMutate();
    TestDispatchPendingNativeHookEventsContinuesAfterCallbackException();
    TestDispatchPendingNativeHookEventsForwardsDeferredHookStatus();
    return 0;
}
