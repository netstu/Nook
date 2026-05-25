#pragma once

#include <cstdint>
#include <string>

#include "agent_runtime/nook_java_js_bridge.h"
#include "agent_runtime/nook_native_js_bridge.h"

namespace nook {
namespace agent_runtime {

struct JsRuntimeTestModuleMapping {
    const char* path = nullptr;
    uint64_t start = 0;
    uint64_t end = 0;
};

struct JsRuntimeTestModuleRecord {
    std::string name;
    std::string path;
    uint64_t base = 0;
    uint64_t size = 0;
};

using JsRuntimeReadJStringUtf8ForTesting =
    bool (*)(uint64_t env_ptr, uint64_t jstring_ptr, std::string* text_out, std::string* error_out);
enum class JsRuntimeJavaEnvQueryStatus {
    kAvailable = 0,
    kUnavailable = 1,
    kError = 2,
};
using JsRuntimeGetJavaEnvPointerForTesting =
    JsRuntimeJavaEnvQueryStatus (*)(bool allow_attach,
                                    uint64_t* env_ptr_out,
                                    std::string* error_out);
using JsRuntimeJavaEnvExceptionCheckForTesting =
    bool (*)(uint64_t env_ptr, bool* has_exception_out, std::string* error_out);
using JsRuntimeJavaEnvExceptionOccurredForTesting =
    bool (*)(uint64_t env_ptr, uint64_t* exception_ptr_out, std::string* error_out);
using JsRuntimeJavaEnvExceptionClearForTesting =
    bool (*)(uint64_t env_ptr, std::string* error_out);
using JsRuntimeJavaEnvFindClassForTesting =
    bool (*)(uint64_t env_ptr,
             const char* class_name,
             uint64_t* class_ptr_out,
             std::string* error_out);
using JsRuntimeJavaEnvGetObjectClassForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t object_handle,
             uint64_t* class_ptr_out,
             std::string* error_out);
using JsRuntimeJavaEnvIsSameObjectForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t left_object_handle,
             uint64_t right_object_handle,
             bool* result_out,
             std::string* error_out);
using JsRuntimeJavaEnvIsInstanceOfForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t object_handle,
             const char* class_name,
             bool* result_out,
             std::string* error_out);
using JsRuntimeJavaEnvNewStringUtfForTesting =
    bool (*)(uint64_t env_ptr,
             const char* utf8_text,
             uint64_t* string_ptr_out,
             std::string* error_out);
using JsRuntimeJavaEnvGetStringUtfCharsForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t jstring_ptr,
             uint64_t* chars_ptr_out,
             std::string* error_out);
using JsRuntimeJavaEnvReleaseStringUtfCharsForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t jstring_ptr,
             uint64_t chars_ptr,
             std::string* error_out);
using JsRuntimeJavaEnvNewGlobalRefForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t object_handle,
             uint64_t* ref_ptr_out,
             std::string* error_out);
using JsRuntimeJavaEnvDeleteGlobalRefForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t ref_ptr,
             std::string* error_out);
using JsRuntimeJavaEnvNewWeakGlobalRefForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t object_handle,
             uint64_t* ref_ptr_out,
             std::string* error_out);
using JsRuntimeJavaEnvDeleteWeakGlobalRefForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t ref_ptr,
             std::string* error_out);
using JsRuntimeJavaEnvGetObjectRefTypeForTesting =
    bool (*)(uint64_t env_ptr,
             uint64_t object_handle,
             uint32_t* ref_type_out,
             std::string* error_out);
using JsRuntimeJavaEnvGetSuperclassForTesting =
    bool (*)(uint64_t env_ptr,
             const char* class_name,
             uint64_t loader_handle,
             bool* has_superclass_out,
             std::string* superclass_name_out,
             std::string* error_out);
using JsRuntimeJavaEnvIsAssignableFromForTesting =
    bool (*)(uint64_t env_ptr,
             const char* target_class_name,
             uint64_t target_loader_handle,
             const char* source_class_name,
             uint64_t source_loader_handle,
             bool* result_out,
             std::string* error_out);

void JsRuntimeSetNativeHookInstallerDependenciesForTesting(
    const NativeJsHookInstallerDependencies& dependencies);
void JsRuntimeResetNativeHookInstallerDependenciesForTesting();
void JsRuntimeSetJavaHookInstallerDependenciesForTesting(
    const JavaJsHookInstallerDependencies& dependencies);
void JsRuntimeResetJavaHookInstallerDependenciesForTesting();
void JsRuntimeSetReadJStringUtf8ForTesting(JsRuntimeReadJStringUtf8ForTesting callback);
void JsRuntimeResetReadJStringUtf8ForTesting();
void JsRuntimeSetGetJavaEnvPointerForTesting(JsRuntimeGetJavaEnvPointerForTesting callback);
void JsRuntimeResetGetJavaEnvPointerForTesting();
void JsRuntimeSetJavaEnvExceptionCheckForTesting(JsRuntimeJavaEnvExceptionCheckForTesting callback);
void JsRuntimeResetJavaEnvExceptionCheckForTesting();
void JsRuntimeSetJavaEnvExceptionOccurredForTesting(
    JsRuntimeJavaEnvExceptionOccurredForTesting callback);
