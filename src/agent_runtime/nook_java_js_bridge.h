#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nook {
namespace agent_runtime {

struct JavaJsHookRequest {
    uint32_t hook_id = 0;
    std::string class_name;
    std::string method_name;
    std::string signature;
    uint64_t loader_handle = 0u;
    bool is_static = false;
    bool deferred = true;
};

struct JavaJsHookRecord {
    uint32_t hook_id = 0;
    std::string class_name;
    std::string method_name;
    std::string signature;
    uint64_t loader_handle = 0u;
    bool is_static = false;
    bool deferred = true;
    int installed_hook_id = -1;
    uint32_t callback_slot = std::numeric_limits<uint32_t>::max();
};

struct JavaJsFieldRecord {
    std::string class_name;
    std::string field_name;
    std::string reflected_field_name;
    std::string signature;
    uint64_t loader_handle = 0u;
    bool is_static = false;
    bool uses_declared_field_lookup = false;
};

struct JavaJsMethodRecord {
    std::string class_name;
    std::string method_name;
    std::string signature;
    uint64_t loader_handle = 0u;
    bool is_static = false;
};

struct JavaJsRegisteredClassMethodRecord {
    std::string name;
    std::string signature;
};

struct JavaJsRegisterClassRequest {
    uint32_t callback_id = 0u;
    std::string class_name;
    uint64_t loader_handle = 0u;
    std::vector<std::string> interface_class_names;
    std::vector<JavaJsRegisteredClassMethodRecord> methods;
};

enum class JavaMethodSpecificityComparisonForTesting {
    kIncomparable = 0,
    kLeftMoreSpecific = 1,
    kRightMoreSpecific = 2,
};

enum class JavaOverloadMatchResultForTesting {
    kNoMatch = 0,
    kUniqueMatch = 1,
    kAmbiguous = 2,
};

using JavaDescriptorIsAssignableFromForTesting =
    bool (*)(const std::string& target_descriptor,
             const std::string& source_descriptor,
             void* opaque);

enum class JavaJsValueKind {
    kUndefined = 0,
    kString = 1,
    kBoolean = 2,
    kInt32 = 3,
    kDouble = 4,
    kInt64 = 5,
    kFloat = 6,
    kObject = 7,
    kArray = 8,
};

struct JavaJsValue {
    JavaJsValueKind kind = JavaJsValueKind::kUndefined;
    std::string string_value;
    bool bool_value = false;
    int32_t int_value = 0;
    int64_t int64_value = 0;
    float float_value = 0.0f;
    double double_value = 0.0;
    uint64_t object_handle = 0u;
    std::string object_class_name;
    bool object_handle_is_global = false;
    std::string array_type_name;
    std::vector<JavaJsValue> array_elements;
};

using InstallJavaJsHookFn = bool (*)(const JavaJsHookRequest& request,
                                     JavaJsHookRecord* out_record,
                                     std::string* error_message);
using CallOriginalJavaJsHookFn = bool (*)(const JavaJsHookRecord& record,
                                          const JavaJsValue* args,
                                          std::size_t arg_count,
                                          JavaJsValue* result,
                                          std::string* error_message);
using ResolveJavaMethodSignatureFn = bool (*)(const std::string& class_name,
                                              const std::string& method_name,
                                              const std::vector<std::string>& argument_type_names,
                                              uint64_t loader_handle,
                                              bool is_static,
                                              std::string* signature,
                                              std::string* error_message);
using ResolveJavaFieldFn = bool (*)(const std::string& class_name,
                                    const std::string& field_name,
                                    uint64_t loader_handle,
                                    bool is_static,
                                    JavaJsFieldRecord* out_record,
                                    std::string* error_message);
using InvokeJavaMethodFn = bool (*)(const JavaJsMethodRecord& record,
                                    uint64_t receiver_handle,
                                    const JavaJsValue* args,
                                    std::size_t arg_count,
                                    JavaJsValue* result,
                                    std::string* error_message);
using ReadJavaFieldFn = bool (*)(const JavaJsFieldRecord& record,
                                 uint64_t receiver_handle,
                                 JavaJsValue* result,
                                 std::string* error_message);
using WriteJavaFieldFn = bool (*)(const JavaJsFieldRecord& record,
                                  uint64_t receiver_handle,
                                  const JavaJsValue& value,
                                  std::string* error_message);
using RetainJavaObjectFn = bool (*)(uint64_t object_handle,
                                    uint64_t* retained_handle,
                                    std::string* error_message);
using ReleaseJavaObjectFn = bool (*)(uint64_t object_handle,
                                     std::string* error_message);
using EnumerateJavaObjectsFn = bool (*)(const std::string& class_name,
                                        uint64_t loader_handle,
                                        std::vector<JavaJsValue>* matches,
                                        std::string* error_message);
using EnumerateLoadedJavaClassesFn = bool (*)(std::vector<std::string>* class_names,
                                              std::string* error_message);
using EnumerateJavaClassLoadersFn = bool (*)(std::vector<JavaJsValue>* matches,
                                             std::string* error_message);
using RegisterJavaClassFn = bool (*)(const JavaJsRegisterClassRequest& request,
                                     JavaJsValue* result,
                                     std::string* error_message);
using JavaJsHookInvocationDispatcher = bool (*)(uint32_t hook_id,
                                                uint64_t receiver_handle,
                                                const JavaJsValue* args,
                                                std::size_t arg_count,
                                                JavaJsValue* result,
                                                std::string* error_message);

struct JavaJsHookInstallerDependencies {
    InstallJavaJsHookFn install_hook = nullptr;
    CallOriginalJavaJsHookFn call_original_hook = nullptr;
    ResolveJavaMethodSignatureFn resolve_signature = nullptr;
    ResolveJavaFieldFn resolve_field = nullptr;
    InvokeJavaMethodFn invoke_method = nullptr;
    ReadJavaFieldFn read_field = nullptr;
    WriteJavaFieldFn write_field = nullptr;
    RetainJavaObjectFn retain_object = nullptr;
    ReleaseJavaObjectFn release_object = nullptr;
    EnumerateJavaObjectsFn enumerate_objects = nullptr;
    EnumerateLoadedJavaClassesFn enumerate_loaded_classes = nullptr;
    EnumerateJavaClassLoadersFn enumerate_class_loaders = nullptr;
    RegisterJavaClassFn register_class = nullptr;
};

bool InstallJavaJsHook(const JavaJsHookRequest& request,
                       const JavaJsHookInstallerDependencies& dependencies,
                       JavaJsHookRecord* out_record,
                       std::string* error_message);
bool UninstallJavaJsHook(uint32_t hook_id, std::string* error_message);
bool CallOriginalJavaJsHook(uint32_t hook_id,
                            const JavaJsValue* args,
                            std::size_t arg_count,
                            const JavaJsHookInstallerDependencies& dependencies,
                            JavaJsValue* result,
                            std::string* error_message);
bool ResolveJavaMethodSignature(const std::string& class_name,
                                const std::string& method_name,
                                const std::vector<std::string>& argument_type_names,
                                uint64_t loader_handle,
                                bool is_static,
                                const JavaJsHookInstallerDependencies& dependencies,
                                std::string* signature,
                                std::string* error_message);
bool ResolveJavaField(const std::string& class_name,
                      const std::string& field_name,
                      uint64_t loader_handle,
                      bool is_static,
                      const JavaJsHookInstallerDependencies& dependencies,
                      JavaJsFieldRecord* out_record,
                      std::string* error_message);
bool InvokeJavaMethod(const JavaJsMethodRecord& record,
                      uint64_t receiver_handle,
                      const JavaJsHookInstallerDependencies& dependencies,
                      const JavaJsValue* args,
                      std::size_t arg_count,
                      JavaJsValue* result,
                      std::string* error_message);
bool ReadJavaField(const JavaJsFieldRecord& record,
                   uint64_t receiver_handle,
                   const JavaJsHookInstallerDependencies& dependencies,
                   JavaJsValue* result,
                   std::string* error_message);
bool WriteJavaField(const JavaJsFieldRecord& record,
                    uint64_t receiver_handle,
                    const JavaJsHookInstallerDependencies& dependencies,
                    const JavaJsValue& value,
                    std::string* error_message);
bool RetainJavaObject(uint64_t object_handle,
                      const JavaJsHookInstallerDependencies& dependencies,
                      uint64_t* retained_handle,
                      std::string* error_message);
bool ReleaseJavaObject(uint64_t object_handle,
                       const JavaJsHookInstallerDependencies& dependencies,
                       std::string* error_message);
bool EnumerateJavaObjects(const std::string& class_name,
                          uint64_t loader_handle,
                          const JavaJsHookInstallerDependencies& dependencies,
                          std::vector<JavaJsValue>* matches,
                          std::string* error_message);
bool EnumerateLoadedJavaClasses(const JavaJsHookInstallerDependencies& dependencies,
                                std::vector<std::string>* class_names,
                                std::string* error_message);
bool EnumerateJavaClassLoaders(const JavaJsHookInstallerDependencies& dependencies,
                               std::vector<JavaJsValue>* matches,
                               std::string* error_message);
bool RegisterJavaClass(const JavaJsRegisterClassRequest& request,
                       const JavaJsHookInstallerDependencies& dependencies,
                       JavaJsValue* result,
                       std::string* error_message);
bool DispatchJavaJsHookInvocationForTesting(uint32_t hook_id,
                                            const JavaJsValue* args,
                                            std::size_t arg_count,
                                            JavaJsValue* result,
                                            std::string* error_message);
bool DispatchJavaJsHookInvocationForTesting(uint32_t hook_id,
                                            uint64_t receiver_handle,
                                            const JavaJsValue* args,
                                            std::size_t arg_count,
                                            JavaJsValue* result,
                                            std::string* error_message);
bool JavaParameterDescriptorAcceptsArgumentForTesting(const std::string& parameter_descriptor,
                                                      const std::string& argument_descriptor);
JavaMethodSpecificityComparisonForTesting CompareJavaMethodSpecificityForTesting(
    const std::vector<std::string>& left_parameter_descriptors,
    const std::vector<std::string>& right_parameter_descriptors,
    JavaDescriptorIsAssignableFromForTesting is_assignable_from,
    void* opaque);
JavaOverloadMatchResultForTesting ResolveMostSpecificJavaOverloadForTesting(
    const std::vector<std::vector<std::string>>& candidate_parameter_descriptors,
    const std::vector<std::string>& argument_descriptors,
    JavaDescriptorIsAssignableFromForTesting is_assignable_from,
    void* opaque,
    std::size_t* matched_index);
bool NormalizeJavaDescriptorForClassLookupForTesting(const std::string& descriptor,
                                                     std::string* class_name_out);
bool ChooseJavaArrayElementDescriptorForTesting(
    const std::string& target_array_descriptor,
    const std::string& source_array_type_name,
    JavaDescriptorIsAssignableFromForTesting is_assignable_from,
    void* opaque,
    std::string* element_descriptor_out);
std::string FormatJavaArrayElementErrorForTesting(const std::string& array_type_name,
                                                  std::size_t index,
                                                  const std::string& nested_error);
bool ConvertJavaReturnArrayForTesting(const std::string& descriptor,
                                      const std::vector<JavaJsValue>& source_elements,
                                      JavaJsValue* out_value,
                                      std::string* error_message);
void SetJavaJsHookInvocationDispatcher(JavaJsHookInvocationDispatcher dispatcher);
void ResetJavaJsHookInvocationDispatcher();

std::size_t GetInstalledJavaJsHookCountForTesting();
bool GetJavaJsHookRecordForTesting(uint32_t hook_id, JavaJsHookRecord* out_record);
void ResetJavaJsHookRegistryForTesting();

}  // namespace agent_runtime
}  // namespace nook