void JsRuntimeResetJavaEnvExceptionOccurredForTesting();
void JsRuntimeSetJavaEnvExceptionClearForTesting(JsRuntimeJavaEnvExceptionClearForTesting callback);
void JsRuntimeResetJavaEnvExceptionClearForTesting();
void JsRuntimeSetJavaEnvFindClassForTesting(JsRuntimeJavaEnvFindClassForTesting callback);
void JsRuntimeResetJavaEnvFindClassForTesting();
void JsRuntimeSetJavaEnvGetObjectClassForTesting(JsRuntimeJavaEnvGetObjectClassForTesting callback);
void JsRuntimeResetJavaEnvGetObjectClassForTesting();
void JsRuntimeSetJavaEnvIsSameObjectForTesting(JsRuntimeJavaEnvIsSameObjectForTesting callback);
void JsRuntimeResetJavaEnvIsSameObjectForTesting();
void JsRuntimeSetJavaEnvIsInstanceOfForTesting(JsRuntimeJavaEnvIsInstanceOfForTesting callback);
void JsRuntimeResetJavaEnvIsInstanceOfForTesting();
void JsRuntimeSetJavaEnvNewStringUtfForTesting(JsRuntimeJavaEnvNewStringUtfForTesting callback);
void JsRuntimeResetJavaEnvNewStringUtfForTesting();
void JsRuntimeSetJavaEnvGetStringUtfCharsForTesting(
    JsRuntimeJavaEnvGetStringUtfCharsForTesting callback);
void JsRuntimeResetJavaEnvGetStringUtfCharsForTesting();
void JsRuntimeSetJavaEnvReleaseStringUtfCharsForTesting(
    JsRuntimeJavaEnvReleaseStringUtfCharsForTesting callback);
void JsRuntimeResetJavaEnvReleaseStringUtfCharsForTesting();
void JsRuntimeSetJavaEnvNewGlobalRefForTesting(JsRuntimeJavaEnvNewGlobalRefForTesting callback);
void JsRuntimeResetJavaEnvNewGlobalRefForTesting();
void JsRuntimeSetJavaEnvDeleteGlobalRefForTesting(
    JsRuntimeJavaEnvDeleteGlobalRefForTesting callback);
void JsRuntimeResetJavaEnvDeleteGlobalRefForTesting();
void JsRuntimeSetJavaEnvNewWeakGlobalRefForTesting(
    JsRuntimeJavaEnvNewWeakGlobalRefForTesting callback);
void JsRuntimeResetJavaEnvNewWeakGlobalRefForTesting();
void JsRuntimeSetJavaEnvDeleteWeakGlobalRefForTesting(
    JsRuntimeJavaEnvDeleteWeakGlobalRefForTesting callback);
void JsRuntimeResetJavaEnvDeleteWeakGlobalRefForTesting();
void JsRuntimeSetJavaEnvGetObjectRefTypeForTesting(
    JsRuntimeJavaEnvGetObjectRefTypeForTesting callback);
void JsRuntimeResetJavaEnvGetObjectRefTypeForTesting();
void JsRuntimeSetJavaEnvGetSuperclassForTesting(
    JsRuntimeJavaEnvGetSuperclassForTesting callback);
void JsRuntimeResetJavaEnvGetSuperclassForTesting();
void JsRuntimeSetJavaEnvIsAssignableFromForTesting(
    JsRuntimeJavaEnvIsAssignableFromForTesting callback);
void JsRuntimeResetJavaEnvIsAssignableFromForTesting();
bool JsRuntimeHasNativeHookCallbacksForTesting(uint32_t script_id, uint32_t hook_id);
bool JsRuntimeHasJavaHookCallbackForTesting(uint32_t script_id, uint32_t hook_id);
size_t JsRuntimeGetNativeCallbackCountForTesting(uint32_t script_id);
bool JsRuntimeInvokeJavaHookCallbackForTesting(uint32_t script_id,
                                               uint32_t hook_id,
                                               const JavaJsValue* args,
                                               size_t arg_count,
                                               JavaJsValue* result,
                                               std::string* error_message);
bool JsRuntimeInvokeJavaRegisteredClassCallbackForTesting(uint32_t script_id,
                                                          uint32_t callback_id,
                                                          uint64_t receiver_handle,
                                                          const char* receiver_class_name,
                                                          const char* method_name,
                                                          const char* method_signature,
                                                          const JavaJsValue* args,
                                                          size_t arg_count,
                                                          JavaJsValue* result,
                                                          std::string* error_message);
uint64_t JsRuntimeNormalizeProcessAddressForTesting(uint64_t value);
bool JsRuntimeGetModuleByAddressForTesting(const JsRuntimeTestModuleMapping* mappings,
                                           size_t mapping_count,
                                           uint64_t address,
                                           JsRuntimeTestModuleRecord* out_record);
void JsRuntimeRunGcForTesting();
void JsRuntimeResetReadableMemoryProbeCountForTesting();
uint64_t JsRuntimeGetReadableMemoryProbeCountForTesting();
void JsRuntimeResetReadableMappingLookupCountForTesting();
uint64_t JsRuntimeGetReadableMappingLookupCountForTesting();

}  // namespace agent_runtime
}  // namespace nook
