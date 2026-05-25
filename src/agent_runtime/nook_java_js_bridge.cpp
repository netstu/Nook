#include "agent_runtime/nook_java_js_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <unistd.h>

#if defined(__ANDROID__)
#include "agent_runtime/generated/nook_register_class_helper_dex.h"
#include "nook/NookJavaHook.h"
#include "framework/NookJavaHookInternal.h"
#include "java_hook/JavaHook.h"
#include "java_hook/deferred/java_hook_loader_resolver.h"
#endif

namespace nook {
namespace agent_runtime {

bool DispatchJavaRegisteredClassInvocationToRuntime(uint32_t callback_id,
                                                    uint64_t receiver_handle,
                                                    const std::string& receiver_class_name,
                                                    const std::string& method_name,
                                                    const std::string& method_signature,
                                                    const JavaJsValue* args,
                                                    size_t arg_count,
                                                    JavaJsValue* result,
                                                    std::string* error_message);

namespace {

constexpr const char* kJavaInvokeNullTypeCandidate = "__nook_null__";

constexpr uint32_t kInvalidJavaJsHookSlot = std::numeric_limits<uint32_t>::max();
#if defined(__ANDROID__)
constexpr size_t kMaxJavaJsHookSlots = 16u;
#endif

struct JavaJsHookRegistryState {
    std::mutex mutex;
    uint32_t next_hook_id = 1u;
    std::unordered_map<uint32_t, JavaJsHookRecord> records;
    JavaJsHookInvocationDispatcher dispatcher = nullptr;
#if defined(__ANDROID__)
    std::array<bool, kMaxJavaJsHookSlots> slot_used = {};
    std::array<uint32_t, kMaxJavaJsHookSlots> hook_id_by_slot = {};
#endif
};

JavaJsHookRegistryState& GetJavaJsHookRegistryState() {
    static JavaJsHookRegistryState state;
    return state;
}

#if defined(__ANDROID__)
constexpr const char* kRegisterClassHelperClassName = "nook.java.NookJsInvocationHandler";
constexpr const char* kRegisterClassHelperDexName = "nook_register_class_helper.dex";
constexpr const char* kRegisterClassHelperNativeMethodName = "nativeInvoke";
constexpr const char* kRegisterClassHelperNativeMethodSignature =
    "(JLjava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;";

struct JavaRegisterClassHelperState {
    std::mutex mutex;
    jobject helper_loader = nullptr;
    jclass helper_class = nullptr;
    bool native_registered = false;
};

JavaRegisterClassHelperState& GetJavaRegisterClassHelperState() {
    static JavaRegisterClassHelperState state;
    return state;
}
#endif

void SetError(std::string* error_message, const char* message) {
    if (error_message != nullptr) {
        *error_message = message != nullptr ? message : "";
    }
}

std::string FormatJavaArrayElementError(const std::string& array_type_name,
                                        std::size_t index,
                                        const std::string& nested_error) {
    std::ostringstream stream;
    stream << "Java array ";
    if (!array_type_name.empty()) {
        stream << array_type_name;
    } else {
        stream << "<unknown>";
    }
    stream << " element[" << index << "]";
    if (!nested_error.empty()) {
        stream << ": " << nested_error;
    }
    return stream.str();
}

bool IsValidRequest(const JavaJsHookRequest& request, std::string* error_message) {
    if (request.class_name.empty()) {
        SetError(error_message, "java hook class name is required");
        return false;
    }
    if (request.method_name.empty()) {
        SetError(error_message, "java hook method name is required");
        return false;
    }
    if (request.signature.empty()) {
        SetError(error_message, "java hook signature is required");
        return false;
    }
    return true;
}

JavaJsHookRecord MakeRecordFromRequest(const JavaJsHookRequest& request) {
    JavaJsHookRecord record = {};
    record.hook_id = request.hook_id;
    record.class_name = request.class_name;
    record.method_name = request.method_name;
    record.signature = request.signature;
    record.loader_handle = request.loader_handle;
    record.is_static = request.is_static;
    record.deferred = request.deferred;
    record.callback_slot = kInvalidJavaJsHookSlot;
    return record;
}

bool DispatchJavaJsHookInvocation(uint32_t hook_id,
                                  uint64_t receiver_handle,
                                  const JavaJsValue* args,
                                  std::size_t arg_count,
                                  JavaJsValue* result,
                                  std::string* error_message) {
    JavaJsHookInvocationDispatcher dispatcher = nullptr;
    {
        JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
        std::lock_guard<std::mutex> lock(state.mutex);
        dispatcher = state.dispatcher;
    }
    if (dispatcher == nullptr) {
        SetError(error_message, "java hook dispatcher is not configured");
        return false;
    }
    return dispatcher(hook_id, receiver_handle, args, arg_count, result, error_message);
}

bool NormalizeJavaFieldValue(const std::string& descriptor,
                             const JavaJsValue& input,
                             JavaJsValue* output,
                             std::string* error_message);
bool DescriptorToJavaArrayTypeName(const std::string& descriptor,
                                   std::string* type_name_out,
                                   std::string* error_message);
#if defined(__ANDROID__)
constexpr const char* kJavaJsBridgeLogTag = "NookJavaJsBridge";
#define NOOK_JAVA_JS_LOGI(...) \
    ((void)__android_log_print(ANDROID_LOG_INFO, kJavaJsBridgeLogTag, __VA_ARGS__))
#define NOOK_JAVA_JS_LOGE(...) \
    ((void)__android_log_print(ANDROID_LOG_ERROR, kJavaJsBridgeLogTag, __VA_ARGS__))

void ClearJniException(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

bool DescribeJavaObject(JNIEnv* env,
                        jobject object,
                        std::string* out_class_name,
                        std::string* error_message);
bool ReadJavaStringUtf8(JNIEnv* env, jstring value, std::string* out_text);
bool DescribeJavaClassObject(JNIEnv* env, jobject class_object, std::string* out_descriptor);
bool DescriptorToDotClassName(const std::string& descriptor,
                              std::string* class_name_out,
                              std::string* error_message);
bool IsJavaConstructorMethodName(const std::string& method_name);
struct ScopedJavaLocalRefs;
jclass ResolveJavaClass(JNIEnv* env,
                        const std::string& class_name,
                        uint64_t loader_handle,
                        std::string* error_message);
bool ConvertJavaJsValueToNookJavaHookValue(JNIEnv* env,
                                           const std::string& descriptor,
                                           const JavaJsValue& value,
                                           ScopedJavaLocalRefs* local_refs,
                                           NookJavaHookValue* out_value,
                                           std::string* error_message);
bool ConvertJavaJsArrayToJniArray(JNIEnv* env,
                                  const std::string& descriptor,
                                  const JavaJsValue& value,
                                  ScopedJavaLocalRefs* local_refs,
                                  jobject* out_array,
                                  std::string* error_message);
bool ConvertJavaReturnArrayToJavaJsValue(JNIEnv* env,
                                         const std::string& descriptor,
                                         jobject array_object,
                                         JavaJsValue* out_value,
                                         std::string* error_message);
bool TypeNameToDescriptor(const std::string& type_name,
                          std::string* descriptor_out,
                          std::string* error_message);
bool JavaParameterDescriptorAcceptsArgument(const std::string& parameter_descriptor,
                                            const std::string& argument_descriptor);
bool DecodeSingleUtf8CodePointToJchar(const std::string& text, jchar* out_value);

bool ThrowJavaRuntimeException(JNIEnv* env, const std::string& message) {
    if (env == nullptr) {
        return false;
    }
    jclass runtime_exception_class = env->FindClass("java/lang/RuntimeException");
    if (runtime_exception_class == nullptr) {
        ClearJniException(env);
        return false;
    }
    env->ThrowNew(runtime_exception_class, message.c_str());
    env->DeleteLocalRef(runtime_exception_class);
    return env->ExceptionCheck() == JNI_TRUE;
}

bool DecodeSingleUtf8CodePointToJchar(const std::string& text, jchar* out_value) {
    if (out_value == nullptr || text.empty()) {
        return false;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const size_t length = text.size();
    uint32_t code_point = 0u;
    size_t consumed = 0u;

    if ((bytes[0] & 0x80u) == 0u) {
        code_point = bytes[0];
        consumed = 1u;
    } else if ((bytes[0] & 0xE0u) == 0xC0u &&
               length >= 2u &&
               (bytes[1] & 0xC0u) == 0x80u) {
        code_point = (static_cast<uint32_t>(bytes[0] & 0x1Fu) << 6u) |
                     static_cast<uint32_t>(bytes[1] & 0x3Fu);
        consumed = 2u;
        if (code_point < 0x80u) {
            return false;
        }
    } else if ((bytes[0] & 0xF0u) == 0xE0u &&
               length >= 3u &&
               (bytes[1] & 0xC0u) == 0x80u &&
               (bytes[2] & 0xC0u) == 0x80u) {
        code_point = (static_cast<uint32_t>(bytes[0] & 0x0Fu) << 12u) |
                     (static_cast<uint32_t>(bytes[1] & 0x3Fu) << 6u) |
                     static_cast<uint32_t>(bytes[2] & 0x3Fu);
        consumed = 3u;
        if (code_point < 0x800u) {
            return false;
        }
    } else {
        return false;
    }

    if (consumed != length) {
        return false;
    }
    if (code_point > 0xFFFFu) {
        return false;
    }
    if (code_point >= 0xD800u && code_point <= 0xDFFFu) {
        return false;
    }

    *out_value = static_cast<jchar>(code_point);
    return true;
}

bool WriteRegisterClassHelperDexToPath(const std::string& dex_path, std::string* error_message) {
    std::ofstream output(dex_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        SetError(error_message, "registerClass helper dex open failed");
        return false;
    }
    output.write(reinterpret_cast<const char*>(kNookRegisterClassHelperDex),
                 static_cast<std::streamsize>(kNookRegisterClassHelperDexSize));
    if (!output.good()) {
        SetError(error_message, "registerClass helper dex write failed");
        return false;
    }
    return true;
}

bool BoxJavaBooleanObject(JNIEnv* env,
                          bool value,
                          ScopedJavaLocalRefs* local_refs,
                          jobject* out_object,
                          std::string* error_message);
bool BoxJavaIntegerObject(JNIEnv* env,
                          int32_t value,
                          ScopedJavaLocalRefs* local_refs,
                          jobject* out_object,
                          std::string* error_message);
bool BoxJavaLongObject(JNIEnv* env,
                       int64_t value,
                       ScopedJavaLocalRefs* local_refs,
                       jobject* out_object,
                       std::string* error_message);
bool BoxJavaFloatObject(JNIEnv* env,
                        float value,
                        ScopedJavaLocalRefs* local_refs,
                        jobject* out_object,
                        std::string* error_message);
bool BoxJavaDoubleObject(JNIEnv* env,
                         double value,
                         ScopedJavaLocalRefs* local_refs,
                         jobject* out_object,
                         std::string* error_message);
bool ConvertJavaObjectToJavaJsValueForRegisterClass(JNIEnv* env,
                                                    jobject object,
                                                    JavaJsValue* out_value,
                                                    std::string* error_message);
bool ConvertJavaJsValueToJavaObjectForRegisterClass(JNIEnv* env,
                                                    const std::string& return_descriptor,
                                                    const JavaJsValue& value,
                                                    ScopedJavaLocalRefs* local_refs,
                                                    jobject* out_object,
                                                    std::string* error_message);
bool HandleRegisterClassObjectMethod(JNIEnv* env,
                                     uint32_t callback_id,
                                     jobject proxy,
                                     const std::string& method_name,
                                     jobjectArray args,
                                     ScopedJavaLocalRefs* local_refs,
                                     jobject* out_object,
                                     std::string* error_message);
jobject JNICALL NookRegisterClassNativeInvoke(JNIEnv* env,
                                              jclass,
                                              jlong callback_id,
                                              jobject proxy,
                                              jobject method,
                                              jobjectArray args);
bool EnsureRegisterClassHelperReady(JNIEnv* env,
                                    jobject* helper_loader_out,
                                    jclass* helper_class_out,
                                    std::string* error_message);
#endif

#if defined(__ANDROID__)

struct ParsedJavaMethodSignature {
    std::vector<std::string> arg_descriptors;
    std::string return_descriptor;
};

struct ScopedJavaLocalRefs {
    JNIEnv* env = nullptr;
    std::vector<jobject> refs;

    ~ScopedJavaLocalRefs() {
        if (env == nullptr) {
            return;
        }
        for (jobject ref : refs) {
            if (ref != nullptr) {
                env->DeleteLocalRef(ref);
            }
        }
    }
};

bool BoxJavaBooleanObject(JNIEnv* env,
                          bool value,
                          ScopedJavaLocalRefs* local_refs,
                          jobject* out_object,
                          std::string* error_message) {
    (void)local_refs;
    jclass clazz = env->FindClass("java/lang/Boolean");
    if (clazz == nullptr) {
        ClearJniException(env);
        SetError(error_message, "java.lang.Boolean is unavailable");
        return false;
    }
    jmethodID value_of = env->GetStaticMethodID(clazz, "valueOf", "(Z)Ljava/lang/Boolean;");
    if (value_of == nullptr) {
        env->DeleteLocalRef(clazz);
        ClearJniException(env);
        SetError(error_message, "Boolean.valueOf is unavailable");
        return false;
    }
    jobject boxed = env->CallStaticObjectMethod(clazz, value_of, value ? JNI_TRUE : JNI_FALSE);
    env->DeleteLocalRef(clazz);
    if (boxed == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "Boolean.valueOf failed");
        return false;
    }
    *out_object = boxed;
    return true;
}

bool BoxJavaIntegerObject(JNIEnv* env,
                          int32_t value,
                          ScopedJavaLocalRefs* local_refs,
                          jobject* out_object,
                          std::string* error_message) {
    (void)local_refs;
    jclass clazz = env->FindClass("java/lang/Integer");
    if (clazz == nullptr) {
        ClearJniException(env);
        SetError(error_message, "java.lang.Integer is unavailable");
        return false;
    }
    jmethodID value_of = env->GetStaticMethodID(clazz, "valueOf", "(I)Ljava/lang/Integer;");
    if (value_of == nullptr) {
        env->DeleteLocalRef(clazz);
        ClearJniException(env);
        SetError(error_message, "Integer.valueOf is unavailable");
        return false;
    }
    jobject boxed = env->CallStaticObjectMethod(clazz, value_of, static_cast<jint>(value));
    env->DeleteLocalRef(clazz);
    if (boxed == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "Integer.valueOf failed");
        return false;
    }
    *out_object = boxed;
    return true;
}

bool BoxJavaLongObject(JNIEnv* env,
                       int64_t value,
                       ScopedJavaLocalRefs* local_refs,
                       jobject* out_object,
                       std::string* error_message) {
    (void)local_refs;
    jclass clazz = env->FindClass("java/lang/Long");
    if (clazz == nullptr) {
        ClearJniException(env);
        SetError(error_message, "java.lang.Long is unavailable");
        return false;
    }
    jmethodID value_of = env->GetStaticMethodID(clazz, "valueOf", "(J)Ljava/lang/Long;");
    if (value_of == nullptr) {
        env->DeleteLocalRef(clazz);
        ClearJniException(env);
        SetError(error_message, "Long.valueOf is unavailable");
        return false;
    }
    jobject boxed = env->CallStaticObjectMethod(clazz, value_of, static_cast<jlong>(value));
    env->DeleteLocalRef(clazz);
    if (boxed == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "Long.valueOf failed");
        return false;
    }
    *out_object = boxed;
    return true;
}

bool BoxJavaFloatObject(JNIEnv* env,
                        float value,
                        ScopedJavaLocalRefs* local_refs,
                        jobject* out_object,
                        std::string* error_message) {
    (void)local_refs;
    jclass clazz = env->FindClass("java/lang/Float");
    if (clazz == nullptr) {
        ClearJniException(env);
        SetError(error_message, "java.lang.Float is unavailable");
        return false;
    }
    jmethodID value_of = env->GetStaticMethodID(clazz, "valueOf", "(F)Ljava/lang/Float;");
    if (value_of == nullptr) {
        env->DeleteLocalRef(clazz);
        ClearJniException(env);
        SetError(error_message, "Float.valueOf is unavailable");
        return false;
    }
    jobject boxed = env->CallStaticObjectMethod(clazz, value_of, value);
    env->DeleteLocalRef(clazz);
    if (boxed == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "Float.valueOf failed");
        return false;
    }
    *out_object = boxed;
    return true;
}

bool BoxJavaDoubleObject(JNIEnv* env,
                         double value,
                         ScopedJavaLocalRefs* local_refs,
                         jobject* out_object,
                         std::string* error_message) {
    (void)local_refs;
    jclass clazz = env->FindClass("java/lang/Double");
    if (clazz == nullptr) {
        ClearJniException(env);
        SetError(error_message, "java.lang.Double is unavailable");
        return false;
    }
    jmethodID value_of = env->GetStaticMethodID(clazz, "valueOf", "(D)Ljava/lang/Double;");
    if (value_of == nullptr) {
        env->DeleteLocalRef(clazz);
        ClearJniException(env);
        SetError(error_message, "Double.valueOf is unavailable");
        return false;
    }
    jobject boxed = env->CallStaticObjectMethod(clazz, value_of, value);
    env->DeleteLocalRef(clazz);
    if (boxed == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "Double.valueOf failed");
        return false;
    }
    *out_object = boxed;
    return true;
}

bool ConvertJavaObjectToJavaJsValueForRegisterClass(JNIEnv* env,
                                                    jobject object,
                                                    JavaJsValue* out_value,
                                                    std::string* error_message) {
    if (out_value == nullptr) {
        SetError(error_message, "registerClass callback argument output is required");
        return false;
    }
    *out_value = {};
    if (object == nullptr) {
        out_value->kind = JavaJsValueKind::kUndefined;
        return true;
    }

    jclass string_class = env->FindClass("java/lang/String");
    if (string_class != nullptr && env->IsInstanceOf(object, string_class)) {
        out_value->kind = JavaJsValueKind::kString;
        const bool ok =
            ReadJavaStringUtf8(env, reinterpret_cast<jstring>(object), &out_value->string_value);
        env->DeleteLocalRef(string_class);
        if (!ok) {
            SetError(error_message, "registerClass argument string decode failed");
            return false;
        }
        return true;
    }
    if (string_class != nullptr) {
        env->DeleteLocalRef(string_class);
    } else {
        ClearJniException(env);
    }

    struct BoxedDescriptor {
        const char* class_name;
        const char* method_name;
        const char* method_signature;
        JavaJsValueKind kind;
    };
    constexpr BoxedDescriptor kBoxedDescriptors[] = {
        {"java/lang/Boolean", "booleanValue", "()Z", JavaJsValueKind::kBoolean},
        {"java/lang/Integer", "intValue", "()I", JavaJsValueKind::kInt32},
        {"java/lang/Long", "longValue", "()J", JavaJsValueKind::kInt64},
        {"java/lang/Float", "floatValue", "()F", JavaJsValueKind::kFloat},
        {"java/lang/Double", "doubleValue", "()D", JavaJsValueKind::kDouble},
    };
    for (const BoxedDescriptor& descriptor : kBoxedDescriptors) {
        jclass clazz = env->FindClass(descriptor.class_name);
        if (clazz == nullptr) {
            ClearJniException(env);
            continue;
        }
        const bool matches = env->IsInstanceOf(object, clazz) == JNI_TRUE;
        if (!matches) {
            env->DeleteLocalRef(clazz);
            continue;
        }
        jmethodID value_method =
            env->GetMethodID(clazz, descriptor.method_name, descriptor.method_signature);
        env->DeleteLocalRef(clazz);
        if (value_method == nullptr) {
            ClearJniException(env);
            SetError(error_message, "registerClass boxed primitive helper is unavailable");
            return false;
        }
        out_value->kind = descriptor.kind;
        switch (descriptor.kind) {
            case JavaJsValueKind::kBoolean:
                out_value->bool_value = env->CallBooleanMethod(object, value_method) == JNI_TRUE;
                break;
            case JavaJsValueKind::kInt32:
                out_value->int_value = static_cast<int32_t>(env->CallIntMethod(object, value_method));
                break;
            case JavaJsValueKind::kInt64:
                out_value->int64_value =
                    static_cast<int64_t>(env->CallLongMethod(object, value_method));
                break;
            case JavaJsValueKind::kFloat:
                out_value->float_value = env->CallFloatMethod(object, value_method);
                break;
            case JavaJsValueKind::kDouble:
                out_value->double_value = env->CallDoubleMethod(object, value_method);
                break;
            default:
                break;
        }
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "registerClass boxed primitive conversion failed");
            return false;
        }
        return true;
    }

    out_value->kind = JavaJsValueKind::kObject;
    out_value->object_handle = reinterpret_cast<uint64_t>(object);
    out_value->object_handle_is_global = false;
    return DescribeJavaObject(env, object, &out_value->object_class_name, error_message);
}

bool ConvertJavaJsValueToJavaObjectForRegisterClass(JNIEnv* env,
                                                    const std::string& return_descriptor,
                                                    const JavaJsValue& value,
                                                    ScopedJavaLocalRefs* local_refs,
                                                    jobject* out_object,
                                                    std::string* error_message) {
    if (out_object == nullptr) {
        SetError(error_message, "registerClass callback object output is required");
        return false;
    }
    *out_object = nullptr;

    if (return_descriptor == "V") {
        return true;
    }

    if (value.kind == JavaJsValueKind::kObject && value.object_handle != 0u) {
        jobject local_object = env->NewLocalRef(reinterpret_cast<jobject>(value.object_handle));
        if (local_object == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "registerClass callback object NewLocalRef failed");
            return false;
        }
        *out_object = local_object;
        return true;
    }

    if (return_descriptor == "Ljava/lang/String;" ||
        return_descriptor == "Ljava/lang/CharSequence;" ||
        return_descriptor == "Ljava/lang/Object;") {
        if (value.kind == JavaJsValueKind::kString) {
            jstring text = env->NewStringUTF(value.string_value.c_str());
            if (text == nullptr || env->ExceptionCheck()) {
                ClearJniException(env);
                SetError(error_message, "registerClass callback return string allocation failed");
                return false;
            }
            *out_object = text;
            return true;
        }
    }

    if (return_descriptor == "Ljava/lang/Boolean;" || return_descriptor == "Ljava/lang/Object;") {
        JavaJsValue normalized = {};
        if (NormalizeJavaFieldValue("Z", value, &normalized, error_message) &&
            normalized.kind == JavaJsValueKind::kBoolean) {
            return BoxJavaBooleanObject(
                env, normalized.bool_value, local_refs, out_object, error_message);
        }
        if (return_descriptor == "Ljava/lang/Boolean;") {
            return false;
        }
    }
    if (return_descriptor == "Ljava/lang/Integer;" || return_descriptor == "Ljava/lang/Object;") {
        JavaJsValue normalized = {};
        if (NormalizeJavaFieldValue("I", value, &normalized, error_message) &&
            normalized.kind == JavaJsValueKind::kInt32) {
            return BoxJavaIntegerObject(
                env, normalized.int_value, local_refs, out_object, error_message);
        }
        if (return_descriptor == "Ljava/lang/Integer;") {
            return false;
        }
    }
    if (return_descriptor == "Ljava/lang/Long;" || return_descriptor == "Ljava/lang/Object;") {
        JavaJsValue normalized = {};
        if (NormalizeJavaFieldValue("J", value, &normalized, error_message) &&
            normalized.kind == JavaJsValueKind::kInt64) {
            return BoxJavaLongObject(
                env, normalized.int64_value, local_refs, out_object, error_message);
        }
        if (return_descriptor == "Ljava/lang/Long;") {
            return false;
        }
    }
    if (return_descriptor == "Ljava/lang/Float;" || return_descriptor == "Ljava/lang/Object;") {
        JavaJsValue normalized = {};
        if (NormalizeJavaFieldValue("F", value, &normalized, error_message) &&
            normalized.kind == JavaJsValueKind::kFloat) {
            return BoxJavaFloatObject(
                env, normalized.float_value, local_refs, out_object, error_message);
        }
        if (return_descriptor == "Ljava/lang/Float;") {
            return false;
        }
    }
    if (return_descriptor == "Ljava/lang/Double;" || return_descriptor == "Ljava/lang/Object;") {
        JavaJsValue normalized = {};
        if (NormalizeJavaFieldValue("D", value, &normalized, error_message) &&
            normalized.kind == JavaJsValueKind::kDouble) {
            return BoxJavaDoubleObject(
                env, normalized.double_value, local_refs, out_object, error_message);
        }
        if (return_descriptor == "Ljava/lang/Double;") {
            return false;
        }
    }

    if (!return_descriptor.empty() &&
        (return_descriptor.front() == 'L' || return_descriptor.front() == '[') &&
        value.kind == JavaJsValueKind::kUndefined) {
        *out_object = nullptr;
        return true;
    }

    SetError(error_message, "registerClass callback return type is unsupported");
    return false;
}

bool HandleRegisterClassObjectMethod(JNIEnv* env,
                                     uint32_t callback_id,
                                     jobject proxy,
                                     const std::string& method_name,
                                     jobjectArray args,
                                     ScopedJavaLocalRefs* local_refs,
                                     jobject* out_object,
                                     std::string* error_message) {
    if (out_object == nullptr) {
        SetError(error_message, "registerClass object method output is required");
        return false;
    }

    const jsize arg_count = args == nullptr ? 0 : env->GetArrayLength(args);
    if (method_name == "toString" && arg_count == 0) {
        const std::string text =
            "NookProxy(callbackId=" + std::to_string(callback_id) + ")";
        jstring result = env->NewStringUTF(text.c_str());
        if (result == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "registerClass toString allocation failed");
            return false;
        }
        *out_object = result;
        return true;
    }

    if (method_name == "hashCode" && arg_count == 0) {
        jclass system_class = env->FindClass("java/lang/System");
        if (system_class == nullptr) {
            ClearJniException(env);
            SetError(error_message, "java.lang.System is unavailable");
            return false;
        }
        jmethodID identity_hash_code = env->GetStaticMethodID(
            system_class, "identityHashCode", "(Ljava/lang/Object;)I");
        if (identity_hash_code == nullptr) {
            env->DeleteLocalRef(system_class);
            ClearJniException(env);
            SetError(error_message, "System.identityHashCode is unavailable");
            return false;
        }
        const jint identity = env->CallStaticIntMethod(system_class, identity_hash_code, proxy);
        env->DeleteLocalRef(system_class);
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "System.identityHashCode failed");
            return false;
        }
        return BoxJavaIntegerObject(
            env, static_cast<int32_t>(identity), local_refs, out_object, error_message);
    }

    if (method_name == "equals" && arg_count == 1) {
        jobject other = env->GetObjectArrayElement(args, 0);
        const bool same = env->IsSameObject(proxy, other) == JNI_TRUE;
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            if (other != nullptr) {
                env->DeleteLocalRef(other);
            }
            SetError(error_message, "Proxy.equals argument read failed");
            return false;
        }
        if (other != nullptr) {
            env->DeleteLocalRef(other);
        }
        return BoxJavaBooleanObject(env, same, local_refs, out_object, error_message);
    }

    SetError(error_message, "registerClass object method is unsupported");
    return false;
}

bool DescribeJavaReflectedMethodSignature(JNIEnv* env,
                                          jobject method,
                                          jmethodID get_parameter_types,
                                          jmethodID get_return_type,
                                          ScopedJavaLocalRefs* local_refs,
                                          std::string* signature_out,
                                          std::string* error_message) {
    if (env == nullptr || method == nullptr || signature_out == nullptr) {
        SetError(error_message, "registerClass reflected signature outputs are required");
        return false;
    }

    signature_out->clear();

    jobjectArray parameter_types = reinterpret_cast<jobjectArray>(
        env->CallObjectMethod(method, get_parameter_types));
    if (parameter_types == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "registerClass invoke getParameterTypes failed");
        return false;
    }
    if (local_refs != nullptr) {
        local_refs->refs.push_back(parameter_types);
    }

    std::string signature = "(";
    const jsize parameter_count = env->GetArrayLength(parameter_types);
    for (jsize index = 0; index < parameter_count; ++index) {
        jobject parameter_type = env->GetObjectArrayElement(parameter_types, index);
        if (parameter_type == nullptr || env->ExceptionCheck()) {
            if (parameter_type != nullptr) {
                env->DeleteLocalRef(parameter_type);
            }
            ClearJniException(env);
            SetError(error_message, "registerClass invoke parameter type read failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(parameter_type);
        }

        std::string parameter_descriptor;
        if (!DescribeJavaClassObject(env, parameter_type, &parameter_descriptor)) {
            SetError(error_message, "registerClass invoke parameter type describe failed");
            return false;
        }
        signature += parameter_descriptor;
    }
    signature += ")";

    jobject return_type = env->CallObjectMethod(method, get_return_type);
    if (return_type == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "registerClass invoke getReturnType failed");
        return false;
    }
    if (local_refs != nullptr) {
        local_refs->refs.push_back(return_type);
    }

    std::string return_descriptor;
    if (!DescribeJavaClassObject(env, return_type, &return_descriptor)) {
        SetError(error_message, "registerClass invoke return type describe failed");
        return false;
    }
    signature += return_descriptor;

    *signature_out = std::move(signature);
    return true;
}

jobject JNICALL NookRegisterClassNativeInvoke(JNIEnv* env,
                                              jclass,
                                              jlong callback_id,
                                              jobject proxy,
                                              jobject method,
                                              jobjectArray args) {
    if (env == nullptr || method == nullptr) {
        return nullptr;
    }

    ScopedJavaLocalRefs local_refs = {};
    local_refs.env = env;

    jclass method_class = env->GetObjectClass(method);
    if (method_class == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        ThrowJavaRuntimeException(env, "registerClass invoke GetObjectClass failed");
        return nullptr;
    }
    local_refs.refs.push_back(method_class);

    jmethodID get_name = env->GetMethodID(method_class, "getName", "()Ljava/lang/String;");
    jmethodID get_parameter_types =
        env->GetMethodID(method_class, "getParameterTypes", "()[Ljava/lang/Class;");
    jmethodID get_return_type =
        env->GetMethodID(method_class, "getReturnType", "()Ljava/lang/Class;");
    if (get_name == nullptr || get_parameter_types == nullptr || get_return_type == nullptr) {
        ClearJniException(env);
        ThrowJavaRuntimeException(env, "registerClass invoke reflection methods unavailable");
        return nullptr;
    }

    jstring method_name_string = reinterpret_cast<jstring>(env->CallObjectMethod(method, get_name));
    if (method_name_string == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        ThrowJavaRuntimeException(env, "registerClass invoke getName failed");
        return nullptr;
    }
    local_refs.refs.push_back(method_name_string);

    std::string method_name;
    if (!ReadJavaStringUtf8(env, method_name_string, &method_name)) {
        ThrowJavaRuntimeException(env, "registerClass invoke method name decode failed");
        return nullptr;
    }

    std::vector<JavaJsValue> converted_args;
    if (args != nullptr) {
        const jsize arg_count = env->GetArrayLength(args);
        converted_args.reserve(static_cast<size_t>(arg_count));
        for (jsize index = 0; index < arg_count; ++index) {
            jobject arg_object = env->GetObjectArrayElement(args, index);
            if (env->ExceptionCheck()) {
                ClearJniException(env);
                ThrowJavaRuntimeException(env, "registerClass invoke read args failed");
                return nullptr;
            }
            if (arg_object != nullptr) {
                local_refs.refs.push_back(arg_object);
            }
            JavaJsValue converted = {};
            std::string arg_error;
            if (!ConvertJavaObjectToJavaJsValueForRegisterClass(
                    env, arg_object, &converted, &arg_error)) {
                ThrowJavaRuntimeException(env, arg_error);
                return nullptr;
            }
            converted_args.push_back(std::move(converted));
        }
    }

    std::string receiver_class_name;
    if (proxy != nullptr) {
        std::string ignored_error;
        if (!DescribeJavaObject(env, proxy, &receiver_class_name, &ignored_error)) {
            receiver_class_name.clear();
            ClearJniException(env);
        }
    }

    std::string dispatch_error;
    std::string method_signature;
    if (!DescribeJavaReflectedMethodSignature(env,
                                              method,
                                              get_parameter_types,
                                              get_return_type,
                                              &local_refs,
                                              &method_signature,
                                              &dispatch_error)) {
        ThrowJavaRuntimeException(env, dispatch_error);
        return nullptr;
    }

    JavaJsValue callback_result = {};
    if (!DispatchJavaRegisteredClassInvocationToRuntime(static_cast<uint32_t>(callback_id),
                                                        reinterpret_cast<uint64_t>(proxy),
                                                        receiver_class_name,
                                                        method_name,
                                                        method_signature,
                                                        converted_args.empty()
                                                            ? nullptr
                                                            : converted_args.data(),
                                                        converted_args.size(),
                                                        &callback_result,
                                                        &dispatch_error)) {
        jobject object_method_result = nullptr;
        std::string object_method_error;
        if (HandleRegisterClassObjectMethod(env,
                                            static_cast<uint32_t>(callback_id),
                                            proxy,
                                            method_name,
                                            args,
                                            &local_refs,
                                            &object_method_result,
                                            &object_method_error)) {
            return object_method_result;
        }
        ThrowJavaRuntimeException(
            env, dispatch_error.empty() ? object_method_error : dispatch_error);
        return nullptr;
    }

    const size_t return_type_start = method_signature.find(')');
    if (return_type_start == std::string::npos || return_type_start + 1u > method_signature.size()) {
        ThrowJavaRuntimeException(env, "registerClass invoke method signature parse failed");
        return nullptr;
    }
    const std::string return_descriptor = method_signature.substr(return_type_start + 1u);

    jobject result_object = nullptr;
    std::string error_message;
    if (!ConvertJavaJsValueToJavaObjectForRegisterClass(env,
                                                        return_descriptor,
                                                        callback_result,
                                                        &local_refs,
                                                        &result_object,
                                                        &error_message)) {
        ThrowJavaRuntimeException(env, error_message);
        return nullptr;
    }
    return result_object;
}

bool EnsureRegisterClassHelperReady(JNIEnv* env,
                                    jobject* helper_loader_out,
                                    jclass* helper_class_out,
                                    std::string* error_message) {
    if (env == nullptr || helper_loader_out == nullptr || helper_class_out == nullptr) {
        SetError(error_message, "registerClass helper output arguments are invalid");
        return false;
    }

    JavaRegisterClassHelperState& state = GetJavaRegisterClassHelperState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.helper_loader != nullptr && state.helper_class != nullptr && state.native_registered) {
            jobject loader = env->NewLocalRef(state.helper_loader);
            jclass clazz = reinterpret_cast<jclass>(env->NewLocalRef(state.helper_class));
            if (loader == nullptr || clazz == nullptr || env->ExceptionCheck()) {
                if (loader != nullptr) {
                    env->DeleteLocalRef(loader);
                }
                if (clazz != nullptr) {
                    env->DeleteLocalRef(clazz);
                }
                ClearJniException(env);
                SetError(error_message, "registerClass helper cache NewLocalRef failed");
                return false;
            }
            *helper_loader_out = loader;
            *helper_class_out = clazz;
            return true;
        }
    }

    jobject application = JavaHookLoaderResolver::GetCurrentApplication(env);
    if (application == nullptr) {
        SetError(error_message, "registerClass currentApplication is unavailable");
        return false;
    }

    jclass application_class = env->GetObjectClass(application);
    if (application_class == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(application);
        ClearJniException(env);
        SetError(error_message, "registerClass application class lookup failed");
        return false;
    }

    jmethodID get_code_cache_dir =
        env->GetMethodID(application_class, "getCodeCacheDir", "()Ljava/io/File;");
    jmethodID get_class_loader =
        env->GetMethodID(application_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(application_class);
    if (get_code_cache_dir == nullptr || get_class_loader == nullptr) {
        env->DeleteLocalRef(application);
        ClearJniException(env);
        SetError(error_message, "registerClass application helpers are unavailable");
        return false;
    }

    jobject code_cache_dir = env->CallObjectMethod(application, get_code_cache_dir);
    jobject parent_loader = env->CallObjectMethod(application, get_class_loader);
    env->DeleteLocalRef(application);
    if (code_cache_dir == nullptr || parent_loader == nullptr || env->ExceptionCheck()) {
        if (code_cache_dir != nullptr) {
            env->DeleteLocalRef(code_cache_dir);
        }
        if (parent_loader != nullptr) {
            env->DeleteLocalRef(parent_loader);
        }
        ClearJniException(env);
        SetError(error_message, "registerClass application data fetch failed");
        return false;
    }

    jclass file_class = env->GetObjectClass(code_cache_dir);
    if (file_class == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(code_cache_dir);
        env->DeleteLocalRef(parent_loader);
        ClearJniException(env);
        SetError(error_message, "registerClass code cache File class lookup failed");
        return false;
    }
    jmethodID get_absolute_path =
        env->GetMethodID(file_class, "getAbsolutePath", "()Ljava/lang/String;");
    env->DeleteLocalRef(file_class);
    if (get_absolute_path == nullptr) {
        env->DeleteLocalRef(code_cache_dir);
        env->DeleteLocalRef(parent_loader);
        ClearJniException(env);
        SetError(error_message, "registerClass File.getAbsolutePath is unavailable");
        return false;
    }

    jstring code_cache_path_string =
        reinterpret_cast<jstring>(env->CallObjectMethod(code_cache_dir, get_absolute_path));
    env->DeleteLocalRef(code_cache_dir);
    if (code_cache_path_string == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(parent_loader);
        ClearJniException(env);
        SetError(error_message, "registerClass code cache path lookup failed");
        return false;
    }

    std::string code_cache_path;
    const bool read_code_cache_path =
        ReadJavaStringUtf8(env, code_cache_path_string, &code_cache_path);
    env->DeleteLocalRef(code_cache_path_string);
    if (!read_code_cache_path || code_cache_path.empty()) {
        env->DeleteLocalRef(parent_loader);
        SetError(error_message, "registerClass code cache path decode failed");
        return false;
    }

    const std::string dex_path = code_cache_path + "/" + kRegisterClassHelperDexName;
    if (!WriteRegisterClassHelperDexToPath(dex_path, error_message)) {
        env->DeleteLocalRef(parent_loader);
        return false;
    }

    jclass dex_class_loader_class = env->FindClass("dalvik/system/DexClassLoader");
    if (dex_class_loader_class == nullptr) {
        env->DeleteLocalRef(parent_loader);
        ClearJniException(env);
        SetError(error_message, "dalvik.system.DexClassLoader is unavailable");
        return false;
    }
    jmethodID dex_ctor = env->GetMethodID(
        dex_class_loader_class,
        "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    if (dex_ctor == nullptr) {
        env->DeleteLocalRef(dex_class_loader_class);
        env->DeleteLocalRef(parent_loader);
        ClearJniException(env);
        SetError(error_message, "DexClassLoader constructor is unavailable");
        return false;
    }

    jstring dex_path_string = env->NewStringUTF(dex_path.c_str());
    jstring optimized_dir_string = env->NewStringUTF(code_cache_path.c_str());
    if (dex_path_string == nullptr || optimized_dir_string == nullptr || env->ExceptionCheck()) {
        if (dex_path_string != nullptr) {
            env->DeleteLocalRef(dex_path_string);
        }
        if (optimized_dir_string != nullptr) {
            env->DeleteLocalRef(optimized_dir_string);
        }
        env->DeleteLocalRef(dex_class_loader_class);
        env->DeleteLocalRef(parent_loader);
        ClearJniException(env);
        SetError(error_message, "registerClass dex path allocation failed");
        return false;
    }

    jobject helper_loader = env->NewObject(
        dex_class_loader_class, dex_ctor, dex_path_string, optimized_dir_string, nullptr, parent_loader);
    env->DeleteLocalRef(dex_path_string);
    env->DeleteLocalRef(optimized_dir_string);
    env->DeleteLocalRef(dex_class_loader_class);
    env->DeleteLocalRef(parent_loader);
    if (helper_loader == nullptr || env->ExceptionCheck()) {
        if (helper_loader != nullptr) {
            env->DeleteLocalRef(helper_loader);
        }
        ClearJniException(env);
        SetError(error_message, "registerClass helper DexClassLoader creation failed");
        return false;
    }

    jclass helper_class = JavaHookLoaderResolver::LoadClassWithLoader(
        env, helper_loader, kRegisterClassHelperClassName);
    if (helper_class == nullptr) {
        env->DeleteLocalRef(helper_loader);
        ClearJniException(env);
        SetError(error_message, "registerClass helper class load failed");
        return false;
    }

    JNINativeMethod native_method = {};
    native_method.name = const_cast<char*>(kRegisterClassHelperNativeMethodName);
    native_method.signature = const_cast<char*>(kRegisterClassHelperNativeMethodSignature);
    native_method.fnPtr = reinterpret_cast<void*>(&NookRegisterClassNativeInvoke);
    if (env->RegisterNatives(helper_class, &native_method, 1) != JNI_OK || env->ExceptionCheck()) {
        env->DeleteLocalRef(helper_class);
        env->DeleteLocalRef(helper_loader);
        ClearJniException(env);
        SetError(error_message, "registerClass helper RegisterNatives failed");
        return false;
    }

    jobject helper_loader_global = env->NewGlobalRef(helper_loader);
    jclass helper_class_global = reinterpret_cast<jclass>(env->NewGlobalRef(helper_class));
    if (helper_loader_global == nullptr || helper_class_global == nullptr || env->ExceptionCheck()) {
        if (helper_loader_global != nullptr) {
            env->DeleteGlobalRef(helper_loader_global);
        }
        if (helper_class_global != nullptr) {
            env->DeleteGlobalRef(helper_class_global);
        }
        env->DeleteLocalRef(helper_class);
        env->DeleteLocalRef(helper_loader);
        ClearJniException(env);
        SetError(error_message, "registerClass helper global retain failed");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.helper_loader != nullptr) {
            env->DeleteGlobalRef(state.helper_loader);
        }
        if (state.helper_class != nullptr) {
            env->DeleteGlobalRef(state.helper_class);
        }
        state.helper_loader = helper_loader_global;
        state.helper_class = helper_class_global;
        state.native_registered = true;
    }

    *helper_loader_out = helper_loader;
    *helper_class_out = helper_class;
    return true;
}

struct ActiveJavaJsInvocation {
    bool active = false;
    uint32_t hook_id = 0u;
    int installed_hook_id = -1;
    JNIEnv* env = nullptr;
    jobject thiz = nullptr;
    ParsedJavaMethodSignature signature = {};
};

using ActiveJavaJsInvocationStack = std::vector<ActiveJavaJsInvocation>;
using VisitArtClassesFn = void (*)(void* class_linker, void* visitor);
using VisitArtClassesLegacyFn = void (*)(void* class_linker,
                                         bool (*visitor)(void* klass, void* arg),
                                         void* arg);
using SuspendAllArtThreadsFn = void (*)(void* thread_list, const char* cause, bool long_suspend);
using SuspendAllArtThreadsLegacyFn = void (*)(void* thread_list);
using ResumeAllArtThreadsFn = void (*)(void* thread_list);

std::mutex g_active_java_js_invocations_mutex;
std::unordered_map<uint64_t, ActiveJavaJsInvocationStack> g_active_java_js_invocations;

uint64_t GetCurrentThreadIdForJavaJsInvocation() {
    return static_cast<uint64_t>(gettid());
}

bool TryGetActiveJavaJsInvocation(ActiveJavaJsInvocation* invocation_out) {
    if (invocation_out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_active_java_js_invocations_mutex);
    const auto it = g_active_java_js_invocations.find(GetCurrentThreadIdForJavaJsInvocation());
    if (it == g_active_java_js_invocations.end() || it->second.empty()) {
        return false;
    }

    *invocation_out = it->second.back();
    return true;
}

void PushActiveJavaJsInvocationForCurrentThread(const ActiveJavaJsInvocation& invocation) {
    std::lock_guard<std::mutex> lock(g_active_java_js_invocations_mutex);
    ActiveJavaJsInvocationStack& stack =
        g_active_java_js_invocations[GetCurrentThreadIdForJavaJsInvocation()];
    stack.push_back(invocation);
}

void PopActiveJavaJsInvocationForCurrentThread() {
    std::lock_guard<std::mutex> lock(g_active_java_js_invocations_mutex);
    const uint64_t thread_id = GetCurrentThreadIdForJavaJsInvocation();
    const auto it = g_active_java_js_invocations.find(thread_id);
    if (it == g_active_java_js_invocations.end()) {
        return;
    }

    ActiveJavaJsInvocationStack& stack = it->second;
    if (!stack.empty()) {
        stack.pop_back();
    }
    if (stack.empty()) {
        g_active_java_js_invocations.erase(thread_id);
    }
}

bool ParseTypeDescriptor(const std::string& signature,
                         size_t* index,
                         std::string* descriptor_out) {
    if (index == nullptr || descriptor_out == nullptr || *index >= signature.size()) {
        return false;
    }

    const char ch = signature[*index];
    if (ch == 'Z' || ch == 'B' || ch == 'C' || ch == 'S' ||
        ch == 'I' || ch == 'J' || ch == 'F' || ch == 'V' || ch == 'D') {
        *descriptor_out = std::string(1, ch);
        ++(*index);
        return true;
    }

    if (ch == '[') {
        const size_t array_start = *index;
        ++(*index);
        std::string component_descriptor;
        if (!ParseTypeDescriptor(signature, index, &component_descriptor)) {
            return false;
        }
        *descriptor_out = signature.substr(array_start, *index - array_start);
        return true;
    }

    if (ch == 'L') {
        const size_t end = signature.find(';', *index);
        if (end == std::string::npos) {
            return false;
        }
        *descriptor_out = signature.substr(*index, end - *index + 1u);
        *index = end + 1u;
        return true;
    }

    return false;
}

bool ParseMethodSignature(const std::string& signature,
                          ParsedJavaMethodSignature* parsed_out,
                          std::string* error_message) {
    if (parsed_out == nullptr) {
        SetError(error_message, "parsed signature output is required");
        return false;
    }
    parsed_out->arg_descriptors.clear();
    parsed_out->return_descriptor.clear();

    if (signature.size() < 3u || signature.front() != '(') {
        SetError(error_message, "invalid Java method signature");
        return false;
    }

    size_t index = 1u;
    while (index < signature.size() && signature[index] != ')') {
        std::string descriptor;
        if (!ParseTypeDescriptor(signature, &index, &descriptor)) {
            SetError(error_message, "unsupported Java argument type in signature");
            return false;
        }
        parsed_out->arg_descriptors.push_back(std::move(descriptor));
    }

    if (index >= signature.size() || signature[index] != ')') {
        SetError(error_message, "invalid Java method signature terminator");
        return false;
    }
    ++index;

    if (!ParseTypeDescriptor(signature, &index, &parsed_out->return_descriptor) ||
        index != signature.size()) {
        SetError(error_message, "unsupported Java return type in signature");
        return false;
    }
    return true;
}

bool IsSupportedJavaFieldDescriptor(const std::string& descriptor) {
    return descriptor == "Z" ||
           descriptor == "I" ||
           descriptor == "J" ||
           descriptor == "F" ||
           descriptor == "D" ||
           descriptor == "Ljava/lang/String;";
}

bool NormalizeJavaFieldValue(const std::string& descriptor,
                             const JavaJsValue& input,
                             JavaJsValue* output,
                             std::string* error_message) {
    if (output == nullptr) {
        SetError(error_message, "normalized Java field output is required");
        return false;
    }

    *output = {};
    if (descriptor == "Z") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kBoolean;
            output->bool_value = false;
            return true;
        }
        if (input.kind != JavaJsValueKind::kBoolean) {
            SetError(error_message, "Java boolean field expects JS boolean");
            return false;
        }
        output->kind = JavaJsValueKind::kBoolean;
        output->bool_value = input.bool_value;
        return true;
    }
    if (descriptor == "I") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kInt32;
            output->int_value = 0;
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble &&
            std::isfinite(input.double_value) &&
            input.double_value >= static_cast<double>(std::numeric_limits<int32_t>::min()) &&
            input.double_value <= static_cast<double>(std::numeric_limits<int32_t>::max()) &&
            std::floor(input.double_value) == input.double_value) {
            output->kind = JavaJsValueKind::kInt32;
            output->int_value = static_cast<int32_t>(input.double_value);
            return true;
        }
        SetError(error_message, "Java int field expects JS int32");
        return false;
    }
    if (descriptor == "J") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kInt64;
            output->int64_value = 0;
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt64) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            output->kind = JavaJsValueKind::kInt64;
            output->int64_value = input.int_value;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble &&
            std::isfinite(input.double_value) &&
            input.double_value >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
            input.double_value <= static_cast<double>(std::numeric_limits<int64_t>::max()) &&
            std::floor(input.double_value) == input.double_value) {
            output->kind = JavaJsValueKind::kInt64;
            output->int64_value = static_cast<int64_t>(input.double_value);
            return true;
        }
        SetError(error_message, "Java long field expects JS integer");
        return false;
    }
    if (descriptor == "F") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = 0.0f;
            return true;
        }
        if (input.kind == JavaJsValueKind::kFloat) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = static_cast<float>(input.double_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = static_cast<float>(input.int_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt64) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = static_cast<float>(input.int64_value);
            return true;
        }
        SetError(error_message, "Java float field expects JS number");
        return false;
    }
    if (descriptor == "D") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = 0.0;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kFloat) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = static_cast<double>(input.float_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = static_cast<double>(input.int_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt64) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = static_cast<double>(input.int64_value);
            return true;
        }
        SetError(error_message, "Java double field expects JS number");
        return false;
    }
    if (descriptor == "Ljava/lang/String;") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kUndefined;
            return true;
        }
        if (input.kind != JavaJsValueKind::kString) {
            SetError(error_message, "Java String field expects JS string");
            return false;
        }
        *output = input;
        return true;
    }

    SetError(error_message, "unsupported Java field descriptor");
    return false;
}

bool ReadJStringUtf8(JNIEnv* env, jstring value, std::string* out_text, std::string* error_message) {
    if (env == nullptr || out_text == nullptr) {
        SetError(error_message, "jni string decode arguments are invalid");
        return false;
    }
    if (value == nullptr) {
        out_text->clear();
        return true;
    }

    const char* utf8 = env->GetStringUTFChars(value, nullptr);
    if (utf8 == nullptr) {
        SetError(error_message, "GetStringUTFChars failed");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return false;
    }
    *out_text = utf8;
    env->ReleaseStringUTFChars(value, utf8);
    return true;
}

bool ConvertNookJavaHookValueToJavaJsValue(JNIEnv* env,
                                           const std::string& descriptor,
                                           const NookJavaHookValue& value,
                                           JavaJsValue* out_value,
                                           std::string* error_message) {
    if (out_value == nullptr) {
        SetError(error_message, "java js value output is required");
        return false;
    }
    *out_value = {};

    if (descriptor == "V") {
        out_value->kind = JavaJsValueKind::kUndefined;
        return true;
    }
    if (descriptor == "Z") {
        out_value->kind = JavaJsValueKind::kBoolean;
        out_value->bool_value = value.z != 0;
        return true;
    }
    if (descriptor == "I") {
        out_value->kind = JavaJsValueKind::kInt32;
        out_value->int_value = static_cast<int32_t>(value.i);
        return true;
    }
    if (descriptor == "J") {
        out_value->kind = JavaJsValueKind::kInt64;
        out_value->int64_value = static_cast<int64_t>(value.j);
        return true;
    }
    if (descriptor == "F") {
        out_value->kind = JavaJsValueKind::kFloat;
        out_value->float_value = value.f;
        return true;
    }
    if (descriptor == "D") {
        out_value->kind = JavaJsValueKind::kDouble;
        out_value->double_value = value.d;
        return true;
    }
    if (descriptor == "Ljava/lang/String;") {
        out_value->kind = JavaJsValueKind::kString;
        return ReadJStringUtf8(env,
                               reinterpret_cast<jstring>(value.l),
                               &out_value->string_value,
                               error_message);
    }
    if (!descriptor.empty() && descriptor.front() == '[') {
        return ConvertJavaReturnArrayToJavaJsValue(
            env, descriptor, reinterpret_cast<jobject>(value.l), out_value, error_message);
    }
    if (!descriptor.empty() && descriptor.front() == 'L') {
        if (value.l == nullptr) {
            out_value->kind = JavaJsValueKind::kUndefined;
            return true;
        }
        jobject retained = env->NewGlobalRef(reinterpret_cast<jobject>(value.l));
        if (retained == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            SetError(error_message, "NewGlobalRef failed");
            return false;
        }
        out_value->kind = JavaJsValueKind::kObject;
        out_value->object_handle = reinterpret_cast<uint64_t>(retained);
        out_value->object_handle_is_global = true;
        if (!DescribeJavaObject(env,
                                reinterpret_cast<jobject>(value.l),
                                &out_value->object_class_name,
                                error_message)) {
            env->DeleteGlobalRef(retained);
            out_value->object_handle = 0u;
            out_value->object_handle_is_global = false;
            return false;
        }
        return true;
    }

    SetError(error_message, "unsupported Java descriptor for JS conversion");
    return false;
}

bool ConvertJavaReturnArrayToJavaJsValue(JNIEnv* env,
                                         const std::string& descriptor,
                                         jobject array_object,
                                         JavaJsValue* out_value,
                                         std::string* error_message) {
    if (env == nullptr || out_value == nullptr) {
        SetError(error_message, "java return array conversion arguments are invalid");
        return false;
    }
    if (descriptor.empty() || descriptor.front() != '[') {
        SetError(error_message, "java return array descriptor is invalid");
        return false;
    }

    *out_value = {};
    out_value->kind = JavaJsValueKind::kArray;
    if (!DescriptorToJavaArrayTypeName(descriptor, &out_value->array_type_name, error_message)) {
        return false;
    }
    if (array_object == nullptr) {
        return true;
    }

    const std::string component_descriptor = descriptor.substr(1u);
    const jsize count = env->GetArrayLength(reinterpret_cast<jarray>(array_object));
    if (env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "GetArrayLength failed");
        return false;
    }
    out_value->array_elements.reserve(static_cast<size_t>(count));

    auto append_scalar = [&](const NookJavaHookValue& raw_element) -> bool {
        JavaJsValue element_value = {};
        if (!ConvertNookJavaHookValueToJavaJsValue(
                env, component_descriptor, raw_element, &element_value, error_message)) {
            return false;
        }
        out_value->array_elements.push_back(std::move(element_value));
        return true;
    };

    if (component_descriptor == "Z") {
        std::vector<jboolean> elements(static_cast<size_t>(count));
        env->GetBooleanArrayRegion(reinterpret_cast<jbooleanArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetBooleanArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue raw_element = {};
            raw_element.z = (elements[static_cast<size_t>(i)] != JNI_FALSE) ? 1 : 0;
            if (!append_scalar(raw_element)) {
                return false;
            }
        }
        return true;
    }
    if (component_descriptor == "B") {
        std::vector<jbyte> elements(static_cast<size_t>(count));
        env->GetByteArrayRegion(reinterpret_cast<jbyteArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetByteArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue raw_element = {};
            raw_element.b = static_cast<unsigned char>(elements[static_cast<size_t>(i)]);
            if (!append_scalar(raw_element)) {
                return false;
            }
        }
        return true;
    }
    if (component_descriptor == "S") {
        std::vector<jshort> elements(static_cast<size_t>(count));
        env->GetShortArrayRegion(reinterpret_cast<jshortArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetShortArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue raw_element = {};
            raw_element.s = static_cast<unsigned short>(elements[static_cast<size_t>(i)]);
            if (!append_scalar(raw_element)) {
                return false;
            }
        }
        return true;
    }
    if (component_descriptor == "C") {
        std::vector<jchar> elements(static_cast<size_t>(count));
        env->GetCharArrayRegion(reinterpret_cast<jcharArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetCharArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            JavaJsValue element_value = {};
            element_value.kind = JavaJsValueKind::kString;
            element_value.string_value.assign(1u, static_cast<char>(elements[static_cast<size_t>(i)]));
            out_value->array_elements.push_back(std::move(element_value));
        }
        return true;
    }
    if (component_descriptor == "I") {
        std::vector<jint> elements(static_cast<size_t>(count));
        env->GetIntArrayRegion(reinterpret_cast<jintArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetIntArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue raw_element = {};
            raw_element.i = elements[static_cast<size_t>(i)];
            if (!append_scalar(raw_element)) {
                return false;
            }
        }
        return true;
    }
    if (component_descriptor == "J") {
        std::vector<jlong> elements(static_cast<size_t>(count));
        env->GetLongArrayRegion(reinterpret_cast<jlongArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetLongArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue raw_element = {};
            raw_element.j = elements[static_cast<size_t>(i)];
            if (!append_scalar(raw_element)) {
                return false;
            }
        }
        return true;
    }
    if (component_descriptor == "F") {
        std::vector<jfloat> elements(static_cast<size_t>(count));
        env->GetFloatArrayRegion(reinterpret_cast<jfloatArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetFloatArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue raw_element = {};
            raw_element.f = elements[static_cast<size_t>(i)];
            if (!append_scalar(raw_element)) {
                return false;
            }
        }
        return true;
    }
    if (component_descriptor == "D") {
        std::vector<jdouble> elements(static_cast<size_t>(count));
        env->GetDoubleArrayRegion(reinterpret_cast<jdoubleArray>(array_object), 0, count, elements.data());
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "GetDoubleArrayRegion failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue raw_element = {};
            raw_element.d = elements[static_cast<size_t>(i)];
            if (!append_scalar(raw_element)) {
                return false;
            }
        }
        return true;
    }

    for (jsize i = 0; i < count; ++i) {
        jobject element_object = env->GetObjectArrayElement(reinterpret_cast<jobjectArray>(array_object), i);
        if (env->ExceptionCheck()) {
            if (element_object != nullptr) {
                env->DeleteLocalRef(element_object);
            }
            ClearJniException(env);
            SetError(error_message, "GetObjectArrayElement failed");
            return false;
        }
        NookJavaHookValue raw_element = {};
        raw_element.l = element_object;
        const bool ok = append_scalar(raw_element);
        if (element_object != nullptr) {
            env->DeleteLocalRef(element_object);
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool ConvertJavaJsValueToNookJavaHookValue(JNIEnv* env,
                                           const std::string& descriptor,
                                           const JavaJsValue& value,
                                           ScopedJavaLocalRefs* local_refs,
                                           NookJavaHookValue* out_value,
                                           std::string* error_message) {
    if (out_value == nullptr) {
        SetError(error_message, "java hook value output is required");
        return false;
    }
    *out_value = {};

    if (descriptor == "V") {
        return true;
    }
    if (descriptor == "Z") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->z = 0;
            return true;
        }
        if (value.kind != JavaJsValueKind::kBoolean) {
            SetError(error_message, "Java boolean expects JS boolean");
            return false;
        }
        out_value->z = value.bool_value ? 1 : 0;
        return true;
    }
    if (descriptor == "I") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->i = 0;
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt32) {
            out_value->i = value.int_value;
            return true;
        }
        if (value.kind == JavaJsValueKind::kDouble &&
            std::isfinite(value.double_value) &&
            value.double_value >= static_cast<double>(std::numeric_limits<int32_t>::min()) &&
            value.double_value <= static_cast<double>(std::numeric_limits<int32_t>::max()) &&
            std::floor(value.double_value) == value.double_value) {
            out_value->i = static_cast<jint>(value.double_value);
            return true;
        }
        SetError(error_message, "Java int expects JS int32");
        return false;
    }
    if (descriptor == "B") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->b = 0;
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt32 &&
            value.int_value >= static_cast<int32_t>(std::numeric_limits<int8_t>::min()) &&
            value.int_value <= static_cast<int32_t>(std::numeric_limits<int8_t>::max())) {
            out_value->b = static_cast<jbyte>(value.int_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kDouble &&
            std::isfinite(value.double_value) &&
            value.double_value >= static_cast<double>(std::numeric_limits<int8_t>::min()) &&
            value.double_value <= static_cast<double>(std::numeric_limits<int8_t>::max()) &&
            std::floor(value.double_value) == value.double_value) {
            out_value->b = static_cast<jbyte>(value.double_value);
            return true;
        }
        SetError(error_message, "Java byte expects JS integer");
        return false;
    }
    if (descriptor == "S") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->s = 0;
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt32 &&
            value.int_value >= static_cast<int32_t>(std::numeric_limits<int16_t>::min()) &&
            value.int_value <= static_cast<int32_t>(std::numeric_limits<int16_t>::max())) {
            out_value->s = static_cast<jshort>(value.int_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kDouble &&
            std::isfinite(value.double_value) &&
            value.double_value >= static_cast<double>(std::numeric_limits<int16_t>::min()) &&
            value.double_value <= static_cast<double>(std::numeric_limits<int16_t>::max()) &&
            std::floor(value.double_value) == value.double_value) {
            out_value->s = static_cast<jshort>(value.double_value);
            return true;
        }
        SetError(error_message, "Java short expects JS integer");
        return false;
    }
    if (descriptor == "C") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->c = 0;
            return true;
        }
        if (value.kind != JavaJsValueKind::kString) {
            SetError(error_message, "Java char expects JS string");
            return false;
        }
        jchar converted = 0;
        if (!DecodeSingleUtf8CodePointToJchar(value.string_value, &converted)) {
            SetError(error_message, "Java char expects single UTF-8 code point string");
            return false;
        }
        out_value->c = converted;
        return true;
    }
    if (descriptor == "J") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->j = 0;
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt64) {
            out_value->j = static_cast<jlong>(value.int64_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt32) {
            out_value->j = static_cast<jlong>(value.int_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kDouble &&
            std::isfinite(value.double_value) &&
            value.double_value >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
            value.double_value <= static_cast<double>(std::numeric_limits<int64_t>::max()) &&
            std::floor(value.double_value) == value.double_value) {
            out_value->j = static_cast<jlong>(value.double_value);
            return true;
        }
        SetError(error_message, "Java long expects JS integer");
        return false;
    }
    if (descriptor == "F") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->f = 0.0f;
            return true;
        }
        if (value.kind == JavaJsValueKind::kFloat) {
            out_value->f = value.float_value;
            return true;
        }
        if (value.kind == JavaJsValueKind::kDouble) {
            out_value->f = static_cast<jfloat>(value.double_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt32) {
            out_value->f = static_cast<jfloat>(value.int_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt64) {
            out_value->f = static_cast<jfloat>(value.int64_value);
            return true;
        }
        SetError(error_message, "Java float expects JS number");
        return false;
    }
    if (descriptor == "D") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->d = 0.0;
            return true;
        }
        if (value.kind == JavaJsValueKind::kFloat) {
            out_value->d = static_cast<jdouble>(value.float_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kDouble) {
            out_value->d = value.double_value;
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt32) {
            out_value->d = static_cast<jdouble>(value.int_value);
            return true;
        }
        if (value.kind == JavaJsValueKind::kInt64) {
            out_value->d = static_cast<jdouble>(value.int64_value);
            return true;
        }
        SetError(error_message, "Java double expects JS number");
        return false;
    }
    if (descriptor == "Ljava/lang/String;") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->l = nullptr;
            return true;
        }
        if (value.kind != JavaJsValueKind::kString) {
            SetError(error_message, "Java String expects JS string");
            return false;
        }
        if (env == nullptr) {
            SetError(error_message, "JNI environment is required for Java String conversion");
            return false;
        }
        jstring text = env->NewStringUTF(value.string_value.c_str());
        if (text == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            SetError(error_message, "NewStringUTF failed");
            return false;
        }
        // For transient callOriginal() arguments we track and release local refs after the
        // original invoke returns. For Java hook return values, the caller may pass nullptr here
        // so the local ref remains valid until the surrounding native frame unwinds.
        if (local_refs != nullptr) {
            local_refs->refs.push_back(text);
        }
        out_value->l = text;
        return true;
    }
    if (descriptor == "Ljava/lang/Object;") {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->l = nullptr;
            return true;
        }
        if (value.kind == JavaJsValueKind::kObject) {
            out_value->l = reinterpret_cast<jobject>(value.object_handle);
            return true;
        }
        if (value.kind == JavaJsValueKind::kArray) {
            if (env == nullptr) {
                SetError(error_message, "JNI environment is required for Java Object array conversion");
                return false;
            }
            if (value.array_type_name.empty()) {
                SetError(error_message, "Java Object array element is missing array type name");
                return false;
            }
            std::string array_descriptor;
            if (!TypeNameToDescriptor(value.array_type_name, &array_descriptor, error_message)) {
                return false;
            }
            if (array_descriptor.empty() || array_descriptor.front() != '[') {
                SetError(error_message, "Java Object array element type must be an array");
                return false;
            }
            jobject array = nullptr;
            if (!ConvertJavaJsArrayToJniArray(
                    env, array_descriptor, value, local_refs, &array, error_message)) {
                return false;
            }
            out_value->l = array;
            return true;
        }
        if (env == nullptr) {
            SetError(error_message, "JNI environment is required for Java Object conversion");
            return false;
        }
        jobject object = nullptr;
        if (!ConvertJavaJsValueToJavaObjectForRegisterClass(
                env, descriptor, value, local_refs, &object, error_message)) {
            return false;
        }
        out_value->l = object;
        return true;
    }
    if (!descriptor.empty() && (descriptor.front() == 'L' || descriptor.front() == '[')) {
        if (value.kind == JavaJsValueKind::kUndefined) {
            out_value->l = nullptr;
            return true;
        }
        if (descriptor.front() == '[' && value.kind == JavaJsValueKind::kArray) {
            jobject array = nullptr;
            if (!ConvertJavaJsArrayToJniArray(
                    env, descriptor, value, local_refs, &array, error_message)) {
                return false;
            }
            out_value->l = array;
            return true;
        }
        if (value.kind != JavaJsValueKind::kObject) {
            SetError(error_message, "Java object expects JS Java wrapper");
            return false;
        }
        out_value->l = reinterpret_cast<jobject>(value.object_handle);
        return true;
    }

    SetError(error_message, "unsupported Java descriptor for hook conversion");
    return false;
}

uint32_t AllocateJavaJsHookSlot(JavaJsHookRegistryState& state) {
    for (uint32_t slot = 0u; slot < kMaxJavaJsHookSlots; ++slot) {
        if (state.slot_used[slot]) {
            continue;
        }
        state.slot_used[slot] = true;
        state.hook_id_by_slot[slot] = 0u;
        return slot;
    }
    return kInvalidJavaJsHookSlot;
}

void ReleaseJavaJsHookSlot(JavaJsHookRegistryState& state, uint32_t slot) {
    if (slot >= kMaxJavaJsHookSlots) {
        return;
    }
    state.slot_used[slot] = false;
    state.hook_id_by_slot[slot] = 0u;
}

bool LookupJavaJsHookRecordBySlot(uint32_t slot, JavaJsHookRecord* out_record) {
    if (out_record == nullptr || slot >= kMaxJavaJsHookSlots) {
        return false;
    }

    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.slot_used[slot]) {
        return false;
    }
    const uint32_t hook_id = state.hook_id_by_slot[slot];
    const auto found = state.records.find(hook_id);
    if (found == state.records.end()) {
        return false;
    }
    *out_record = found->second;
    return true;
}

bool ReadJavaStringUtf8(JNIEnv* env, jstring value, std::string* out_text) {
    if (env == nullptr || out_text == nullptr) {
        return false;
    }
    if (value == nullptr) {
        out_text->clear();
        return true;
    }

    const char* utf8 = env->GetStringUTFChars(value, nullptr);
    if (utf8 == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return false;
    }

    *out_text = utf8;
    env->ReleaseStringUTFChars(value, utf8);
    return true;
}

std::string PrimitiveTypeNameToDescriptor(const std::string& type_name) {
    if (type_name == "void") return "V";
    if (type_name == "boolean") return "Z";
    if (type_name == "byte") return "B";
    if (type_name == "char") return "C";
    if (type_name == "short") return "S";
    if (type_name == "int") return "I";
    if (type_name == "long") return "J";
    if (type_name == "float") return "F";
    if (type_name == "double") return "D";
    return {};
}

bool TypeNameToDescriptor(const std::string& type_name,
                          std::string* descriptor_out,
                          std::string* error_message) {
    if (descriptor_out == nullptr) {
        SetError(error_message, "java descriptor output is required");
        return false;
    }
    descriptor_out->clear();
    if (type_name.empty()) {
        SetError(error_message, "java type name must be non-empty");
        return false;
    }

    const std::string primitive_descriptor = PrimitiveTypeNameToDescriptor(type_name);
    if (!primitive_descriptor.empty()) {
        *descriptor_out = primitive_descriptor;
        return true;
    }

    if (type_name.size() >= 2u && type_name.compare(type_name.size() - 2u, 2u, "[]") == 0) {
        std::string element_descriptor;
        if (!TypeNameToDescriptor(type_name.substr(0u, type_name.size() - 2u),
                                  &element_descriptor,
                                  error_message)) {
            return false;
        }
        *descriptor_out = "[" + element_descriptor;
        return true;
    }

    if (type_name[0] == '[') {
        *descriptor_out = type_name;
        std::replace(descriptor_out->begin(), descriptor_out->end(), '.', '/');
        return true;
    }

    if (type_name[0] == 'L' && type_name.back() == ';') {
        *descriptor_out = type_name;
        return true;
    }

    std::string normalized = type_name;
    std::replace(normalized.begin(), normalized.end(), '.', '/');
    *descriptor_out = "L" + normalized + ";";
    return true;
}

bool IsReferenceLikeJavaDescriptor(const std::string& descriptor) {
    return !descriptor.empty() && (descriptor.front() == 'L' || descriptor.front() == '[');
}

using JavaDescriptorAssignabilityCallback =
    bool (*)(const std::string& target_descriptor,
             const std::string& source_descriptor,
             void* opaque);

struct JavaDescriptorAssignabilityContext {
    JNIEnv* env = nullptr;
    uint64_t loader_handle = 0u;
    std::string* error_message = nullptr;
    bool had_error = false;
};

bool ReflectiveJavaDescriptorIsAssignableFrom(const std::string& target_descriptor,
                                              const std::string& source_descriptor,
                                              void* opaque);

bool JavaParameterDescriptorAcceptsArgumentWithAssignability(
    const std::string& parameter_descriptor,
    const std::string& argument_descriptor,
    JavaDescriptorAssignabilityCallback is_assignable_from,
    void* opaque) {
    if (parameter_descriptor == argument_descriptor) {
        return true;
    }
    if (parameter_descriptor.empty() || argument_descriptor.empty()) {
        return false;
    }
    if (argument_descriptor == kJavaInvokeNullTypeCandidate) {
        return IsReferenceLikeJavaDescriptor(parameter_descriptor);
    }
    if (IsReferenceLikeJavaDescriptor(parameter_descriptor) &&
        IsReferenceLikeJavaDescriptor(argument_descriptor) &&
        is_assignable_from != nullptr &&
        is_assignable_from(parameter_descriptor, argument_descriptor, opaque)) {
        return true;
    }
    if (parameter_descriptor == "Ljava/lang/Object;" &&
        IsReferenceLikeJavaDescriptor(argument_descriptor)) {
        return true;
    }
    if (parameter_descriptor.front() != '[' || argument_descriptor.front() != '[') {
        return false;
    }

    const std::string parameter_component = parameter_descriptor.substr(1u);
    const std::string argument_component = argument_descriptor.substr(1u);
    if (parameter_component == argument_component) {
        return true;
    }
    if (parameter_component == "Ljava/lang/Object;" &&
        IsReferenceLikeJavaDescriptor(argument_component)) {
        return true;
    }
    if (parameter_component.front() == '[' && argument_component.front() == '[') {
        return JavaParameterDescriptorAcceptsArgumentWithAssignability(parameter_component,
                                                                      argument_component,
                                                                      is_assignable_from,
                                                                      opaque);
    }
    return false;
}

bool ChooseJavaArrayElementDescriptor(const std::string& target_array_descriptor,
                                      const std::string& source_array_type_name,
                                      JavaDescriptorAssignabilityCallback is_assignable_from,
                                      void* opaque,
                                      std::string* element_descriptor_out,
                                      std::string* error_message) {
    if (element_descriptor_out == nullptr) {
        SetError(error_message, "java array element descriptor output is required");
        return false;
    }

    element_descriptor_out->clear();
    if (target_array_descriptor.empty() || target_array_descriptor.front() != '[') {
        SetError(error_message, "target Java array descriptor is invalid");
        return false;
    }

    const std::string target_component_descriptor = target_array_descriptor.substr(1u);
    *element_descriptor_out = target_component_descriptor;

    if (source_array_type_name.empty()) {
        return true;
    }

    std::string source_array_descriptor;
    if (!TypeNameToDescriptor(source_array_type_name, &source_array_descriptor, nullptr) ||
        source_array_descriptor.empty() ||
        source_array_descriptor.front() != '[') {
        return true;
    }

    const std::string source_component_descriptor = source_array_descriptor.substr(1u);
    if (JavaParameterDescriptorAcceptsArgumentWithAssignability(target_component_descriptor,
                                                               source_component_descriptor,
                                                               is_assignable_from,
                                                               opaque)) {
        *element_descriptor_out = source_component_descriptor;
    }
    return true;
}

bool JavaParameterDescriptorAcceptsArgument(const std::string& parameter_descriptor,
                                            const std::string& argument_descriptor) {
    return JavaParameterDescriptorAcceptsArgumentWithAssignability(parameter_descriptor,
                                                                  argument_descriptor,
                                                                  nullptr,
                                                                  nullptr);
}

JavaMethodSpecificityComparisonForTesting CompareJavaMethodSpecificity(
    const std::vector<std::string>& left_parameter_descriptors,
    const std::vector<std::string>& right_parameter_descriptors,
    JavaDescriptorAssignabilityCallback is_assignable_from,
    void* opaque) {
    if (left_parameter_descriptors.size() != right_parameter_descriptors.size()) {
        return JavaMethodSpecificityComparisonForTesting::kIncomparable;
    }

    bool left_more_specific = false;
    bool right_more_specific = false;
    for (size_t index = 0; index < left_parameter_descriptors.size(); ++index) {
        const std::string& left = left_parameter_descriptors[index];
        const std::string& right = right_parameter_descriptors[index];
        if (left == right) {
            continue;
        }
        if (!IsReferenceLikeJavaDescriptor(left) || !IsReferenceLikeJavaDescriptor(right) ||
            is_assignable_from == nullptr) {
            return JavaMethodSpecificityComparisonForTesting::kIncomparable;
        }

        const bool left_assignable_to_right = is_assignable_from(right, left, opaque);
        const bool right_assignable_to_left = is_assignable_from(left, right, opaque);
        if (left_assignable_to_right && !right_assignable_to_left) {
            left_more_specific = true;
            if (right_more_specific) {
                return JavaMethodSpecificityComparisonForTesting::kIncomparable;
            }
            continue;
        }
        if (right_assignable_to_left && !left_assignable_to_right) {
            right_more_specific = true;
            if (left_more_specific) {
                return JavaMethodSpecificityComparisonForTesting::kIncomparable;
            }
            continue;
        }
        return JavaMethodSpecificityComparisonForTesting::kIncomparable;
    }

    if (left_more_specific == right_more_specific) {
        return JavaMethodSpecificityComparisonForTesting::kIncomparable;
    }
    return left_more_specific
               ? JavaMethodSpecificityComparisonForTesting::kLeftMoreSpecific
               : JavaMethodSpecificityComparisonForTesting::kRightMoreSpecific;
}

JavaOverloadMatchResultForTesting ResolveMostSpecificJavaOverload(
    const std::vector<std::vector<std::string>>& candidate_parameter_descriptors,
    const std::vector<std::string>& argument_descriptors,
    JavaDescriptorAssignabilityCallback is_assignable_from,
    void* opaque,
    size_t* matched_index) {
    if (matched_index != nullptr) {
        *matched_index = 0u;
    }

    std::vector<size_t> matching_indices;
    matching_indices.reserve(candidate_parameter_descriptors.size());
    for (size_t candidate_index = 0; candidate_index < candidate_parameter_descriptors.size();
         ++candidate_index) {
        const std::vector<std::string>& parameters = candidate_parameter_descriptors[candidate_index];
        if (parameters.size() != argument_descriptors.size()) {
            continue;
        }

        bool matched = true;
        for (size_t argument_index = 0; argument_index < argument_descriptors.size(); ++argument_index) {
            if (!JavaParameterDescriptorAcceptsArgumentWithAssignability(parameters[argument_index],
                                                                        argument_descriptors[argument_index],
                                                                        is_assignable_from,
                                                                        opaque)) {
                matched = false;
                break;
            }
        }
        if (matched) {
            matching_indices.push_back(candidate_index);
        }
    }

    if (matching_indices.empty()) {
        return JavaOverloadMatchResultForTesting::kNoMatch;
    }
    if (matching_indices.size() == 1u) {
        if (matched_index != nullptr) {
            *matched_index = matching_indices[0];
        }
        return JavaOverloadMatchResultForTesting::kUniqueMatch;
    }

    size_t best_index = 0u;
    bool found_best = false;
    for (size_t candidate_position = 0; candidate_position < matching_indices.size(); ++candidate_position) {
        const size_t candidate_index = matching_indices[candidate_position];
        bool better_than_all = true;
        for (size_t other_position = 0; other_position < matching_indices.size(); ++other_position) {
            if (candidate_position == other_position) {
                continue;
            }
            const size_t other_index = matching_indices[other_position];
            const JavaMethodSpecificityComparisonForTesting comparison = CompareJavaMethodSpecificity(
                candidate_parameter_descriptors[candidate_index],
                candidate_parameter_descriptors[other_index],
                is_assignable_from,
                opaque);
            if (comparison != JavaMethodSpecificityComparisonForTesting::kLeftMoreSpecific) {
                better_than_all = false;
                break;
            }
        }
        if (!better_than_all) {
            continue;
        }
        if (found_best) {
            return JavaOverloadMatchResultForTesting::kAmbiguous;
        }
        found_best = true;
        best_index = candidate_index;
    }

    if (!found_best) {
        return JavaOverloadMatchResultForTesting::kAmbiguous;
    }
    if (matched_index != nullptr) {
        *matched_index = best_index;
    }
    return JavaOverloadMatchResultForTesting::kUniqueMatch;
}

bool ConvertJavaJsArrayToJniArray(JNIEnv* env,
                                  const std::string& descriptor,
                                  const JavaJsValue& value,
                                  ScopedJavaLocalRefs* local_refs,
                                  jobject* out_array,
                                  std::string* error_message) {
    if (env == nullptr || out_array == nullptr) {
        SetError(error_message, "JNI environment is required for Java array conversion");
        return false;
    }
    *out_array = nullptr;
    if (descriptor.empty() || descriptor.front() != '[') {
        SetError(error_message, "Java array descriptor is invalid");
        return false;
    }

    const std::string component_descriptor = descriptor.substr(1u);
    const jsize count = static_cast<jsize>(value.array_elements.size());

    if (component_descriptor == "Z") {
        jbooleanArray array = env->NewBooleanArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewBooleanArray failed");
            return false;
        }
        std::vector<jboolean> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "Z", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] =
                static_cast<jboolean>(converted.z != 0 ? JNI_TRUE : JNI_FALSE);
        }
        env->SetBooleanArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetBooleanArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor == "B") {
        jbyteArray array = env->NewByteArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewByteArray failed");
            return false;
        }
        std::vector<jbyte> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "B", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] = converted.b;
        }
        env->SetByteArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetByteArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor == "S") {
        jshortArray array = env->NewShortArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewShortArray failed");
            return false;
        }
        std::vector<jshort> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "S", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] = converted.s;
        }
        env->SetShortArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetShortArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor == "C") {
        jcharArray array = env->NewCharArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewCharArray failed");
            return false;
        }
        std::vector<jchar> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "C", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] = converted.c;
        }
        env->SetCharArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetCharArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor == "I") {
        jintArray array = env->NewIntArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewIntArray failed");
            return false;
        }
        std::vector<jint> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "I", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] = converted.i;
        }
        env->SetIntArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetIntArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor == "J") {
        jlongArray array = env->NewLongArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewLongArray failed");
            return false;
        }
        std::vector<jlong> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "J", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] = converted.j;
        }
        env->SetLongArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetLongArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor == "F") {
        jfloatArray array = env->NewFloatArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewFloatArray failed");
            return false;
        }
        std::vector<jfloat> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "F", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] = converted.f;
        }
        env->SetFloatArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetFloatArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor == "D") {
        jdoubleArray array = env->NewDoubleArray(count);
        if (array == nullptr || env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "NewDoubleArray failed");
            return false;
        }
        std::vector<jdouble> elements(static_cast<size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(
                    env, "D", value.array_elements[static_cast<size_t>(i)], local_refs, &converted, error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            elements[static_cast<size_t>(i)] = converted.d;
        }
        env->SetDoubleArrayRegion(array, 0, count, elements.data());
        if (env->ExceptionCheck()) {
            env->DeleteLocalRef(array);
            ClearJniException(env);
            SetError(error_message, "SetDoubleArrayRegion failed");
            return false;
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (!component_descriptor.empty() && component_descriptor.front() == '[') {
        jclass component_class = ResolveJavaClass(env, component_descriptor, 0u, error_message);
        if (component_class == nullptr) {
            return false;
        }
        jobjectArray array = env->NewObjectArray(count, component_class, nullptr);
        env->DeleteLocalRef(component_class);
        if (array == nullptr || env->ExceptionCheck()) {
            if (array != nullptr) {
                env->DeleteLocalRef(array);
            }
            ClearJniException(env);
            SetError(error_message, "NewObjectArray failed");
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(env,
                                                       component_descriptor,
                                                       value.array_elements[static_cast<size_t>(i)],
                                                       local_refs,
                                                       &converted,
                                                       error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            env->SetObjectArrayElement(array, i, reinterpret_cast<jobject>(converted.l));
            if (env->ExceptionCheck()) {
                env->DeleteLocalRef(array);
                ClearJniException(env);
                SetError(error_message, "SetObjectArrayElement failed");
                return false;
            }
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    if (component_descriptor.size() >= 2u &&
        component_descriptor.front() == 'L' &&
        component_descriptor.back() == ';') {
        std::string component_class_name;
        if (!DescriptorToDotClassName(component_descriptor, &component_class_name, error_message)) {
            return false;
        }
        jclass component_class = ResolveJavaClass(env, component_class_name, 0u, error_message);
        if (component_class == nullptr) {
            return false;
        }
        jobjectArray array = env->NewObjectArray(count, component_class, nullptr);
        env->DeleteLocalRef(component_class);
        if (array == nullptr || env->ExceptionCheck()) {
            if (array != nullptr) {
                env->DeleteLocalRef(array);
            }
            ClearJniException(env);
            SetError(error_message, "NewObjectArray failed");
            return false;
        }
        std::string element_descriptor;
        JavaDescriptorAssignabilityContext assignability_context = {};
        assignability_context.env = env;
        assignability_context.loader_handle = 0u;
        assignability_context.error_message = error_message;
        if (!ChooseJavaArrayElementDescriptor(descriptor,
                                             value.array_type_name,
                                             &ReflectiveJavaDescriptorIsAssignableFrom,
                                             &assignability_context,
                                             &element_descriptor,
                                             error_message)) {
            env->DeleteLocalRef(array);
            return false;
        }
        if (assignability_context.had_error) {
            env->DeleteLocalRef(array);
            return false;
        }
        for (jsize i = 0; i < count; ++i) {
            NookJavaHookValue converted = {};
            if (!ConvertJavaJsValueToNookJavaHookValue(env,
                                                       element_descriptor,
                                                       value.array_elements[static_cast<size_t>(i)],
                                                       local_refs,
                                                       &converted,
                                                       error_message)) {
                if (error_message != nullptr) {
                    *error_message = FormatJavaArrayElementError(
                        value.array_type_name, static_cast<size_t>(i), *error_message);
                }
                env->DeleteLocalRef(array);
                return false;
            }
            env->SetObjectArrayElement(array, i, reinterpret_cast<jobject>(converted.l));
            if (env->ExceptionCheck()) {
                env->DeleteLocalRef(array);
                ClearJniException(env);
                SetError(error_message, "SetObjectArrayElement failed");
                return false;
            }
        }
        if (local_refs != nullptr) {
            local_refs->refs.push_back(array);
        }
        *out_array = array;
        return true;
    }

    SetError(error_message, "unsupported Java array descriptor");
    return false;
}

bool DescribeJavaClassObject(JNIEnv* env, jobject class_object, std::string* out_descriptor) {
    if (env == nullptr || class_object == nullptr || out_descriptor == nullptr) {
        return false;
    }

    jclass class_class = env->FindClass("java/lang/Class");
    if (class_class == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return false;
    }

    jmethodID get_name_method = env->GetMethodID(class_class, "getName", "()Ljava/lang/String;");
    env->DeleteLocalRef(class_class);
    if (get_name_method == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return false;
    }

    jstring name_string = reinterpret_cast<jstring>(env->CallObjectMethod(class_object, get_name_method));
    if (name_string == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return false;
    }

    std::string type_name;
    const bool ok = ReadJavaStringUtf8(env, name_string, &type_name);
    env->DeleteLocalRef(name_string);
    if (!ok) {
        return false;
    }

    std::string descriptor;
    std::string ignored_error;
    if (!TypeNameToDescriptor(type_name, &descriptor, &ignored_error)) {
        return false;
    }
    *out_descriptor = std::move(descriptor);
    return true;
}

bool DescribeJavaObject(JNIEnv* env,
                        jobject object,
                        std::string* out_class_name,
                        std::string* error_message) {
    if (env == nullptr || object == nullptr || out_class_name == nullptr) {
        SetError(error_message, "java object description arguments are invalid");
        return false;
    }

    jclass object_class = env->GetObjectClass(object);
    if (object_class == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "GetObjectClass failed");
        return false;
    }

    std::string descriptor;
    const bool described = DescribeJavaClassObject(env, object_class, &descriptor);
    env->DeleteLocalRef(object_class);
    if (!described) {
        SetError(error_message, "DescribeJavaClassObject failed");
        return false;
    }

    if (descriptor.size() >= 2u && descriptor.front() == 'L' && descriptor.back() == ';') {
        *out_class_name = descriptor.substr(1u, descriptor.size() - 2u);
        std::replace(out_class_name->begin(), out_class_name->end(), '/', '.');
        return true;
    }
    if (!descriptor.empty() && descriptor.front() == '[') {
        *out_class_name = descriptor;
        std::replace(out_class_name->begin(), out_class_name->end(), '/', '.');
        return true;
    }

    SetError(error_message, "unsupported Java descriptor for JS conversion");
    return false;
}

bool DescriptorToDotClassName(const std::string& descriptor,
                              std::string* class_name_out,
                              std::string* error_message) {
    if (class_name_out == nullptr) {
        SetError(error_message, "java class name output is required");
        return false;
    }

    if (descriptor == "V") {
        *class_name_out = "void";
        return true;
    }
    if (descriptor == "Z") {
        *class_name_out = "boolean";
        return true;
    }
    if (descriptor == "B") {
        *class_name_out = "byte";
        return true;
    }
    if (descriptor == "C") {
        *class_name_out = "char";
        return true;
    }
    if (descriptor == "S") {
        *class_name_out = "short";
        return true;
    }
    if (descriptor == "I") {
        *class_name_out = "int";
        return true;
    }
    if (descriptor == "J") {
        *class_name_out = "long";
        return true;
    }
    if (descriptor == "F") {
        *class_name_out = "float";
        return true;
    }
    if (descriptor == "D") {
        *class_name_out = "double";
        return true;
    }

    if (descriptor.size() >= 2u && descriptor.front() == 'L' && descriptor.back() == ';') {
        *class_name_out = descriptor.substr(1u, descriptor.size() - 2u);
        std::replace(class_name_out->begin(), class_name_out->end(), '/', '.');
        return true;
    }
    if (!descriptor.empty() && descriptor.front() == '[') {
        *class_name_out = descriptor;
        std::replace(class_name_out->begin(), class_name_out->end(), '/', '.');
        return true;
    }

    SetError(error_message, "unsupported Java class descriptor");
    return false;
}

bool DescriptorToJavaArrayTypeName(const std::string& descriptor,
                                   std::string* type_name_out,
                                   std::string* error_message) {
    if (type_name_out == nullptr) {
        SetError(error_message, "java array type output is required");
        return false;
    }
    if (descriptor.empty() || descriptor.front() != '[') {
        SetError(error_message, "java array descriptor is invalid");
        return false;
    }

    std::string component_type_name;
    if (!DescriptorToDotClassName(descriptor.substr(1u), &component_type_name, error_message)) {
        return false;
    }
    *type_name_out = component_type_name + "[]";
    return true;
}

bool NormalizeJavaDescriptorForClassLookup(const std::string& descriptor,
                                           std::string* class_name_out,
                                           std::string* error_message) {
    if (class_name_out == nullptr) {
        SetError(error_message, "java class name output is required");
        return false;
    }

    if (!descriptor.empty() && descriptor.front() == 'L' && descriptor.back() == ';') {
        return DescriptorToDotClassName(descriptor, class_name_out, error_message);
    }
    *class_name_out = descriptor;
    if (!class_name_out->empty()) {
        std::replace(class_name_out->begin(), class_name_out->end(), '/', '.');
    }
    return true;
}

bool IsJavaConstructorMethodName(const std::string& method_name) {
    return method_name == "<init>" || method_name == "$init";
}

struct EnumerateLoadedClassesContext {
    JNIEnv* env = nullptr;
    std::vector<std::string>* class_names = nullptr;
    std::unordered_set<std::string> seen;
};

struct ArtLoadedClassVisitor;

struct ArtLoadedClassVisitorVTable {
    void (*destroy)(ArtLoadedClassVisitor*) = nullptr;
    void (*delete_destroy)(ArtLoadedClassVisitor*) = nullptr;
    bool (*visit)(ArtLoadedClassVisitor*, void* klass) = nullptr;
};

struct ArtLoadedClassVisitor {
    ArtLoadedClassVisitorVTable* vtable = nullptr;
    EnumerateLoadedClassesContext* context = nullptr;
    ArtLoadedClassVisitorVTable storage = {};
};

bool TryRecordLoadedJavaClass(void* klass, EnumerateLoadedClassesContext* context) {
    if (klass == nullptr || context == nullptr || ArtInternals::newlocalrefFn == nullptr) {
        return true;
    }
    if (context->env == nullptr || context->class_names == nullptr) {
        return true;
    }

    JNIEnv* env = context->env;
    jclass local_ref = reinterpret_cast<jclass>(ArtInternals::newlocalrefFn(env, klass));
    if (local_ref == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        if (local_ref != nullptr) {
            env->DeleteLocalRef(local_ref);
        }
        return true;
    }

    std::string descriptor;
    std::string class_name;
    std::string error_message;
    if (DescribeJavaClassObject(env, local_ref, &descriptor) &&
        DescriptorToDotClassName(descriptor, &class_name, &error_message) &&
        context->seen.insert(class_name).second) {
        context->class_names->push_back(std::move(class_name));
    }

    env->DeleteLocalRef(local_ref);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    return true;
}

bool VisitLoadedClassForEnumerationLegacy(void* klass, void* arg) {
    return TryRecordLoadedJavaClass(
        klass, reinterpret_cast<EnumerateLoadedClassesContext*>(arg));
}

bool VisitLoadedClassForEnumeration(ArtLoadedClassVisitor* self, void* klass) {
    if (self == nullptr) {
        return true;
    }
    return TryRecordLoadedJavaClass(klass, self->context);
}

ArtLoadedClassVisitor MakeArtLoadedClassVisitor(EnumerateLoadedClassesContext* context) {
    ArtLoadedClassVisitor visitor = {};
    visitor.context = context;
    visitor.vtable = &visitor.storage;
    visitor.storage.visit = &VisitLoadedClassForEnumeration;
    return visitor;
}

bool WithAllArtThreadsSuspended(const char* cause,
                                std::string* error_message,
                                const std::function<void()>& action) {
    if (!action) {
        SetError(error_message, "suspend-all action is required");
        return false;
    }

    void* thread_list = nullptr;
    if (ArtInternals::RuntimeInstance != 0u && ArtInternals::RunTimeSpec.threadList != 0) {
        thread_list = *reinterpret_cast<void**>(
            ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.threadList);
    }

    const char* libart_path = tool::find_path_from_maps("libart.so");
    if (thread_list != nullptr && libart_path != nullptr) {
        auto suspend_all = reinterpret_cast<SuspendAllArtThreadsFn>(
            tool::get_address_from_module(
                libart_path, "_ZN3art10ThreadList10SuspendAllEPKcb", true));
        auto suspend_all_legacy = reinterpret_cast<SuspendAllArtThreadsLegacyFn>(
            tool::get_address_from_module(
                libart_path, "_ZN3art10ThreadList10SuspendAllEv", true));
        auto resume_all = reinterpret_cast<ResumeAllArtThreadsFn>(
            tool::get_address_from_module(
                libart_path, "_ZN3art10ThreadList9ResumeAllEv", true));
        if (resume_all != nullptr && (suspend_all != nullptr || suspend_all_legacy != nullptr)) {
            if (suspend_all != nullptr) {
                suspend_all(thread_list,
                            cause != nullptr ? cause : "Nook Enumerate Loaded Classes",
                            false);
            } else {
                suspend_all_legacy(thread_list);
            }
            action();
            resume_all(thread_list);
            return true;
        }
    }

    if (ArtInternals::ScopedSuspendAllFn != nullptr &&
        ArtInternals::destroyScopedSuspendAllFn != nullptr) {
        char scoped_suspend_all[256] = {};
        ArtInternals::ScopedSuspendAllFn(
            scoped_suspend_all,
            cause != nullptr ? cause : "Nook Enumerate Loaded Classes",
            false);
        action();
        ArtInternals::destroyScopedSuspendAllFn(scoped_suspend_all);
        return true;
    }

    SetError(error_message, "ART suspend-all helpers are unavailable");
    return false;
}

jclass ResolveJavaClass(JNIEnv* env,
                        const std::string& class_name,
                        uint64_t loader_handle,
                        std::string* error_message) {
    if (env == nullptr) {
        SetError(error_message, "JNIEnv is null while resolving Java class");
        return nullptr;
    }

    if (loader_handle == 0u) {
        jclass clazz = JavaHook::FindClass(env, class_name.c_str());
        if (clazz == nullptr) {
            SetError(error_message, "FindClass failed");
        }
        return clazz;
    }

    jobject loader = env->NewLocalRef(reinterpret_cast<jobject>(loader_handle));
    if (loader == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "loader handle is invalid");
        return nullptr;
    }

    jclass clazz = JavaHookLoaderResolver::FindLoadedClassWithLoader(
        env, loader, class_name.c_str());
    if (clazz == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        clazz = JavaHookLoaderResolver::LoadClassWithLoader(
            env, loader, class_name.c_str());
    }
    env->DeleteLocalRef(loader);

    if (clazz == nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (clazz == nullptr) {
        SetError(error_message, "FindClass with loader failed");
    }
    return clazz;
}

bool ResolveJavaFieldByName(const std::string& class_name,
                            const std::string& field_name,
                            uint64_t loader_handle,
                            bool is_static,
                            JavaJsFieldRecord* out_record,
                            std::string* error_message) {
    if (out_record == nullptr) {
        SetError(error_message, "resolved Java field output is required");
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while resolving Java field");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = ResolveJavaClass(env, class_name, loader_handle, error_message);
    if (clazz == nullptr) {
        return false;
    }

    jclass class_class = env->FindClass("java/lang/Class");
    jclass field_class = env->FindClass("java/lang/reflect/Field");
    jclass modifier_class = env->FindClass("java/lang/reflect/Modifier");
    if (class_class == nullptr || field_class == nullptr || modifier_class == nullptr) {
        if (class_class != nullptr) env->DeleteLocalRef(class_class);
        if (field_class != nullptr) env->DeleteLocalRef(field_class);
        if (modifier_class != nullptr) env->DeleteLocalRef(modifier_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaField reflection classes unavailable");
        return false;
    }

    jmethodID get_declared_fields =
        env->GetMethodID(class_class, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
    jmethodID field_get_name = env->GetMethodID(field_class, "getName", "()Ljava/lang/String;");
    jmethodID field_get_type = env->GetMethodID(field_class, "getType", "()Ljava/lang/Class;");
    jmethodID field_get_modifiers = env->GetMethodID(field_class, "getModifiers", "()I");
    jmethodID modifier_is_static = env->GetStaticMethodID(modifier_class, "isStatic", "(I)Z");
    if (get_declared_fields == nullptr ||
        field_get_name == nullptr ||
        field_get_type == nullptr ||
        field_get_modifiers == nullptr ||
        modifier_is_static == nullptr) {
        env->DeleteLocalRef(class_class);
        env->DeleteLocalRef(field_class);
        env->DeleteLocalRef(modifier_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaField reflection methods unavailable");
        return false;
    }

    jobjectArray fields =
        reinterpret_cast<jobjectArray>(env->CallObjectMethod(clazz, get_declared_fields));
    env->DeleteLocalRef(class_class);
    env->DeleteLocalRef(field_class);
    env->DeleteLocalRef(clazz);
    if (fields == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(modifier_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaField getDeclaredFields failed");
        return false;
    }

    bool found = false;
    JavaJsFieldRecord resolved = {};
    const jsize field_count = env->GetArrayLength(fields);
    for (jsize index = 0; index < field_count; ++index) {
        jobject field_object = env->GetObjectArrayElement(fields, index);
        if (field_object == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            continue;
        }

        jstring name_string =
            reinterpret_cast<jstring>(env->CallObjectMethod(field_object, field_get_name));
        std::string reflected_name;
        if (name_string == nullptr || !ReadJavaStringUtf8(env, name_string, &reflected_name)) {
            if (name_string != nullptr) {
                env->DeleteLocalRef(name_string);
            }
            env->DeleteLocalRef(field_object);
            continue;
        }
        env->DeleteLocalRef(name_string);
        if (reflected_name != field_name) {
            env->DeleteLocalRef(field_object);
            continue;
        }

        jint modifiers = env->CallIntMethod(field_object, field_get_modifiers);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(field_object);
            continue;
        }
        const bool reflected_is_static =
            env->CallStaticBooleanMethod(modifier_class, modifier_is_static, modifiers) == JNI_TRUE;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(field_object);
            continue;
        }
        if (reflected_is_static != is_static) {
            env->DeleteLocalRef(field_object);
            continue;
        }

        jobject type_object = env->CallObjectMethod(field_object, field_get_type);
        if (type_object == nullptr || env->ExceptionCheck()) {
            if (type_object != nullptr) {
                env->DeleteLocalRef(type_object);
            }
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(field_object);
            continue;
        }

        std::string descriptor;
        const bool described =
            DescribeJavaClassObject(env, type_object, &descriptor) &&
            IsSupportedJavaFieldDescriptor(descriptor);
        env->DeleteLocalRef(type_object);
        env->DeleteLocalRef(field_object);
        if (!described) {
            continue;
        }

        resolved.class_name = class_name;
        resolved.field_name = field_name;
        resolved.reflected_field_name = reflected_name;
        resolved.signature = descriptor;
        resolved.loader_handle = loader_handle;
        resolved.is_static = is_static;
        resolved.uses_declared_field_lookup = true;
        found = true;
        break;
    }

    env->DeleteLocalRef(fields);
    env->DeleteLocalRef(modifier_class);
    if (!found) {
        SetError(error_message, "ResolveJavaField no field match");
        return false;
    }

    *out_record = resolved;
    return true;
}

bool ResolveJavaDeclaredFieldObject(JNIEnv* env,
                                    const JavaJsFieldRecord& record,
                                    jclass* clazz_out,
                                    jobject* field_out,
                                    std::string* error_message) {
    if (env == nullptr || clazz_out == nullptr || field_out == nullptr) {
        SetError(error_message, "java declared field resolution arguments are invalid");
        return false;
    }

    *clazz_out = nullptr;
    *field_out = nullptr;

    jclass clazz = ResolveJavaClass(env, record.class_name, record.loader_handle, error_message);
    if (clazz == nullptr) {
        return false;
    }

    jclass class_class = env->FindClass("java/lang/Class");
    jclass field_class = env->FindClass("java/lang/reflect/Field");
    if (class_class == nullptr || field_class == nullptr) {
        if (class_class != nullptr) env->DeleteLocalRef(class_class);
        if (field_class != nullptr) env->DeleteLocalRef(field_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaDeclaredField reflection classes unavailable");
        return false;
    }

    jmethodID get_declared_field =
        env->GetMethodID(class_class, "getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;");
    jmethodID field_set_accessible =
        env->GetMethodID(field_class, "setAccessible", "(Z)V");
    env->DeleteLocalRef(class_class);
    env->DeleteLocalRef(field_class);
    if (get_declared_field == nullptr || field_set_accessible == nullptr) {
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaDeclaredField reflection methods unavailable");
        return false;
    }

    const std::string& reflected_name =
        record.reflected_field_name.empty() ? record.field_name : record.reflected_field_name;
    jstring reflected_name_string = env->NewStringUTF(reflected_name.c_str());
    if (reflected_name_string == nullptr) {
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaDeclaredField field name allocation failed");
        return false;
    }

    jobject field = env->CallObjectMethod(clazz, get_declared_field, reflected_name_string);
    env->DeleteLocalRef(reflected_name_string);
    if (field == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(clazz);
        SetError(error_message, "ResolveJavaDeclaredField getDeclaredField failed");
        return false;
    }

    env->CallVoidMethod(field, field_set_accessible, JNI_TRUE);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(field);
        env->DeleteLocalRef(clazz);
        SetError(error_message, "ResolveJavaDeclaredField setAccessible failed");
        return false;
    }

    *clazz_out = clazz;
    *field_out = field;
    return true;
}

bool ResolveJavaFieldId(JNIEnv* env,
                        const JavaJsFieldRecord& record,
                        jclass* clazz_out,
                        jfieldID* field_id_out,
                        std::string* error_message) {
    if (env == nullptr || clazz_out == nullptr || field_id_out == nullptr) {
        SetError(error_message, "java field resolution arguments are invalid");
        return false;
    }

    *clazz_out = nullptr;
    *field_id_out = nullptr;

    jclass clazz = ResolveJavaClass(env, record.class_name, record.loader_handle, error_message);
    if (clazz == nullptr) {
        return false;
    }

    jfieldID field_id = record.is_static
        ? env->GetStaticFieldID(clazz, record.field_name.c_str(), record.signature.c_str())
        : env->GetFieldID(clazz, record.field_name.c_str(), record.signature.c_str());
    if (field_id == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(clazz);
        SetError(error_message, "ResolveJavaFieldId GetFieldID failed");
        return false;
    }

    *clazz_out = clazz;
    *field_id_out = field_id;
    return true;
}

bool ResolveJavaInstanceFieldAccessTarget(const JavaJsFieldRecord& record,
                                          uint64_t receiver_handle,
                                          JNIEnv** env_out,
                                          jobject* receiver_out,
                                          std::string* error_message) {
    if (env_out == nullptr || receiver_out == nullptr) {
        SetError(error_message, "java field access outputs are required");
        return false;
    }
    *env_out = nullptr;
    *receiver_out = nullptr;

    if (record.is_static) {
        SetError(error_message, "instance field resolver received static field");
        return false;
    }

    ActiveJavaJsInvocation active_invocation = {};
    if (TryGetActiveJavaJsInvocation(&active_invocation) &&
        active_invocation.active &&
        active_invocation.env != nullptr &&
        active_invocation.thiz != nullptr) {
        const uint64_t active_handle = reinterpret_cast<uint64_t>(active_invocation.thiz);
        if (receiver_handle != 0u && receiver_handle != active_handle) {
            SetError(error_message, "java instance field receiver mismatch");
            return false;
        }

        *env_out = active_invocation.env;
        *receiver_out = active_invocation.thiz;
        return true;
    }

    if (receiver_handle == 0u) {
        SetError(error_message, "java instance field access requires receiver");
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while accessing Java instance field");
        return false;
    }

    *env_out = jenv.get();
    *receiver_out = reinterpret_cast<jobject>(receiver_handle);
    return true;
}

bool ResolveJavaMethodId(JNIEnv* env,
                         const JavaJsMethodRecord& record,
                         jobject receiver,
                         jclass* clazz_out,
                         jmethodID* method_id_out,
                         std::string* error_message) {
    if (env == nullptr || clazz_out == nullptr || method_id_out == nullptr) {
        SetError(error_message, "java method resolution arguments are invalid");
        return false;
    }

    *clazz_out = nullptr;
    *method_id_out = nullptr;

    jclass clazz = nullptr;
    if (!record.is_static && receiver != nullptr) {
        clazz = env->GetObjectClass(receiver);
        if (clazz == nullptr || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            SetError(error_message, "ResolveJavaMethodId GetObjectClass failed");
            return false;
        }
    } else {
        clazz = ResolveJavaClass(env, record.class_name, record.loader_handle, error_message);
        if (clazz == nullptr) {
            return false;
        }
    }

    jmethodID method_id = record.is_static
        ? env->GetStaticMethodID(clazz, record.method_name.c_str(), record.signature.c_str())
        : env->GetMethodID(clazz, record.method_name.c_str(), record.signature.c_str());
    if (method_id == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(clazz);
        SetError(error_message, "ResolveJavaMethodId GetMethodID failed");
        return false;
    }

    *clazz_out = clazz;
    *method_id_out = method_id;
    return true;
}

bool DefaultInvokeJavaMethod(const JavaJsMethodRecord& record,
                             uint64_t receiver_handle,
                             const JavaJsValue* args,
                             std::size_t arg_count,
                             JavaJsValue* result,
                             std::string* error_message) {
    if (result == nullptr) {
        SetError(error_message, "java invoke result output is required");
        return false;
    }
    if (record.signature.empty()) {
        SetError(error_message, "java invoke requires an exact method signature");
        return false;
    }

    ParsedJavaMethodSignature parsed_signature = {};
    if (!ParseMethodSignature(record.signature, &parsed_signature, error_message)) {
        return false;
    }
    if (arg_count != parsed_signature.arg_descriptors.size()) {
        SetError(error_message, "java invoke arg count mismatch");
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while invoking Java method");
        return false;
    }
    JNIEnv* env = jenv.get();

    const bool is_constructor = IsJavaConstructorMethodName(record.method_name);
    if (is_constructor) {
        jclass clazz = ResolveJavaClass(env, record.class_name, record.loader_handle, error_message);
        if (clazz == nullptr) {
            return false;
        }

        jmethodID method_id = env->GetMethodID(clazz, "<init>", record.signature.c_str());
        if (method_id == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(clazz);
            SetError(error_message, "ResolveJavaConstructorId GetMethodID failed");
            return false;
        }

        ScopedJavaLocalRefs local_refs = {};
        local_refs.env = env;
        std::vector<NookJavaHookValue> raw_args(arg_count);
        for (std::size_t index = 0; index < arg_count; ++index) {
            if (!ConvertJavaJsValueToNookJavaHookValue(env,
                                                       parsed_signature.arg_descriptors[index],
                                                       args[index],
                                                       &local_refs,
                                                       &raw_args[index],
                                                       error_message)) {
                env->DeleteLocalRef(clazz);
                return false;
            }
        }

        jobject instance = env->NewObjectA(clazz,
                                           method_id,
                                           reinterpret_cast<const jvalue*>(raw_args.data()));
        env->DeleteLocalRef(clazz);
        if (instance == nullptr || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            SetError(error_message, "java constructor invoke failed");
            return false;
        }

        jobject retained = env->NewGlobalRef(instance);
        if (retained == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(instance);
            SetError(error_message, "NewGlobalRef failed");
            return false;
        }

        result->kind = JavaJsValueKind::kObject;
        result->object_handle = reinterpret_cast<uint64_t>(retained);
        result->object_handle_is_global = true;
        const bool described =
            DescribeJavaObject(env, instance, &result->object_class_name, error_message);
        env->DeleteLocalRef(instance);
        if (!described) {
            env->DeleteGlobalRef(retained);
            result->object_handle = 0u;
            result->object_handle_is_global = false;
            return false;
        }
        return true;
    }

    jobject receiver = nullptr;
    if (!record.is_static) {
        if (receiver_handle == 0u) {
            SetError(error_message, "instance Java method invoke requires receiver");
            return false;
        }
        receiver = reinterpret_cast<jobject>(receiver_handle);
    }

    jclass clazz = nullptr;
    jmethodID method_id = nullptr;
    if (!ResolveJavaMethodId(env, record, receiver, &clazz, &method_id, error_message)) {
        return false;
    }

    ScopedJavaLocalRefs local_refs = {};
    local_refs.env = env;
    std::vector<NookJavaHookValue> raw_args(arg_count);
    for (std::size_t index = 0; index < arg_count; ++index) {
        if (!ConvertJavaJsValueToNookJavaHookValue(env,
                                                   parsed_signature.arg_descriptors[index],
                                                   args[index],
                                                   &local_refs,
                                                   &raw_args[index],
                                                   error_message)) {
            env->DeleteLocalRef(clazz);
            return false;
        }
    }

    NookJavaHookValue raw_result = {};
    const std::string& return_descriptor = parsed_signature.return_descriptor;
    if (return_descriptor == "V") {
        if (record.is_static) {
            env->CallStaticVoidMethodA(clazz, method_id,
                                       reinterpret_cast<const jvalue*>(raw_args.data()));
        } else {
            env->CallVoidMethodA(receiver, method_id,
                                 reinterpret_cast<const jvalue*>(raw_args.data()));
        }
    } else if (return_descriptor == "Z") {
        raw_result.z = record.is_static
            ? env->CallStaticBooleanMethodA(clazz, method_id,
                                            reinterpret_cast<const jvalue*>(raw_args.data()))
            : env->CallBooleanMethodA(receiver, method_id,
                                      reinterpret_cast<const jvalue*>(raw_args.data()));
    } else if (return_descriptor == "I") {
        raw_result.i = record.is_static
            ? env->CallStaticIntMethodA(clazz, method_id,
                                        reinterpret_cast<const jvalue*>(raw_args.data()))
            : env->CallIntMethodA(receiver, method_id,
                                  reinterpret_cast<const jvalue*>(raw_args.data()));
    } else if (return_descriptor == "J") {
        raw_result.j = record.is_static
            ? env->CallStaticLongMethodA(clazz, method_id,
                                         reinterpret_cast<const jvalue*>(raw_args.data()))
            : env->CallLongMethodA(receiver, method_id,
                                   reinterpret_cast<const jvalue*>(raw_args.data()));
    } else if (return_descriptor == "F") {
        raw_result.f = record.is_static
            ? env->CallStaticFloatMethodA(clazz, method_id,
                                          reinterpret_cast<const jvalue*>(raw_args.data()))
            : env->CallFloatMethodA(receiver, method_id,
                                    reinterpret_cast<const jvalue*>(raw_args.data()));
    } else if (return_descriptor == "D") {
        raw_result.d = record.is_static
            ? env->CallStaticDoubleMethodA(clazz, method_id,
                                           reinterpret_cast<const jvalue*>(raw_args.data()))
            : env->CallDoubleMethodA(receiver, method_id,
                                     reinterpret_cast<const jvalue*>(raw_args.data()));
    } else if (!return_descriptor.empty() &&
               (return_descriptor.front() == 'L' || return_descriptor.front() == '[')) {
        raw_result.l = record.is_static
            ? env->CallStaticObjectMethodA(clazz, method_id,
                                           reinterpret_cast<const jvalue*>(raw_args.data()))
            : env->CallObjectMethodA(receiver, method_id,
                                     reinterpret_cast<const jvalue*>(raw_args.data()));
    } else {
        env->DeleteLocalRef(clazz);
        SetError(error_message, "unsupported Java method return descriptor");
        return false;
    }

    env->DeleteLocalRef(clazz);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "java method invoke failed");
        return false;
    }

    return ConvertNookJavaHookValueToJavaJsValue(
        env, return_descriptor, raw_result, result, error_message);
}

bool DefaultReadJavaField(const JavaJsFieldRecord& record,
                          uint64_t receiver_handle,
                          JavaJsValue* result,
                          std::string* error_message) {
    if (result == nullptr) {
        SetError(error_message, "java field result output is required");
        return false;
    }

    JNIEnv* env = nullptr;
    jobject receiver = nullptr;
    JavaEnv static_jenv;
    if (record.is_static) {
        if (static_jenv.isNull()) {
            SetError(error_message, "JNIEnv is null while accessing static Java field");
            return false;
        }
        env = static_jenv.get();
    } else if (!ResolveJavaInstanceFieldAccessTarget(
                   record, receiver_handle, &env, &receiver, error_message)) {
        return false;
    }

    if (record.uses_declared_field_lookup) {
        jclass clazz = nullptr;
        jobject field = nullptr;
        if (!ResolveJavaDeclaredFieldObject(env, record, &clazz, &field, error_message)) {
            return false;
        }

        jclass field_class = env->FindClass("java/lang/reflect/Field");
        if (field_class == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(field);
            env->DeleteLocalRef(clazz);
            SetError(error_message, "read declared Java field reflection class unavailable");
            return false;
        }

        JavaJsValue value = {};
        if (record.signature == "I") {
            jmethodID get_int = env->GetMethodID(field_class, "getInt", "(Ljava/lang/Object;)I");
            if (get_int == nullptr) {
                env->DeleteLocalRef(field_class);
                env->DeleteLocalRef(field);
                env->DeleteLocalRef(clazz);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                SetError(error_message, "read declared Java int field helper unavailable");
                return false;
            }
            value.kind = JavaJsValueKind::kInt32;
            value.int_value = static_cast<int32_t>(
                env->CallIntMethod(field, get_int, record.is_static ? nullptr : receiver));
        } else if (record.signature == "Ljava/lang/String;") {
            jmethodID get_object =
                env->GetMethodID(field_class, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
            if (get_object == nullptr) {
                env->DeleteLocalRef(field_class);
                env->DeleteLocalRef(field);
                env->DeleteLocalRef(clazz);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                SetError(error_message, "read declared Java object field helper unavailable");
                return false;
            }
            value.kind = JavaJsValueKind::kString;
            jstring text = reinterpret_cast<jstring>(
                env->CallObjectMethod(field, get_object, record.is_static ? nullptr : receiver));
            const bool ok = ReadJStringUtf8(env, text, &value.string_value, error_message);
            if (text != nullptr) {
                env->DeleteLocalRef(text);
            }
            env->DeleteLocalRef(field_class);
            env->DeleteLocalRef(field);
            env->DeleteLocalRef(clazz);
            if (!ok) {
                return false;
            }
            *result = value;
            return true;
        } else {
            env->DeleteLocalRef(field_class);
            env->DeleteLocalRef(field);
            env->DeleteLocalRef(clazz);
            SetError(error_message, "unsupported declared Java field descriptor");
            return false;
        }

        env->DeleteLocalRef(field_class);
        env->DeleteLocalRef(field);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            SetError(error_message, "read declared Java field failed");
            return false;
        }

        *result = value;
        return true;
    }

    jclass clazz = nullptr;
    jfieldID field_id = nullptr;
    if (!ResolveJavaFieldId(env, record, &clazz, &field_id, error_message)) {
        return false;
    }

    JavaJsValue value = {};
    if (record.signature == "Z") {
        value.kind = JavaJsValueKind::kBoolean;
        value.bool_value = record.is_static
            ? env->GetStaticBooleanField(clazz, field_id) == JNI_TRUE
            : env->GetBooleanField(receiver, field_id) == JNI_TRUE;
    } else if (record.signature == "I") {
        value.kind = JavaJsValueKind::kInt32;
        value.int_value = record.is_static
            ? static_cast<int32_t>(env->GetStaticIntField(clazz, field_id))
            : static_cast<int32_t>(env->GetIntField(receiver, field_id));
    } else if (record.signature == "J") {
        value.kind = JavaJsValueKind::kInt64;
        value.int64_value = record.is_static
            ? static_cast<int64_t>(env->GetStaticLongField(clazz, field_id))
            : static_cast<int64_t>(env->GetLongField(receiver, field_id));
    } else if (record.signature == "F") {
        value.kind = JavaJsValueKind::kFloat;
        value.float_value = record.is_static
            ? env->GetStaticFloatField(clazz, field_id)
            : env->GetFloatField(receiver, field_id);
    } else if (record.signature == "D") {
        value.kind = JavaJsValueKind::kDouble;
        value.double_value = record.is_static
            ? env->GetStaticDoubleField(clazz, field_id)
            : env->GetDoubleField(receiver, field_id);
    } else if (record.signature == "Ljava/lang/String;") {
        value.kind = JavaJsValueKind::kString;
        jstring text = reinterpret_cast<jstring>(record.is_static
            ? env->GetStaticObjectField(clazz, field_id)
            : env->GetObjectField(receiver, field_id));
        const bool ok = ReadJStringUtf8(env, text, &value.string_value, error_message);
        if (text != nullptr) {
            env->DeleteLocalRef(text);
        }
        env->DeleteLocalRef(clazz);
        if (!ok) {
            return false;
        }
        *result = value;
        return true;
    } else {
        env->DeleteLocalRef(clazz);
        SetError(error_message, "unsupported Java field descriptor");
        return false;
    }

    env->DeleteLocalRef(clazz);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "read Java field failed");
        return false;
    }

    *result = value;
    return true;
}

bool DefaultWriteJavaField(const JavaJsFieldRecord& record,
                           uint64_t receiver_handle,
                           const JavaJsValue& value,
                           std::string* error_message) {
    JNIEnv* env = nullptr;
    jobject receiver = nullptr;
    JavaEnv static_jenv;
    if (record.is_static) {
        if (static_jenv.isNull()) {
            SetError(error_message, "JNIEnv is null while accessing static Java field");
            return false;
        }
        env = static_jenv.get();
    } else if (!ResolveJavaInstanceFieldAccessTarget(
                   record, receiver_handle, &env, &receiver, error_message)) {
        return false;
    }

    if (record.uses_declared_field_lookup) {
        jclass clazz = nullptr;
        jobject field = nullptr;
        if (!ResolveJavaDeclaredFieldObject(env, record, &clazz, &field, error_message)) {
            return false;
        }

        jclass field_class = env->FindClass("java/lang/reflect/Field");
        if (field_class == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(field);
            env->DeleteLocalRef(clazz);
            SetError(error_message, "write declared Java field reflection class unavailable");
            return false;
        }

        ScopedJavaLocalRefs local_refs = {};
        local_refs.env = env;
        NookJavaHookValue raw_value = {};
        if (!ConvertJavaJsValueToNookJavaHookValue(
                env, record.signature, value, &local_refs, &raw_value, error_message)) {
            env->DeleteLocalRef(field_class);
            env->DeleteLocalRef(field);
            env->DeleteLocalRef(clazz);
            return false;
        }

        if (record.signature == "I") {
            jmethodID set_int =
                env->GetMethodID(field_class, "setInt", "(Ljava/lang/Object;I)V");
            if (set_int == nullptr) {
                env->DeleteLocalRef(field_class);
                env->DeleteLocalRef(field);
                env->DeleteLocalRef(clazz);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                SetError(error_message, "write declared Java int field helper unavailable");
                return false;
            }
            env->CallVoidMethod(field, set_int, record.is_static ? nullptr : receiver, raw_value.i);
        } else if (record.signature == "Ljava/lang/String;") {
            jmethodID set_object =
                env->GetMethodID(field_class, "set", "(Ljava/lang/Object;Ljava/lang/Object;)V");
            if (set_object == nullptr) {
                env->DeleteLocalRef(field_class);
                env->DeleteLocalRef(field);
                env->DeleteLocalRef(clazz);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                SetError(error_message, "write declared Java object field helper unavailable");
                return false;
            }
            env->CallVoidMethod(field,
                                set_object,
                                record.is_static ? nullptr : receiver,
                                reinterpret_cast<jobject>(raw_value.l));
        } else {
            env->DeleteLocalRef(field_class);
            env->DeleteLocalRef(field);
            env->DeleteLocalRef(clazz);
            SetError(error_message, "unsupported declared Java field descriptor");
            return false;
        }

        env->DeleteLocalRef(field_class);
        env->DeleteLocalRef(field);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            SetError(error_message, "write declared Java field failed");
            return false;
        }

        return true;
    }

    jclass clazz = nullptr;
    jfieldID field_id = nullptr;
    if (!ResolveJavaFieldId(env, record, &clazz, &field_id, error_message)) {
        return false;
    }

    ScopedJavaLocalRefs local_refs = {};
    local_refs.env = env;
    NookJavaHookValue raw_value = {};
    if (!ConvertJavaJsValueToNookJavaHookValue(
            env, record.signature, value, &local_refs, &raw_value, error_message)) {
        env->DeleteLocalRef(clazz);
        return false;
    }

    if (record.signature == "Z") {
        if (record.is_static) {
            env->SetStaticBooleanField(clazz, field_id, raw_value.z);
        } else {
            env->SetBooleanField(receiver, field_id, raw_value.z);
        }
    } else if (record.signature == "I") {
        if (record.is_static) {
            env->SetStaticIntField(clazz, field_id, raw_value.i);
        } else {
            env->SetIntField(receiver, field_id, raw_value.i);
        }
    } else if (record.signature == "J") {
        if (record.is_static) {
            env->SetStaticLongField(clazz, field_id, raw_value.j);
        } else {
            env->SetLongField(receiver, field_id, raw_value.j);
        }
    } else if (record.signature == "F") {
        if (record.is_static) {
            env->SetStaticFloatField(clazz, field_id, raw_value.f);
        } else {
            env->SetFloatField(receiver, field_id, raw_value.f);
        }
    } else if (record.signature == "D") {
        if (record.is_static) {
            env->SetStaticDoubleField(clazz, field_id, raw_value.d);
        } else {
            env->SetDoubleField(receiver, field_id, raw_value.d);
        }
    } else if (record.signature == "Ljava/lang/String;") {
        if (record.is_static) {
            env->SetStaticObjectField(
                clazz, field_id, reinterpret_cast<jobject>(raw_value.l));
        } else {
            env->SetObjectField(
                receiver, field_id, reinterpret_cast<jobject>(raw_value.l));
        }
    } else {
        env->DeleteLocalRef(clazz);
        SetError(error_message, "unsupported Java field descriptor");
        return false;
    }

    env->DeleteLocalRef(clazz);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "write Java field failed");
        return false;
    }

    return true;
}

bool DefaultRetainJavaObject(uint64_t object_handle,
                             uint64_t* retained_handle,
                             std::string* error_message) {
    if (retained_handle == nullptr) {
        SetError(error_message, "retained Java object output is required");
        return false;
    }
    if (object_handle == 0u) {
        SetError(error_message, "Java retain source handle is invalid");
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while retaining Java object");
        return false;
    }

    JNIEnv* env = jenv.get();
    jobject source = reinterpret_cast<jobject>(object_handle);
    jobject retained = env->NewGlobalRef(source);
    if (retained == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "NewGlobalRef failed");
        return false;
    }

    *retained_handle = reinterpret_cast<uint64_t>(retained);
    return true;
}

bool DefaultReleaseJavaObject(uint64_t object_handle, std::string* error_message) {
    if (object_handle == 0u) {
        SetError(error_message, "Java release source handle is invalid");
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while releasing Java object");
        return false;
    }

    JNIEnv* env = jenv.get();
    env->DeleteGlobalRef(reinterpret_cast<jobject>(object_handle));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SetError(error_message, "DeleteGlobalRef failed");
        return false;
    }

    return true;
}

bool DefaultEnumerateJavaObjects(const std::string& class_name,
                                 uint64_t loader_handle,
                                 std::vector<JavaJsValue>* matches,
                                 std::string* error_message) {
    if (matches == nullptr) {
        SetError(error_message, "java choose matches output is required");
        return false;
    }
    matches->clear();

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while enumerating Java objects");
        return false;
    }

    JNIEnv* env = jenv.get();
    jobject loader = loader_handle == 0u ? nullptr : reinterpret_cast<jobject>(loader_handle);
    jclass target_class = JavaHook::FindClassWithLoader(env, loader, class_name.c_str());
    if (target_class == nullptr) {
        SetError(error_message, "Java.choose FindClass failed");
        return false;
    }

    jclass vmdebug_class = env->FindClass("dalvik/system/VMDebug");
    jclass class_class = env->FindClass("java/lang/Class");
    if (vmdebug_class == nullptr || class_class == nullptr) {
        if (vmdebug_class != nullptr) {
            env->DeleteLocalRef(vmdebug_class);
        }
        if (class_class != nullptr) {
            env->DeleteLocalRef(class_class);
        }
        env->DeleteLocalRef(target_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.choose VMDebug classes unavailable");
        return false;
    }

    jmethodID get_instances = env->GetStaticMethodID(
        vmdebug_class,
        "getInstancesOfClasses",
        "([Ljava/lang/Class;Z)[[Ljava/lang/Object;");
    if (get_instances == nullptr) {
        env->DeleteLocalRef(vmdebug_class);
        env->DeleteLocalRef(class_class);
        env->DeleteLocalRef(target_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.choose getInstancesOfClasses unavailable");
        return false;
    }

    jobjectArray requested_classes = env->NewObjectArray(1, class_class, nullptr);
    env->DeleteLocalRef(class_class);
    if (requested_classes == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(vmdebug_class);
        env->DeleteLocalRef(target_class);
        if (requested_classes != nullptr) {
            env->DeleteLocalRef(requested_classes);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.choose class array allocation failed");
        return false;
    }

    env->SetObjectArrayElement(requested_classes, 0, target_class);
    env->DeleteLocalRef(target_class);
    if (env->ExceptionCheck()) {
        env->DeleteLocalRef(vmdebug_class);
        env->DeleteLocalRef(requested_classes);
        env->ExceptionClear();
        SetError(error_message, "Java.choose class array population failed");
        return false;
    }

    jobjectArray grouped_instances = reinterpret_cast<jobjectArray>(
        env->CallStaticObjectMethod(vmdebug_class,
                                    get_instances,
                                    requested_classes,
                                    JNI_TRUE));
    env->DeleteLocalRef(vmdebug_class);
    env->DeleteLocalRef(requested_classes);
    if (grouped_instances == nullptr || env->ExceptionCheck()) {
        if (grouped_instances != nullptr) {
            env->DeleteLocalRef(grouped_instances);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "Java.choose getInstancesOfClasses failed");
        return false;
    }

    const jsize group_count = env->GetArrayLength(grouped_instances);
    for (jsize group_index = 0; group_index < group_count; ++group_index) {
        jobjectArray instance_group = reinterpret_cast<jobjectArray>(
            env->GetObjectArrayElement(grouped_instances, group_index));
        if (instance_group == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            continue;
        }

        const jsize instance_count = env->GetArrayLength(instance_group);
        for (jsize instance_index = 0; instance_index < instance_count; ++instance_index) {
            jobject instance = env->GetObjectArrayElement(instance_group, instance_index);
            if (instance == nullptr) {
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                continue;
            }

            jobject retained = env->NewGlobalRef(instance);
            env->DeleteLocalRef(instance);
            if (retained == nullptr || env->ExceptionCheck()) {
                if (retained != nullptr) {
                    env->DeleteGlobalRef(retained);
                }
                env->DeleteLocalRef(instance_group);
                env->DeleteLocalRef(grouped_instances);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                SetError(error_message, "Java.choose object retain failed");
                return false;
            }

            JavaJsValue match = {};
            match.kind = JavaJsValueKind::kObject;
            match.object_handle = reinterpret_cast<uint64_t>(retained);
            match.object_handle_is_global = true;
            if (!DescribeJavaObject(env,
                                    retained,
                                    &match.object_class_name,
                                    error_message)) {
                env->DeleteGlobalRef(retained);
                env->DeleteLocalRef(instance_group);
                env->DeleteLocalRef(grouped_instances);
                return false;
            }
            matches->push_back(std::move(match));
        }

        env->DeleteLocalRef(instance_group);
    }

    env->DeleteLocalRef(grouped_instances);
    return true;
}

bool DefaultEnumerateLoadedJavaClasses(std::vector<std::string>* class_names,
                                       std::string* error_message) {
    if (class_names == nullptr) {
        SetError(error_message, "java loaded classes output is required");
        return false;
    }
    class_names->clear();

    const NookStatus status = NookJavaHookInitialize();
    if (status != NOOK_STATUS_OK) {
        SetError(error_message, "JavaHook initialize failed");
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while enumerating loaded Java classes");
        return false;
    }

    if (ArtInternals::RuntimeInstance == 0u) {
        SetError(error_message, "ART runtime is unavailable");
        return false;
    }
    if (ArtInternals::newlocalrefFn == nullptr) {
        SetError(error_message, "ART NewLocalRef helper is unavailable");
        return false;
    }

    void* class_linker = *reinterpret_cast<void**>(
        ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
    if (class_linker == nullptr) {
        SetError(error_message, "ART class linker pointer is unavailable");
        return false;
    }

    const char* libart_path = tool::find_path_from_maps("libart.so");
    if (libart_path == nullptr) {
        SetError(error_message, "libart.so path not found");
        return false;
    }

    auto visit_classes = reinterpret_cast<VisitArtClassesFn>(
        tool::get_address_from_module(
            libart_path,
            "_ZN3art11ClassLinker12VisitClassesEPNS_12ClassVisitorE",
            true));
    auto visit_classes_legacy = reinterpret_cast<VisitArtClassesLegacyFn>(
        tool::get_address_from_module(
            libart_path,
            "_ZN3art11ClassLinker12VisitClassesEPFbPNS_6mirror5ClassEPvES4_",
            true));
    if (visit_classes == nullptr && visit_classes_legacy == nullptr) {
        SetError(error_message, "ART ClassLinker::VisitClasses is unavailable");
        return false;
    }

    EnumerateLoadedClassesContext context = {};
    context.env = jenv.get();
    context.class_names = class_names;

    bool invoked = false;
    if (!WithAllArtThreadsSuspended(
            "Enumerate Loaded Classes",
            error_message,
            [&]() {
                if (visit_classes != nullptr) {
                    ArtLoadedClassVisitor visitor = MakeArtLoadedClassVisitor(&context);
                    visit_classes(class_linker, &visitor);
                } else {
                    visit_classes_legacy(
                        class_linker, VisitLoadedClassForEnumerationLegacy, &context);
                }
                invoked = true;
            })) {
        return false;
    }
    if (!invoked) {
        SetError(error_message, "ART loaded class enumeration did not run");
        return false;
    }

    return true;
}

bool DefaultEnumerateJavaClassLoaders(std::vector<JavaJsValue>* matches,
                                      std::string* error_message) {
    if (matches == nullptr) {
        SetError(error_message, "java class-loader matches output is required");
        return false;
    }
    matches->clear();

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while enumerating Java class loaders");
        return false;
    }

    JNIEnv* env = jenv.get();
    std::vector<jobject> retained;

    auto clear_exception = [env]() {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    };

    auto release_retained = [&]() {
        for (jobject retained_loader : retained) {
            if (retained_loader != nullptr) {
                env->DeleteGlobalRef(retained_loader);
            }
        }
        retained.clear();
    };

    auto contains_loader = [&](jobject loader) {
        for (jobject retained_loader : retained) {
            if (retained_loader != nullptr && env->IsSameObject(retained_loader, loader)) {
                return true;
            }
        }
        return false;
    };

    auto add_loader = [&](jobject loader) -> bool {
        if (loader == nullptr) {
            return true;
        }
        if (contains_loader(loader)) {
            return true;
        }

        jobject retained_loader = env->NewGlobalRef(loader);
        if (retained_loader == nullptr || env->ExceptionCheck()) {
            clear_exception();
            SetError(error_message, "NewGlobalRef failed for Java class loader");
            return false;
        }

        JavaJsValue match = {};
        match.kind = JavaJsValueKind::kObject;
        match.object_handle = reinterpret_cast<uint64_t>(retained_loader);
        match.object_handle_is_global = true;
        if (!DescribeJavaObject(env,
                                retained_loader,
                                &match.object_class_name,
                                error_message)) {
            env->DeleteGlobalRef(retained_loader);
            return false;
        }

        retained.push_back(retained_loader);
        matches->push_back(std::move(match));
        return true;
    };

    auto add_loader_chain = [&](jobject loader) -> bool {
        if (loader == nullptr) {
            return true;
        }

        jclass class_loader_class = env->FindClass("java/lang/ClassLoader");
        if (class_loader_class == nullptr) {
            clear_exception();
            SetError(error_message, "java/lang/ClassLoader is unavailable");
            return false;
        }

        jmethodID get_parent = env->GetMethodID(
            class_loader_class, "getParent", "()Ljava/lang/ClassLoader;");
        env->DeleteLocalRef(class_loader_class);
        if (get_parent == nullptr) {
            clear_exception();
            SetError(error_message, "ClassLoader.getParent is unavailable");
            return false;
        }

        jobject current = env->NewLocalRef(loader);
        if (current == nullptr || env->ExceptionCheck()) {
            clear_exception();
            SetError(error_message, "NewLocalRef failed for class loader chain");
            return false;
        }

        while (current != nullptr) {
            if (!add_loader(current)) {
                env->DeleteLocalRef(current);
                return false;
            }

            jobject parent = env->CallObjectMethod(current, get_parent);
            if (env->ExceptionCheck()) {
                clear_exception();
                env->DeleteLocalRef(current);
                SetError(error_message, "ClassLoader.getParent failed");
                return false;
            }
            env->DeleteLocalRef(current);
            current = parent;
        }

        return true;
    };

    jobject app_loader = JavaHookLoaderResolver::GetApplicationClassLoader(env);
    if (app_loader != nullptr) {
        if (!add_loader_chain(app_loader)) {
            env->DeleteLocalRef(app_loader);
            release_retained();
            return false;
        }
        env->DeleteLocalRef(app_loader);
    } else {
        clear_exception();
    }

    jclass thread_class = env->FindClass("java/lang/Thread");
    if (thread_class != nullptr) {
        jmethodID current_thread = env->GetStaticMethodID(
            thread_class, "currentThread", "()Ljava/lang/Thread;");
        jmethodID get_context_class_loader = env->GetMethodID(
            thread_class, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
        if (current_thread != nullptr && get_context_class_loader != nullptr) {
            jobject thread = env->CallStaticObjectMethod(thread_class, current_thread);
            if (thread != nullptr && !env->ExceptionCheck()) {
                jobject context_loader =
                    env->CallObjectMethod(thread, get_context_class_loader);
                if (!env->ExceptionCheck()) {
                    if (!add_loader_chain(context_loader)) {
                        if (context_loader != nullptr) {
                            env->DeleteLocalRef(context_loader);
                        }
                        env->DeleteLocalRef(thread);
                        env->DeleteLocalRef(thread_class);
                        release_retained();
                        return false;
                    }
                } else {
                    clear_exception();
                }
                if (context_loader != nullptr) {
                    env->DeleteLocalRef(context_loader);
                }
                env->DeleteLocalRef(thread);
            } else {
                clear_exception();
            }
        } else {
            clear_exception();
        }
        env->DeleteLocalRef(thread_class);
    } else {
        clear_exception();
    }

    jclass class_loader_class = env->FindClass("java/lang/ClassLoader");
    if (class_loader_class != nullptr) {
        jmethodID get_system_class_loader = env->GetStaticMethodID(
            class_loader_class, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
        if (get_system_class_loader != nullptr) {
            jobject system_loader =
                env->CallStaticObjectMethod(class_loader_class, get_system_class_loader);
            if (!env->ExceptionCheck()) {
                if (!add_loader_chain(system_loader)) {
                    if (system_loader != nullptr) {
                        env->DeleteLocalRef(system_loader);
                    }
                    env->DeleteLocalRef(class_loader_class);
                    release_retained();
                    return false;
                }
            } else {
                clear_exception();
            }
            if (system_loader != nullptr) {
                env->DeleteLocalRef(system_loader);
            }
        } else {
            clear_exception();
        }
        env->DeleteLocalRef(class_loader_class);
    } else {
        clear_exception();
    }

    return true;
}

std::string BuildResolveSignatureDebugMessage(const std::string& class_name,
                                              const std::string& method_name,
                                              const std::vector<std::string>& argument_type_names,
                                              bool is_static,
                                              const std::vector<std::string>& candidates) {
    std::ostringstream stream;
    stream << "ResolveJavaMethodSignature no method match"
           << " class=" << class_name
           << " method=" << method_name
           << " static=" << (is_static ? "true" : "false")
           << " args=[";
    for (size_t index = 0; index < argument_type_names.size(); ++index) {
        if (index > 0u) {
            stream << ",";
        }
        stream << argument_type_names[index];
    }
    stream << "]";
    if (!candidates.empty()) {
        stream << " candidates=";
        for (size_t index = 0; index < candidates.size(); ++index) {
            if (index > 0u) {
                stream << " | ";
            }
            stream << candidates[index];
        }
    }
    return stream.str();
}

bool ResolveWildcardJavaMethodSignature(const JavaJsHookRequest& request,
                                        std::string* resolved_signature,
                                        std::string* error_message) {
    if (resolved_signature == nullptr) {
        SetError(error_message, "resolved Java signature output is required");
        return false;
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while resolving wildcard Java signature");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = ResolveJavaClass(env,
                                    request.class_name,
                                    request.loader_handle,
                                    error_message);
    if (clazz == nullptr) {
        if (error_message == nullptr || error_message->empty()) {
            SetError(error_message, "ResolveWildcardJavaMethodSignature FindClass failed");
        }
        return false;
    }

    jclass class_class = env->FindClass("java/lang/Class");
    jclass method_class = env->FindClass("java/lang/reflect/Method");
    jclass modifier_class = env->FindClass("java/lang/reflect/Modifier");
    if (class_class == nullptr || method_class == nullptr || modifier_class == nullptr) {
        if (class_class != nullptr) env->DeleteLocalRef(class_class);
        if (method_class != nullptr) env->DeleteLocalRef(method_class);
        if (modifier_class != nullptr) env->DeleteLocalRef(modifier_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveWildcardJavaMethodSignature reflection classes unavailable");
        return false;
    }

    jmethodID get_declared_methods =
        env->GetMethodID(class_class, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jmethodID get_superclass =
        env->GetMethodID(class_class, "getSuperclass", "()Ljava/lang/Class;");
    jmethodID method_get_name = env->GetMethodID(method_class, "getName", "()Ljava/lang/String;");
    jmethodID method_get_modifiers = env->GetMethodID(method_class, "getModifiers", "()I");
    jmethodID method_get_parameter_types =
        env->GetMethodID(method_class, "getParameterTypes", "()[Ljava/lang/Class;");
    jmethodID method_get_return_type =
        env->GetMethodID(method_class, "getReturnType", "()Ljava/lang/Class;");
    jmethodID modifier_is_static = env->GetStaticMethodID(modifier_class, "isStatic", "(I)Z");
    if (get_declared_methods == nullptr ||
        get_superclass == nullptr ||
        method_get_name == nullptr ||
        method_get_modifiers == nullptr ||
        method_get_parameter_types == nullptr ||
        method_get_return_type == nullptr ||
        modifier_is_static == nullptr) {
        env->DeleteLocalRef(class_class);
        env->DeleteLocalRef(method_class);
        env->DeleteLocalRef(modifier_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveWildcardJavaMethodSignature reflection methods unavailable");
        return false;
    }

    int matched_count = 0;
    std::string matched_signature;
    jclass current_class = clazz;
    while (current_class != nullptr) {
        jobjectArray methods =
            reinterpret_cast<jobjectArray>(env->CallObjectMethod(current_class, get_declared_methods));
        if (methods == nullptr || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(class_class);
            env->DeleteLocalRef(method_class);
            env->DeleteLocalRef(modifier_class);
            env->DeleteLocalRef(current_class);
            SetError(error_message, "ResolveWildcardJavaMethodSignature getDeclaredMethods failed");
            return false;
        }

        const jsize method_count = env->GetArrayLength(methods);
        for (jsize index = 0; index < method_count; ++index) {
            jobject method_object = env->GetObjectArrayElement(methods, index);
            if (method_object == nullptr) {
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                continue;
            }

            jstring name_string =
                reinterpret_cast<jstring>(env->CallObjectMethod(method_object, method_get_name));
            std::string reflected_name;
            if (name_string == nullptr || !ReadJavaStringUtf8(env, name_string, &reflected_name)) {
                if (name_string != nullptr) env->DeleteLocalRef(name_string);
                env->DeleteLocalRef(method_object);
                continue;
            }
            env->DeleteLocalRef(name_string);

            if (reflected_name != request.method_name) {
                env->DeleteLocalRef(method_object);
                continue;
            }

            jint modifiers = env->CallIntMethod(method_object, method_get_modifiers);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(method_object);
                continue;
            }
            const bool reflected_is_static =
                env->CallStaticBooleanMethod(modifier_class, modifier_is_static, modifiers) == JNI_TRUE;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(method_object);
                continue;
            }
            if (reflected_is_static != request.is_static) {
                env->DeleteLocalRef(method_object);
                continue;
            }

            jobjectArray parameter_types = reinterpret_cast<jobjectArray>(
                env->CallObjectMethod(method_object, method_get_parameter_types));
            jobject return_type = env->CallObjectMethod(method_object, method_get_return_type);
            if (parameter_types == nullptr || return_type == nullptr || env->ExceptionCheck()) {
                if (parameter_types != nullptr) env->DeleteLocalRef(parameter_types);
                if (return_type != nullptr) env->DeleteLocalRef(return_type);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(method_object);
                continue;
            }

            std::string signature = "(";
            bool ok = true;
            const jsize parameter_count = env->GetArrayLength(parameter_types);
            for (jsize parameter_index = 0; parameter_index < parameter_count; ++parameter_index) {
                jobject parameter_type = env->GetObjectArrayElement(parameter_types, parameter_index);
                std::string descriptor;
                if (parameter_type == nullptr || !DescribeJavaClassObject(env, parameter_type, &descriptor)) {
                    ok = false;
                } else {
                    signature += descriptor;
                }
                if (parameter_type != nullptr) {
                    env->DeleteLocalRef(parameter_type);
                }
                if (!ok) {
                    break;
                }
            }

            std::string return_descriptor;
            if (!ok || !DescribeJavaClassObject(env, return_type, &return_descriptor)) {
                ok = false;
            } else {
                signature += ")";
                signature += return_descriptor;
            }

            env->DeleteLocalRef(parameter_types);
            env->DeleteLocalRef(return_type);
            env->DeleteLocalRef(method_object);
            if (!ok) {
                continue;
            }

            matched_signature = signature;
            matched_count++;
            if (matched_count > 1) {
                break;
            }
        }

        env->DeleteLocalRef(methods);
        if (matched_count > 0) {
            env->DeleteLocalRef(current_class);
            break;
        }
        jclass next_class =
            reinterpret_cast<jclass>(env->CallObjectMethod(current_class, get_superclass));
        env->DeleteLocalRef(current_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(class_class);
            env->DeleteLocalRef(method_class);
            env->DeleteLocalRef(modifier_class);
            SetError(error_message, "ResolveWildcardJavaMethodSignature getSuperclass failed");
            return false;
        }
        current_class = next_class;
    }

    env->DeleteLocalRef(class_class);
    env->DeleteLocalRef(method_class);
    env->DeleteLocalRef(modifier_class);
    if (matched_count != 1) {
        SetError(error_message,
                 matched_count == 0
                     ? "ResolveWildcardJavaMethodSignature no method match"
                     : "ResolveWildcardJavaMethodSignature ambiguous match");
        return false;
    }

    *resolved_signature = matched_signature;
    return true;
}

bool ResolveJavaConstructorSignatureByTypeNames(const std::string& class_name,
                                                const std::vector<std::string>& argument_type_names,
                                                uint64_t loader_handle,
                                                std::string* resolved_signature,
                                                std::string* error_message) {
    if (resolved_signature == nullptr) {
        SetError(error_message, "resolved Java constructor signature output is required");
        return false;
    }

    std::vector<std::string> expected_descriptors;
    expected_descriptors.reserve(argument_type_names.size());
    for (const std::string& type_name : argument_type_names) {
        if (type_name == kJavaInvokeNullTypeCandidate) {
            expected_descriptors.push_back(type_name);
            continue;
        }
        std::string descriptor;
        if (!TypeNameToDescriptor(type_name, &descriptor, error_message)) {
            return false;
        }
        expected_descriptors.push_back(std::move(descriptor));
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while resolving Java constructor signature");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = ResolveJavaClass(env, class_name, loader_handle, error_message);
    if (clazz == nullptr) {
        if (error_message == nullptr || error_message->empty()) {
            SetError(error_message, "ResolveJavaConstructorSignature FindClass failed");
        }
        return false;
    }

    jclass class_class = env->FindClass("java/lang/Class");
    jclass constructor_class = env->FindClass("java/lang/reflect/Constructor");
    if (class_class == nullptr || constructor_class == nullptr) {
        if (class_class != nullptr) env->DeleteLocalRef(class_class);
        if (constructor_class != nullptr) env->DeleteLocalRef(constructor_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaConstructorSignature reflection classes unavailable");
        return false;
    }

    jmethodID get_declared_constructors =
        env->GetMethodID(class_class,
                         "getDeclaredConstructors",
                         "()[Ljava/lang/reflect/Constructor;");
    jmethodID constructor_get_parameter_types =
        env->GetMethodID(constructor_class, "getParameterTypes", "()[Ljava/lang/Class;");
    if (get_declared_constructors == nullptr || constructor_get_parameter_types == nullptr) {
        env->DeleteLocalRef(class_class);
        env->DeleteLocalRef(constructor_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaConstructorSignature reflection methods unavailable");
        return false;
    }

    jobjectArray constructors = reinterpret_cast<jobjectArray>(
        env->CallObjectMethod(clazz, get_declared_constructors));
    env->DeleteLocalRef(class_class);
    env->DeleteLocalRef(constructor_class);
    env->DeleteLocalRef(clazz);
    if (constructors == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaConstructorSignature getDeclaredConstructors failed");
        return false;
    }

    int matched_count = 0;
    std::string matched_signature;
    const jsize constructor_count = env->GetArrayLength(constructors);
    for (jsize index = 0; index < constructor_count; ++index) {
        jobject constructor_object = env->GetObjectArrayElement(constructors, index);
        if (constructor_object == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            continue;
        }

        jobjectArray parameter_types = reinterpret_cast<jobjectArray>(
            env->CallObjectMethod(constructor_object, constructor_get_parameter_types));
        if (parameter_types == nullptr || env->ExceptionCheck()) {
            if (parameter_types != nullptr) env->DeleteLocalRef(parameter_types);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(constructor_object);
            continue;
        }

        std::string signature = "(";
        bool signature_ok = true;
        const jsize parameter_count = env->GetArrayLength(parameter_types);
        for (jsize parameter_index = 0; signature_ok && parameter_index < parameter_count; ++parameter_index) {
            jobject parameter_type = env->GetObjectArrayElement(parameter_types, parameter_index);
            std::string descriptor;
            if (parameter_type == nullptr || !DescribeJavaClassObject(env, parameter_type, &descriptor)) {
                signature_ok = false;
            } else {
                signature += descriptor;
            }
            if (parameter_type != nullptr) {
                env->DeleteLocalRef(parameter_type);
            }
        }
        signature += ")V";

        env->DeleteLocalRef(parameter_types);
        env->DeleteLocalRef(constructor_object);
        if (!signature_ok) {
            continue;
        }
        if (static_cast<size_t>(parameter_count) != expected_descriptors.size()) {
            continue;
        }

        bool ok = true;
        size_t compare_offset = 1u;
        for (size_t parameter_index = 0; parameter_index < expected_descriptors.size(); ++parameter_index) {
            const std::string& expected = expected_descriptors[parameter_index];
            if (signature.compare(compare_offset, expected.size(), expected) != 0) {
                ok = false;
                break;
            }
            compare_offset += expected.size();
        }
        if (!ok) {
            continue;
        }

        matched_signature = signature;
        matched_count++;
        if (matched_count > 1) {
            break;
        }
    }

    env->DeleteLocalRef(constructors);
    if (matched_count != 1) {
        SetError(error_message,
                 matched_count == 0
                     ? "ResolveJavaConstructorSignature no constructor match"
                     : "ResolveJavaConstructorSignature ambiguous match");
        return false;
    }

    *resolved_signature = matched_signature;
    return true;
}

bool ReflectiveJavaDescriptorIsAssignableFrom(const std::string& target_descriptor,
                                              const std::string& source_descriptor,
                                              void* opaque) {
    if (target_descriptor == source_descriptor) {
        return true;
    }
    if (!IsReferenceLikeJavaDescriptor(target_descriptor) ||
        !IsReferenceLikeJavaDescriptor(source_descriptor)) {
        return false;
    }

    auto* context = static_cast<JavaDescriptorAssignabilityContext*>(opaque);
    if (context == nullptr || context->env == nullptr) {
        return false;
    }

    std::string local_error;
    std::string target_class_name;
    if (!NormalizeJavaDescriptorForClassLookup(target_descriptor, &target_class_name, &local_error)) {
        context->had_error = true;
        SetError(context->error_message,
                 local_error.empty() ? "ResolveJavaMethodSignature target class normalize failed"
                                     : local_error.c_str());
        return false;
    }
    jclass target_class = ResolveJavaClass(context->env,
                                           target_class_name,
                                           context->loader_handle,
                                           &local_error);
    if (target_class == nullptr) {
        context->had_error = true;
        SetError(context->error_message,
                 local_error.empty() ? "ResolveJavaMethodSignature target class resolve failed"
                                     : local_error.c_str());
        return false;
    }

    std::string source_class_name;
    if (!NormalizeJavaDescriptorForClassLookup(source_descriptor, &source_class_name, &local_error)) {
        context->env->DeleteLocalRef(target_class);
        context->had_error = true;
        SetError(context->error_message,
                 local_error.empty() ? "ResolveJavaMethodSignature source class normalize failed"
                                     : local_error.c_str());
        return false;
    }
    jclass source_class = ResolveJavaClass(context->env,
                                           source_class_name,
                                           context->loader_handle,
                                           &local_error);
    if (source_class == nullptr) {
        context->env->DeleteLocalRef(target_class);
        context->had_error = true;
        SetError(context->error_message,
                 local_error.empty() ? "ResolveJavaMethodSignature source class resolve failed"
                                     : local_error.c_str());
        return false;
    }

    const bool result =
        context->env->IsAssignableFrom(source_class, target_class) == JNI_TRUE;
    if (context->env->ExceptionCheck()) {
        context->env->ExceptionClear();
        context->had_error = true;
        SetError(context->error_message, "ResolveJavaMethodSignature IsAssignableFrom failed");
    }
    context->env->DeleteLocalRef(source_class);
    context->env->DeleteLocalRef(target_class);
    return result && !context->had_error;
}

bool ResolveJavaMethodSignatureByTypeNames(const std::string& class_name,
                                           const std::string& method_name,
                                           const std::vector<std::string>& argument_type_names,
                                           uint64_t loader_handle,
                                           bool is_static,
                                           std::string* resolved_signature,
                                           std::string* error_message) {
    if (IsJavaConstructorMethodName(method_name)) {
        if (is_static) {
            SetError(error_message, "ResolveJavaConstructorSignature static constructor is invalid");
            return false;
        }
        return ResolveJavaConstructorSignatureByTypeNames(
            class_name, argument_type_names, loader_handle, resolved_signature, error_message);
    }

    if (resolved_signature == nullptr) {
        SetError(error_message, "resolved Java signature output is required");
        return false;
    }

    std::vector<std::string> expected_descriptors;
    expected_descriptors.reserve(argument_type_names.size());
    for (const std::string& type_name : argument_type_names) {
        std::string descriptor;
        if (!TypeNameToDescriptor(type_name, &descriptor, error_message)) {
            return false;
        }
        expected_descriptors.push_back(std::move(descriptor));
    }

    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while resolving Java overload signature");
        return false;
    }

    JNIEnv* env = jenv.get();
    jclass clazz = ResolveJavaClass(env, class_name, loader_handle, error_message);
    if (clazz == nullptr) {
        if (error_message == nullptr || error_message->empty()) {
            SetError(error_message, "ResolveJavaMethodSignature FindClass failed");
        }
        return false;
    }

    jclass class_class = env->FindClass("java/lang/Class");
    jclass method_class = env->FindClass("java/lang/reflect/Method");
    jclass modifier_class = env->FindClass("java/lang/reflect/Modifier");
    if (class_class == nullptr || method_class == nullptr || modifier_class == nullptr) {
        if (class_class != nullptr) env->DeleteLocalRef(class_class);
        if (method_class != nullptr) env->DeleteLocalRef(method_class);
        if (modifier_class != nullptr) env->DeleteLocalRef(modifier_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaMethodSignature reflection classes unavailable");
        return false;
    }

    jmethodID get_declared_methods =
        env->GetMethodID(class_class, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jmethodID get_superclass =
        env->GetMethodID(class_class, "getSuperclass", "()Ljava/lang/Class;");
    jmethodID method_get_name = env->GetMethodID(method_class, "getName", "()Ljava/lang/String;");
    jmethodID method_get_modifiers = env->GetMethodID(method_class, "getModifiers", "()I");
    jmethodID method_get_parameter_types =
        env->GetMethodID(method_class, "getParameterTypes", "()[Ljava/lang/Class;");
    jmethodID method_get_return_type =
        env->GetMethodID(method_class, "getReturnType", "()Ljava/lang/Class;");
    jmethodID modifier_is_static = env->GetStaticMethodID(modifier_class, "isStatic", "(I)Z");
    if (get_declared_methods == nullptr ||
        get_superclass == nullptr ||
        method_get_name == nullptr ||
        method_get_modifiers == nullptr ||
        method_get_parameter_types == nullptr ||
        method_get_return_type == nullptr ||
        modifier_is_static == nullptr) {
        env->DeleteLocalRef(class_class);
        env->DeleteLocalRef(method_class);
        env->DeleteLocalRef(modifier_class);
        env->DeleteLocalRef(clazz);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        SetError(error_message, "ResolveJavaMethodSignature reflection methods unavailable");
        return false;
    }

    struct MatchedMethodCandidate {
        std::string signature;
        std::vector<std::string> parameter_descriptors;
    };

    std::vector<MatchedMethodCandidate> matched_methods;
    std::vector<std::string> debug_candidates;
    JavaDescriptorAssignabilityContext assignability_context = {};
    assignability_context.env = env;
    assignability_context.loader_handle = loader_handle;
    assignability_context.error_message = error_message;
    jclass current_class = clazz;
    while (current_class != nullptr) {
        jobjectArray methods =
            reinterpret_cast<jobjectArray>(env->CallObjectMethod(current_class, get_declared_methods));
        if (methods == nullptr || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(class_class);
            env->DeleteLocalRef(method_class);
            env->DeleteLocalRef(modifier_class);
            env->DeleteLocalRef(current_class);
            SetError(error_message, "ResolveJavaMethodSignature getDeclaredMethods failed");
            return false;
        }

        const jsize method_count = env->GetArrayLength(methods);
        for (jsize index = 0; index < method_count; ++index) {
            jobject method_object = env->GetObjectArrayElement(methods, index);
            if (method_object == nullptr) {
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                continue;
            }

            jstring name_string =
                reinterpret_cast<jstring>(env->CallObjectMethod(method_object, method_get_name));
            std::string reflected_name;
            if (name_string == nullptr || !ReadJavaStringUtf8(env, name_string, &reflected_name)) {
                if (name_string != nullptr) env->DeleteLocalRef(name_string);
                env->DeleteLocalRef(method_object);
                continue;
            }
            env->DeleteLocalRef(name_string);

            if (reflected_name != method_name) {
                env->DeleteLocalRef(method_object);
                continue;
            }

            std::ostringstream candidate_stream;
            candidate_stream << reflected_name;

            jint modifiers = env->CallIntMethod(method_object, method_get_modifiers);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(method_object);
                continue;
            }
            const bool reflected_is_static =
                env->CallStaticBooleanMethod(modifier_class, modifier_is_static, modifiers) == JNI_TRUE;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(method_object);
                continue;
            }

            jobjectArray parameter_types = reinterpret_cast<jobjectArray>(
                env->CallObjectMethod(method_object, method_get_parameter_types));
            jobject return_type = env->CallObjectMethod(method_object, method_get_return_type);
            if (parameter_types == nullptr || return_type == nullptr || env->ExceptionCheck()) {
                if (parameter_types != nullptr) env->DeleteLocalRef(parameter_types);
                if (return_type != nullptr) env->DeleteLocalRef(return_type);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(method_object);
                continue;
            }

            std::string signature = "(";
            bool signature_ok = true;
            const jsize parameter_count = env->GetArrayLength(parameter_types);
            std::vector<std::string> reflected_parameter_descriptors;
            reflected_parameter_descriptors.reserve(static_cast<size_t>(parameter_count));
            for (jsize parameter_index = 0; signature_ok && parameter_index < parameter_count; ++parameter_index) {
                jobject parameter_type = env->GetObjectArrayElement(parameter_types, parameter_index);
                std::string descriptor;
                if (parameter_type == nullptr || !DescribeJavaClassObject(env, parameter_type, &descriptor)) {
                    signature_ok = false;
                } else {
                    signature += descriptor;
                    reflected_parameter_descriptors.push_back(descriptor);
                }
                if (parameter_type != nullptr) {
                    env->DeleteLocalRef(parameter_type);
                }
            }

            std::string return_descriptor;
            if (!signature_ok || !DescribeJavaClassObject(env, return_type, &return_descriptor)) {
                signature_ok = false;
            } else {
                signature += ")";
                signature += return_descriptor;
            }

            candidate_stream << (reflected_is_static ? "[static]" : "[instance]") << signature;
            debug_candidates.push_back(candidate_stream.str());

            env->DeleteLocalRef(parameter_types);
            env->DeleteLocalRef(return_type);
            env->DeleteLocalRef(method_object);
            if (!signature_ok) {
                continue;
            }
            if (reflected_is_static != is_static) {
                continue;
            }
            if (static_cast<size_t>(parameter_count) != expected_descriptors.size()) {
                continue;
            }

            bool ok = true;
            for (size_t parameter_index = 0; parameter_index < expected_descriptors.size(); ++parameter_index) {
                if (!JavaParameterDescriptorAcceptsArgumentWithAssignability(
                        reflected_parameter_descriptors[parameter_index],
                        expected_descriptors[parameter_index],
                        &ReflectiveJavaDescriptorIsAssignableFrom,
                        &assignability_context)) {
                    ok = false;
                    break;
                }
            }
            if (assignability_context.had_error) {
                env->DeleteLocalRef(class_class);
                env->DeleteLocalRef(method_class);
                env->DeleteLocalRef(modifier_class);
                env->DeleteLocalRef(methods);
                env->DeleteLocalRef(current_class);
                return false;
            }
            if (!ok) {
                continue;
            }

            matched_methods.push_back({signature, reflected_parameter_descriptors});
        }

        env->DeleteLocalRef(methods);
        if (!matched_methods.empty()) {
            env->DeleteLocalRef(current_class);
            break;
        }
        jclass next_class =
            reinterpret_cast<jclass>(env->CallObjectMethod(current_class, get_superclass));
        env->DeleteLocalRef(current_class);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(class_class);
            env->DeleteLocalRef(method_class);
            env->DeleteLocalRef(modifier_class);
            SetError(error_message, "ResolveJavaMethodSignature getSuperclass failed");
            return false;
        }
        current_class = next_class;
    }

    env->DeleteLocalRef(class_class);
    env->DeleteLocalRef(method_class);
    env->DeleteLocalRef(modifier_class);
    if (assignability_context.had_error) {
        return false;
    }

    if (matched_methods.empty()) {
        const std::string debug_message = BuildResolveSignatureDebugMessage(class_name,
                                                                            method_name,
                                                                            argument_type_names,
                                                                            is_static,
                                                                            debug_candidates);
        SetError(error_message, debug_message.c_str());
        return false;
    }

    size_t matched_index = 0u;
    if (matched_methods.size() > 1u) {
        std::vector<std::vector<std::string>> matched_parameter_descriptors;
        matched_parameter_descriptors.reserve(matched_methods.size());
        for (const MatchedMethodCandidate& candidate : matched_methods) {
            matched_parameter_descriptors.push_back(candidate.parameter_descriptors);
        }

        const JavaOverloadMatchResultForTesting result = ResolveMostSpecificJavaOverload(
            matched_parameter_descriptors,
            expected_descriptors,
            &ReflectiveJavaDescriptorIsAssignableFrom,
            &assignability_context,
            &matched_index);
        if (assignability_context.had_error) {
            return false;
        }
        if (result != JavaOverloadMatchResultForTesting::kUniqueMatch) {
            SetError(error_message,
                     result == JavaOverloadMatchResultForTesting::kNoMatch
                         ? BuildResolveSignatureDebugMessage(class_name,
                                                            method_name,
                                                            argument_type_names,
                                                            is_static,
                                                            debug_candidates)
                               .c_str()
                         : "ResolveJavaMethodSignature ambiguous match");
            return false;
        }
    }

    if (matched_index >= matched_methods.size()) {
        if (matched_methods.empty()) {
            const std::string debug_message = BuildResolveSignatureDebugMessage(class_name,
                                                                                method_name,
                                                                                argument_type_names,
                                                                                is_static,
                                                                                debug_candidates);
            SetError(error_message, debug_message.c_str());
        }
        return false;
    }

    *resolved_signature = matched_methods[matched_index].signature;
    return true;
}

bool DefaultCallOriginalJavaJsHook(const JavaJsHookRecord& record,
                                   const JavaJsValue* args,
                                   std::size_t arg_count,
                                   JavaJsValue* result,
                                   std::string* error_message) {
    if (result == nullptr) {
        SetError(error_message, "java original result output is required");
        return false;
    }

    ActiveJavaJsInvocation active_invocation = {};
    if (!TryGetActiveJavaJsInvocation(&active_invocation) ||
        !active_invocation.active ||
        active_invocation.hook_id != record.hook_id) {
        SetError(error_message, "java callOriginal must run during the matching hook callback");
        return false;
    }
    if (active_invocation.installed_hook_id < 0) {
        SetError(error_message, "java installed hook id is not available yet");
        return false;
    }
    ActiveJavaJsInvocation invocation = active_invocation;
    std::string effective_signature = record.signature;
    if (effective_signature == "*" && invocation.installed_hook_id >= 0) {
        std::string resolved_signature;
        if (nook::java_hook_internal::ResolveInstalledHookSignature(invocation.installed_hook_id,
                                                                    &resolved_signature) &&
            !resolved_signature.empty()) {
            effective_signature = resolved_signature;
        }
    }
    NOOK_JAVA_JS_LOGI(
        "callOriginal enter hook=%u installed=%d class=%s method=%s sig=%s argc=%zu active_sig=%s",
        record.hook_id,
        invocation.installed_hook_id,
        record.class_name.c_str(),
        record.method_name.c_str(),
        effective_signature.c_str(),
        arg_count,
        effective_signature.c_str());
    if (arg_count != invocation.signature.arg_descriptors.size()) {
        SetError(error_message, "java callOriginal arg count mismatch");
        NOOK_JAVA_JS_LOGE("callOriginal arg mismatch hook=%u expected=%zu actual=%zu sig=%s",
                          record.hook_id,
                          invocation.signature.arg_descriptors.size(),
                          arg_count,
                          effective_signature.c_str());
        return false;
    }

    ScopedJavaLocalRefs local_refs = {};
    local_refs.env = invocation.env;
    std::vector<NookJavaHookValue> raw_args(arg_count);
    for (std::size_t index = 0; index < arg_count; ++index) {
        if (!ConvertJavaJsValueToNookJavaHookValue(invocation.env,
                                                   invocation.signature.arg_descriptors[index],
                                                   args[index],
                                                   &local_refs,
                                                   &raw_args[index],
                                                   error_message)) {
            return false;
        }
    }

    NookJavaHookValue raw_result = {};
    if (!nook::java_hook_internal::CallOriginalNow(invocation.installed_hook_id,
                                                   invocation.env,
                                                   invocation.thiz,
                                                   raw_args.data(),
                                                   raw_args.size(),
                                                   &raw_result)) {
        NOOK_JAVA_JS_LOGE("callOriginal invoke failed hook=%u installed=%d class=%s method=%s sig=%s",
                          record.hook_id,
                          invocation.installed_hook_id,
                          record.class_name.c_str(),
                          record.method_name.c_str(),
                          effective_signature.c_str());
        SetError(error_message, "java callOriginal invoke failed");
        return false;
    }

    NOOK_JAVA_JS_LOGI("callOriginal ok hook=%u installed=%d class=%s method=%s sig=%s",
                      record.hook_id,
                      invocation.installed_hook_id,
                      record.class_name.c_str(),
                      record.method_name.c_str(),
                      effective_signature.c_str());

    return ConvertNookJavaHookValueToJavaJsValue(invocation.env,
                                                 invocation.signature.return_descriptor,
                                                 raw_result,
                                                 result,
                                                 error_message);
}

bool DefaultInstallJavaJsHook(const JavaJsHookRequest& request,
                              JavaJsHookRecord* out_record,
                              std::string* error_message);

int HandleJavaJsHookInvocation(uint32_t slot,
                               JNIEnv* env,
                               jobject thiz,
                               NookJavaHookValue* args,
                               size_t arg_count,
                               NookJavaHookValue* result) {
    JavaJsHookRecord record = {};
    if (!LookupJavaJsHookRecordBySlot(slot, &record)) {
        return 1;
    }

    int installed_hook_id = record.installed_hook_id;
    if (installed_hook_id >= 0x40000000) {
        int resolved_hook_id = -1;
        if (nook::java_hook_internal::ResolveInstalledHookId(installed_hook_id, &resolved_hook_id)) {
            installed_hook_id = resolved_hook_id;
        }
    }

    std::string effective_signature = record.signature;
    if (effective_signature == "*" && installed_hook_id >= 0) {
        std::string resolved_signature;
        if (nook::java_hook_internal::ResolveInstalledHookSignature(installed_hook_id,
                                                                    &resolved_signature) &&
            !resolved_signature.empty()) {
            effective_signature = resolved_signature;
        }
    }

    ParsedJavaMethodSignature signature = {};
    std::string error_message;
    if (!ParseMethodSignature(effective_signature, &signature, &error_message)) {
        return 1;
    }
    if (arg_count != signature.arg_descriptors.size()) {
        return 1;
    }

    std::vector<JavaJsValue> js_args(arg_count);
    for (size_t index = 0; index < arg_count; ++index) {
        if (!ConvertNookJavaHookValueToJavaJsValue(env,
                                                   signature.arg_descriptors[index],
                                                   args[index],
                                                   &js_args[index],
                                                   &error_message)) {
            return 1;
        }
    }

    NOOK_JAVA_JS_LOGI("hook callback enter slot=%u hook=%u installed_raw=%d installed_resolved=%d class=%s method=%s sig=%s argc=%zu",
                      slot,
                      record.hook_id,
                      record.installed_hook_id,
                      installed_hook_id,
                      record.class_name.c_str(),
                      record.method_name.c_str(),
                      effective_signature.c_str(),
                      arg_count);

    ActiveJavaJsInvocation invocation = {};
    invocation.active = true;
    invocation.hook_id = record.hook_id;
    invocation.installed_hook_id = installed_hook_id;
    invocation.env = env;
    invocation.thiz = thiz;
    invocation.signature = signature;
    PushActiveJavaJsInvocationForCurrentThread(invocation);

    JavaJsValue js_result = {};
    const bool dispatched = DispatchJavaJsHookInvocation(record.hook_id,
                                                         reinterpret_cast<uint64_t>(thiz),
                                                         js_args.data(),
                                                         js_args.size(),
                                                         &js_result,
                                                         &error_message);

    PopActiveJavaJsInvocationForCurrentThread();

    if (!dispatched) {
        NOOK_JAVA_JS_LOGE("hook callback dispatch failed slot=%u hook=%u class=%s method=%s sig=%s",
                          slot,
                          record.hook_id,
                          record.class_name.c_str(),
                          record.method_name.c_str(),
                          record.signature.c_str());
        return 1;
    }

    NOOK_JAVA_JS_LOGI("hook callback dispatched slot=%u hook=%u class=%s method=%s sig=%s",
                      slot,
                      record.hook_id,
                      record.class_name.c_str(),
                      record.method_name.c_str(),
                      record.signature.c_str());

    return ConvertJavaJsValueToNookJavaHookValue(env,
                                                 signature.return_descriptor,
                                                 js_result,
                                                 nullptr,
                                                 result,
                                                 &error_message)
        ? 0
        : 1;
}

int JavaJsHookSlotThunk0(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(0u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk1(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(1u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk2(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(2u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk3(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(3u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk4(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(4u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk5(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(5u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk6(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(6u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk7(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(7u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk8(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(8u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk9(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(9u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk10(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(10u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk11(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(11u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk12(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(12u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk13(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(13u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk14(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(14u, env, thiz, args, arg_count, result);
}
int JavaJsHookSlotThunk15(JNIEnv* env, jobject thiz, NookJavaHookValue* args, size_t arg_count, NookJavaHookValue* result) {
    return HandleJavaJsHookInvocation(15u, env, thiz, args, arg_count, result);
}

constexpr NookJavaHookCallback kJavaJsHookSlotCallbacks[kMaxJavaJsHookSlots] = {
    &JavaJsHookSlotThunk0,
    &JavaJsHookSlotThunk1,
    &JavaJsHookSlotThunk2,
    &JavaJsHookSlotThunk3,
    &JavaJsHookSlotThunk4,
    &JavaJsHookSlotThunk5,
    &JavaJsHookSlotThunk6,
    &JavaJsHookSlotThunk7,
    &JavaJsHookSlotThunk8,
    &JavaJsHookSlotThunk9,
    &JavaJsHookSlotThunk10,
    &JavaJsHookSlotThunk11,
    &JavaJsHookSlotThunk12,
    &JavaJsHookSlotThunk13,
    &JavaJsHookSlotThunk14,
    &JavaJsHookSlotThunk15,
};

bool DefaultInstallJavaJsHook(const JavaJsHookRequest& request,
                              JavaJsHookRecord* out_record,
                              std::string* error_message) {
    if (out_record == nullptr) {
        SetError(error_message, "java hook record output is required");
        return false;
    }

    JavaJsHookRequest resolved_request = request;
    if (resolved_request.signature == "*" && !resolved_request.deferred) {
        std::string resolved_signature;
        if (!ResolveWildcardJavaMethodSignature(resolved_request, &resolved_signature, error_message)) {
            return false;
        }
        resolved_request.signature = resolved_signature;
    }

    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    uint32_t slot = kInvalidJavaJsHookSlot;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        slot = AllocateJavaJsHookSlot(state);
        if (slot == kInvalidJavaJsHookSlot) {
            SetError(error_message, "no free Java JS hook callback slots");
            return false;
        }
        state.hook_id_by_slot[slot] = resolved_request.hook_id;
    }

    const jobject loader =
        resolved_request.loader_handle == 0u
            ? nullptr
            : reinterpret_cast<jobject>(resolved_request.loader_handle);
    const int installed_hook_id = NookJavaHookHookDeferredWithLoader(
        nullptr,
        loader,
        resolved_request.class_name.c_str(),
        resolved_request.method_name.c_str(),
        resolved_request.signature.c_str(),
        resolved_request.is_static ? 1 : 0,
        kJavaJsHookSlotCallbacks[slot]);
    NOOK_JAVA_JS_LOGI("install hook request hook=%u slot=%u class=%s method=%s sig=%s static=%d loader=%p installed=%d",
                      resolved_request.hook_id,
                      slot,
                      resolved_request.class_name.c_str(),
                      resolved_request.method_name.c_str(),
                      resolved_request.signature.c_str(),
                      resolved_request.is_static ? 1 : 0,
                      loader,
                      installed_hook_id);
    if (installed_hook_id < 0) {
        std::lock_guard<std::mutex> lock(state.mutex);
        ReleaseJavaJsHookSlot(state, slot);
        SetError(error_message, "NookJavaHookHookDeferred failed");
        return false;
    }

    *out_record = MakeRecordFromRequest(resolved_request);
    out_record->installed_hook_id = installed_hook_id;
    out_record->callback_slot = slot;
    return true;
}

#endif  // defined(__ANDROID__)

#if !defined(__ANDROID__)
bool NormalizeJavaFieldValue(const std::string& descriptor,
                             const JavaJsValue& input,
                             JavaJsValue* output,
                             std::string* error_message) {
    if (output == nullptr) {
        SetError(error_message, "normalized Java field output is required");
        return false;
    }

    *output = {};
    if (descriptor == "Z") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kBoolean;
            output->bool_value = false;
            return true;
        }
        if (input.kind != JavaJsValueKind::kBoolean) {
            SetError(error_message, "Java boolean field expects JS boolean");
            return false;
        }
        output->kind = JavaJsValueKind::kBoolean;
        output->bool_value = input.bool_value;
        return true;
    }
    if (descriptor == "I") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kInt32;
            output->int_value = 0;
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble &&
            std::isfinite(input.double_value) &&
            input.double_value >= static_cast<double>(std::numeric_limits<int32_t>::min()) &&
            input.double_value <= static_cast<double>(std::numeric_limits<int32_t>::max()) &&
            std::floor(input.double_value) == input.double_value) {
            output->kind = JavaJsValueKind::kInt32;
            output->int_value = static_cast<int32_t>(input.double_value);
            return true;
        }
        SetError(error_message, "Java int field expects JS int32");
        return false;
    }
    if (descriptor == "J") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kInt64;
            output->int64_value = 0;
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt64) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            output->kind = JavaJsValueKind::kInt64;
            output->int64_value = input.int_value;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble &&
            std::isfinite(input.double_value) &&
            input.double_value >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
            input.double_value <= static_cast<double>(std::numeric_limits<int64_t>::max()) &&
            std::floor(input.double_value) == input.double_value) {
            output->kind = JavaJsValueKind::kInt64;
            output->int64_value = static_cast<int64_t>(input.double_value);
            return true;
        }
        SetError(error_message, "Java long field expects JS integer");
        return false;
    }
    if (descriptor == "F") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = 0.0f;
            return true;
        }
        if (input.kind == JavaJsValueKind::kFloat) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = static_cast<float>(input.double_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = static_cast<float>(input.int_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt64) {
            output->kind = JavaJsValueKind::kFloat;
            output->float_value = static_cast<float>(input.int64_value);
            return true;
        }
        SetError(error_message, "Java float field expects JS number");
        return false;
    }
    if (descriptor == "D") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = 0.0;
            return true;
        }
        if (input.kind == JavaJsValueKind::kDouble) {
            *output = input;
            return true;
        }
        if (input.kind == JavaJsValueKind::kFloat) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = static_cast<double>(input.float_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt32) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = static_cast<double>(input.int_value);
            return true;
        }
        if (input.kind == JavaJsValueKind::kInt64) {
            output->kind = JavaJsValueKind::kDouble;
            output->double_value = static_cast<double>(input.int64_value);
            return true;
        }
        SetError(error_message, "Java double field expects JS number");
        return false;
    }
    if (descriptor == "Ljava/lang/String;") {
        if (input.kind == JavaJsValueKind::kUndefined) {
            output->kind = JavaJsValueKind::kUndefined;
            return true;
        }
        if (input.kind != JavaJsValueKind::kString) {
            SetError(error_message, "Java String field expects JS string");
            return false;
        }
        *output = input;
        return true;
    }

    SetError(error_message, "unsupported Java field descriptor");
    return false;
}
#endif

#if !defined(__ANDROID__)
bool DescriptorToJavaArrayTypeName(const std::string& descriptor,
                                   std::string* type_name_out,
                                   std::string* error_message) {
    if (type_name_out == nullptr) {
        SetError(error_message, "java array type output is required");
        return false;
    }
    if (descriptor.empty() || descriptor.front() != '[') {
        SetError(error_message, "java array descriptor is invalid");
        return false;
    }

    const std::string component_descriptor = descriptor.substr(1u);
    std::string component_type_name;
    if (component_descriptor == "V") {
        component_type_name = "void";
    } else if (component_descriptor == "Z") {
        component_type_name = "boolean";
    } else if (component_descriptor == "B") {
        component_type_name = "byte";
    } else if (component_descriptor == "C") {
        component_type_name = "char";
    } else if (component_descriptor == "S") {
        component_type_name = "short";
    } else if (component_descriptor == "I") {
        component_type_name = "int";
    } else if (component_descriptor == "J") {
        component_type_name = "long";
    } else if (component_descriptor == "F") {
        component_type_name = "float";
    } else if (component_descriptor == "D") {
        component_type_name = "double";
    } else if (component_descriptor.size() >= 2u &&
               component_descriptor.front() == 'L' &&
               component_descriptor.back() == ';') {
        component_type_name = component_descriptor.substr(1u, component_descriptor.size() - 2u);
        std::replace(component_type_name.begin(), component_type_name.end(), '/', '.');
    } else if (!component_descriptor.empty() && component_descriptor.front() == '[') {
        component_type_name = component_descriptor;
        std::replace(component_type_name.begin(), component_type_name.end(), '/', '.');
    } else {
        SetError(error_message, "unsupported Java class descriptor");
        return false;
    }

    *type_name_out = component_type_name + "[]";
    return true;
}
#endif

}  // namespace

bool InstallJavaJsHook(const JavaJsHookRequest& request,
                       const JavaJsHookInstallerDependencies& dependencies,
                       JavaJsHookRecord* out_record,
                       std::string* error_message) {
    if (!IsValidRequest(request, error_message)) {
        return false;
    }

    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();

    JavaJsHookRequest resolved_request = request;
    if (resolved_request.method_name == "$init") {
        resolved_request.method_name = "<init>";
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        resolved_request.hook_id = state.next_hook_id++;
    }

    JavaJsHookRecord resolved_record = MakeRecordFromRequest(resolved_request);
    if (dependencies.install_hook != nullptr) {
        if (!dependencies.install_hook(resolved_request, &resolved_record, error_message)) {
            return false;
        }
    }
#if defined(__ANDROID__)
    else {
        if (!DefaultInstallJavaJsHook(resolved_request, &resolved_record, error_message)) {
            return false;
        }
    }
#endif

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.records[resolved_record.hook_id] = resolved_record;
    }

    if (out_record != nullptr) {
        *out_record = resolved_record;
    }
    return true;
}

bool UninstallJavaJsHook(uint32_t hook_id, std::string* error_message) {
    if (hook_id == 0u) {
        SetError(error_message, "java hook id must be non-zero");
        return false;
    }

    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    JavaJsHookRecord record = {};
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.records.find(hook_id);
        if (found == state.records.end()) {
            SetError(error_message, "java hook id not found");
            return false;
        }
        record = found->second;
        state.records.erase(found);
#if defined(__ANDROID__)
        if (record.callback_slot != kInvalidJavaJsHookSlot) {
            ReleaseJavaJsHookSlot(state, record.callback_slot);
        }
#endif
    }

#if defined(__ANDROID__)
    if (record.callback_slot != kInvalidJavaJsHookSlot) {
        if (NookJavaHookUnhook(record.installed_hook_id) != NOOK_STATUS_OK) {
            SetError(error_message, "NookJavaHookUnhook failed");
            return false;
        }
    }
#else
    (void)record;
#endif
    return true;
}

bool CallOriginalJavaJsHook(uint32_t hook_id,
                            const JavaJsValue* args,
                            std::size_t arg_count,
                            const JavaJsHookInstallerDependencies& dependencies,
                            JavaJsValue* result,
                            std::string* error_message) {
    if (hook_id == 0u) {
        SetError(error_message, "java hook id must be non-zero");
        return false;
    }
    if (result == nullptr) {
        SetError(error_message, "java original result output is required");
        return false;
    }

    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    JavaJsHookRecord record = {};
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.records.find(hook_id);
        if (found == state.records.end()) {
            SetError(error_message, "java hook id not found");
            return false;
        }
        record = found->second;
    }

    if (dependencies.call_original_hook != nullptr) {
        return dependencies.call_original_hook(record, args, arg_count, result, error_message);
    }
#if defined(__ANDROID__)
    return DefaultCallOriginalJavaJsHook(record, args, arg_count, result, error_message);
#else
    (void)args;
    (void)arg_count;
    SetError(error_message, "java original call dependency is not configured");
    return false;
#endif
}

bool ResolveJavaMethodSignature(const std::string& class_name,
                                const std::string& method_name,
                                const std::vector<std::string>& argument_type_names,
                                uint64_t loader_handle,
                                bool is_static,
                                const JavaJsHookInstallerDependencies& dependencies,
                                std::string* signature,
                                std::string* error_message) {
    if (signature == nullptr) {
        SetError(error_message, "resolved Java signature output is required");
        return false;
    }
    if (dependencies.resolve_signature != nullptr) {
        return dependencies.resolve_signature(
            class_name,
            method_name,
            argument_type_names,
            loader_handle,
            is_static,
            signature,
            error_message);
    }
#if defined(__ANDROID__)
    return ResolveJavaMethodSignatureByTypeNames(
        class_name,
        method_name,
        argument_type_names,
        loader_handle,
        is_static,
        signature,
        error_message);
#else
    SetError(error_message, "java overload resolver is not configured");
    return false;
#endif
}

bool ResolveJavaField(const std::string& class_name,
                      const std::string& field_name,
                      uint64_t loader_handle,
                      bool is_static,
                      const JavaJsHookInstallerDependencies& dependencies,
                      JavaJsFieldRecord* out_record,
                      std::string* error_message) {
    if (out_record == nullptr) {
        SetError(error_message, "resolved Java field output is required");
        return false;
    }
    if (dependencies.resolve_field != nullptr) {
        return dependencies.resolve_field(
            class_name,
            field_name,
            loader_handle,
            is_static,
            out_record,
            error_message);
    }
#if defined(__ANDROID__)
    return ResolveJavaFieldByName(
        class_name, field_name, loader_handle, is_static, out_record, error_message);
#else
    SetError(error_message, "java field resolver is not configured");
    return false;
#endif
}

bool InvokeJavaMethod(const JavaJsMethodRecord& record,
                      uint64_t receiver_handle,
                      const JavaJsHookInstallerDependencies& dependencies,
                      const JavaJsValue* args,
                      std::size_t arg_count,
                      JavaJsValue* result,
                      std::string* error_message) {
    if (result == nullptr) {
        SetError(error_message, "java invoke result output is required");
        return false;
    }
    if (dependencies.invoke_method != nullptr) {
        return dependencies.invoke_method(
            record, receiver_handle, args, arg_count, result, error_message);
    }
#if defined(__ANDROID__)
    return DefaultInvokeJavaMethod(record, receiver_handle, args, arg_count, result, error_message);
#else
    (void)record;
    (void)receiver_handle;
    (void)args;
    (void)arg_count;
    SetError(error_message, "java method invoker is not configured");
    return false;
#endif
}

bool ReadJavaField(const JavaJsFieldRecord& record,
                   uint64_t receiver_handle,
                   const JavaJsHookInstallerDependencies& dependencies,
                   JavaJsValue* result,
                   std::string* error_message) {
    if (dependencies.read_field != nullptr) {
        return dependencies.read_field(record, receiver_handle, result, error_message);
    }
#if defined(__ANDROID__)
    return DefaultReadJavaField(record, receiver_handle, result, error_message);
#else
    (void)record;
    (void)receiver_handle;
    SetError(error_message, "java field reader is not configured");
    return false;
#endif
}

bool WriteJavaField(const JavaJsFieldRecord& record,
                    uint64_t receiver_handle,
                    const JavaJsHookInstallerDependencies& dependencies,
                    const JavaJsValue& value,
                    std::string* error_message) {
    JavaJsValue normalized_value = {};
    if (!NormalizeJavaFieldValue(record.signature, value, &normalized_value, error_message)) {
        return false;
    }

    if (dependencies.write_field != nullptr) {
        return dependencies.write_field(record, receiver_handle, normalized_value, error_message);
    }
#if defined(__ANDROID__)
    return DefaultWriteJavaField(record, receiver_handle, normalized_value, error_message);
#else
    (void)record;
    (void)receiver_handle;
    SetError(error_message, "java field writer is not configured");
    return false;
#endif
}

bool RetainJavaObject(uint64_t object_handle,
                      const JavaJsHookInstallerDependencies& dependencies,
                      uint64_t* retained_handle,
                      std::string* error_message) {
    if (retained_handle == nullptr) {
        SetError(error_message, "retained Java object output is required");
        return false;
    }
    if (dependencies.retain_object != nullptr) {
        return dependencies.retain_object(object_handle, retained_handle, error_message);
    }
#if defined(__ANDROID__)
    return DefaultRetainJavaObject(object_handle, retained_handle, error_message);
#else
    (void)object_handle;
    SetError(error_message, "java object retainer is not configured");
    return false;
#endif
}

bool ReleaseJavaObject(uint64_t object_handle,
                       const JavaJsHookInstallerDependencies& dependencies,
                       std::string* error_message) {
    if (dependencies.release_object != nullptr) {
        return dependencies.release_object(object_handle, error_message);
    }
#if defined(__ANDROID__)
    return DefaultReleaseJavaObject(object_handle, error_message);
#else
    (void)object_handle;
    SetError(error_message, "java object releaser is not configured");
    return false;
#endif
}

bool EnumerateJavaObjects(const std::string& class_name,
                          uint64_t loader_handle,
                          const JavaJsHookInstallerDependencies& dependencies,
                          std::vector<JavaJsValue>* matches,
                          std::string* error_message) {
    if (matches == nullptr) {
        SetError(error_message, "java choose matches output is required");
        return false;
    }
    if (dependencies.enumerate_objects != nullptr) {
        return dependencies.enumerate_objects(class_name, loader_handle, matches, error_message);
    }
#if defined(__ANDROID__)
    return DefaultEnumerateJavaObjects(class_name, loader_handle, matches, error_message);
#else
    (void)class_name;
    (void)loader_handle;
    SetError(error_message, "java object enumerator is not configured");
    return false;
#endif
}

bool EnumerateLoadedJavaClasses(const JavaJsHookInstallerDependencies& dependencies,
                                std::vector<std::string>* class_names,
                                std::string* error_message) {
    if (class_names == nullptr) {
        SetError(error_message, "java loaded classes output is required");
        return false;
    }
    if (dependencies.enumerate_loaded_classes != nullptr) {
        return dependencies.enumerate_loaded_classes(class_names, error_message);
    }
#if defined(__ANDROID__)
    return DefaultEnumerateLoadedJavaClasses(class_names, error_message);
#else
    SetError(error_message, "java loaded-class enumerator is not configured");
    return false;
#endif
}

bool EnumerateJavaClassLoaders(const JavaJsHookInstallerDependencies& dependencies,
                               std::vector<JavaJsValue>* matches,
                               std::string* error_message) {
    if (matches == nullptr) {
        SetError(error_message, "java class-loader matches output is required");
        return false;
    }
    if (dependencies.enumerate_class_loaders != nullptr) {
        return dependencies.enumerate_class_loaders(matches, error_message);
    }
#if defined(__ANDROID__)
    return DefaultEnumerateJavaClassLoaders(matches, error_message);
#else
    SetError(error_message, "java class-loader enumerator is not configured");
    return false;
#endif
}

bool RegisterJavaClass(const JavaJsRegisterClassRequest& request,
                       const JavaJsHookInstallerDependencies& dependencies,
                       JavaJsValue* result,
                       std::string* error_message) {
    if (result == nullptr) {
        SetError(error_message, "java registerClass result output is required");
        return false;
    }
    if (request.class_name.empty()) {
        SetError(error_message, "java registerClass class name is required");
        return false;
    }
    if (request.interface_class_names.empty()) {
        SetError(error_message, "java registerClass requires at least one interface");
        return false;
    }
    if (request.methods.empty()) {
        SetError(error_message, "java registerClass requires at least one method");
        return false;
    }
    if (dependencies.register_class != nullptr) {
        return dependencies.register_class(request, result, error_message);
    }
#if defined(__ANDROID__)
    JavaEnv jenv;
    if (jenv.isNull()) {
        SetError(error_message, "JNIEnv is null while creating Java registerClass proxy");
        return false;
    }

    JNIEnv* env = jenv.get();
    ScopedJavaLocalRefs local_refs = {};
    local_refs.env = env;

    jobject helper_loader = nullptr;
    jclass helper_class = nullptr;
    if (!EnsureRegisterClassHelperReady(env, &helper_loader, &helper_class, error_message)) {
        return false;
    }
    local_refs.refs.push_back(helper_loader);
    local_refs.refs.push_back(helper_class);

    jmethodID helper_ctor = env->GetMethodID(helper_class, "<init>", "(J)V");
    if (helper_ctor == nullptr) {
        ClearJniException(env);
        SetError(error_message, "registerClass helper constructor is unavailable");
        return false;
    }

    jobject helper_instance =
        env->NewObject(helper_class, helper_ctor, static_cast<jlong>(request.callback_id));
    if (helper_instance == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "registerClass helper instance creation failed");
        return false;
    }
    local_refs.refs.push_back(helper_instance);

    jclass class_class = env->FindClass("java/lang/Class");
    if (class_class == nullptr) {
        ClearJniException(env);
        SetError(error_message, "java.lang.Class is unavailable");
        return false;
    }
    local_refs.refs.push_back(class_class);

    jobject proxy_loader =
        request.loader_handle == 0u
            ? nullptr
            : env->NewLocalRef(reinterpret_cast<jobject>(request.loader_handle));
    if (request.loader_handle != 0u && (proxy_loader == nullptr || env->ExceptionCheck())) {
        ClearJniException(env);
        SetError(error_message, "registerClass loader handle is invalid");
        return false;
    }
    if (proxy_loader != nullptr) {
        local_refs.refs.push_back(proxy_loader);
    }

    jobjectArray interface_array = env->NewObjectArray(
        static_cast<jsize>(request.interface_class_names.size()), class_class, nullptr);
    if (interface_array == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "registerClass interface array allocation failed");
        return false;
    }
    local_refs.refs.push_back(interface_array);

    jmethodID class_get_loader =
        env->GetMethodID(class_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (class_get_loader == nullptr) {
        ClearJniException(env);
        SetError(error_message, "Class.getClassLoader is unavailable");
        return false;
    }

    for (size_t index = 0; index < request.interface_class_names.size(); ++index) {
        jclass interface_class = ResolveJavaClass(
            env, request.interface_class_names[index], request.loader_handle, error_message);
        if (interface_class == nullptr) {
            return false;
        }
        local_refs.refs.push_back(interface_class);

        env->SetObjectArrayElement(interface_array, static_cast<jsize>(index), interface_class);
        if (env->ExceptionCheck()) {
            ClearJniException(env);
            SetError(error_message, "registerClass interface array population failed");
            return false;
        }

        if (proxy_loader == nullptr) {
            jobject interface_loader = env->CallObjectMethod(interface_class, class_get_loader);
            if (env->ExceptionCheck()) {
                ClearJniException(env);
                if (interface_loader != nullptr) {
                    env->DeleteLocalRef(interface_loader);
                }
                SetError(error_message, "registerClass interface loader lookup failed");
                return false;
            }
            if (interface_loader != nullptr) {
                proxy_loader = interface_loader;
                local_refs.refs.push_back(proxy_loader);
            }
        }
    }

    if (proxy_loader == nullptr) {
        proxy_loader = helper_loader;
    }

    jclass proxy_class = env->FindClass("java/lang/reflect/Proxy");
    if (proxy_class == nullptr) {
        ClearJniException(env);
        SetError(error_message, "java.lang.reflect.Proxy is unavailable");
        return false;
    }
    local_refs.refs.push_back(proxy_class);

    jmethodID new_proxy_instance = env->GetStaticMethodID(
        proxy_class,
        "newProxyInstance",
        "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;");
    if (new_proxy_instance == nullptr) {
        ClearJniException(env);
        SetError(error_message, "Proxy.newProxyInstance is unavailable");
        return false;
    }

    jobject proxy = env->CallStaticObjectMethod(
        proxy_class, new_proxy_instance, proxy_loader, interface_array, helper_instance);
    if (proxy == nullptr || env->ExceptionCheck()) {
        ClearJniException(env);
        SetError(error_message, "Proxy.newProxyInstance failed");
        return false;
    }

    jobject retained_proxy = env->NewGlobalRef(proxy);
    if (retained_proxy == nullptr || env->ExceptionCheck()) {
        env->DeleteLocalRef(proxy);
        ClearJniException(env);
        SetError(error_message, "registerClass proxy retain failed");
        return false;
    }

    result->kind = JavaJsValueKind::kObject;
    result->object_handle = reinterpret_cast<uint64_t>(retained_proxy);
    result->object_handle_is_global = true;
    if (!DescribeJavaObject(env, proxy, &result->object_class_name, error_message)) {
        env->DeleteGlobalRef(retained_proxy);
        env->DeleteLocalRef(proxy);
        return false;
    }
    env->DeleteLocalRef(proxy);
    return true;
#else
    SetError(error_message, "java registerClass bridge is not configured");
    return false;
#endif
}

bool DispatchJavaJsHookInvocationForTesting(uint32_t hook_id,
                                            const JavaJsValue* args,
                                            std::size_t arg_count,
                                            JavaJsValue* result,
                                            std::string* error_message) {
    return DispatchJavaJsHookInvocationForTesting(
        hook_id, 0u, args, arg_count, result, error_message);
}

bool DispatchJavaJsHookInvocationForTesting(uint32_t hook_id,
                                            uint64_t receiver_handle,
                                            const JavaJsValue* args,
                                            std::size_t arg_count,
                                            JavaJsValue* result,
                                            std::string* error_message) {
    return DispatchJavaJsHookInvocation(
        hook_id, receiver_handle, args, arg_count, result, error_message);
}

void SetJavaJsHookInvocationDispatcher(JavaJsHookInvocationDispatcher dispatcher) {
    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.dispatcher = dispatcher;
}

void ResetJavaJsHookInvocationDispatcher() {
    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.dispatcher = nullptr;
}

std::size_t GetInstalledJavaJsHookCountForTesting() {
    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.records.size();
}

bool GetJavaJsHookRecordForTesting(uint32_t hook_id, JavaJsHookRecord* out_record) {
    if (out_record == nullptr) {
        return false;
    }

    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto found = state.records.find(hook_id);
    if (found == state.records.end()) {
        return false;
    }
    *out_record = found->second;
    return true;
}

void ResetJavaJsHookRegistryForTesting() {
    JavaJsHookRegistryState& state = GetJavaJsHookRegistryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.next_hook_id = 1u;
    state.records.clear();
#if defined(__ANDROID__)
    state.slot_used.fill(false);
    state.hook_id_by_slot.fill(0u);
#endif
}

bool JavaParameterDescriptorAcceptsArgumentForTesting(const std::string& parameter_descriptor,
                                                      const std::string& argument_descriptor) {
    if (parameter_descriptor == argument_descriptor) {
        return true;
    }
    if (parameter_descriptor.empty() || argument_descriptor.empty()) {
        return false;
    }
    if (argument_descriptor == "__nook_null__") {
        return parameter_descriptor.front() == 'L' || parameter_descriptor.front() == '[';
    }
    if (parameter_descriptor == "Ljava/lang/Object;" &&
        (argument_descriptor.front() == 'L' || argument_descriptor.front() == '[')) {
        return true;
    }
    if (parameter_descriptor.front() != '[' || argument_descriptor.front() != '[') {
        return false;
    }

    const std::string parameter_component = parameter_descriptor.substr(1u);
    const std::string argument_component = argument_descriptor.substr(1u);
    if (parameter_component == argument_component) {
        return true;
    }
    if (parameter_component == "Ljava/lang/Object;" &&
        !argument_component.empty() &&
        (argument_component.front() == 'L' || argument_component.front() == '[')) {
        return true;
    }
    if (!parameter_component.empty() &&
        !argument_component.empty() &&
        parameter_component.front() == '[' &&
        argument_component.front() == '[') {
        return JavaParameterDescriptorAcceptsArgumentForTesting(parameter_component,
                                                                argument_component);
    }
    return false;
}

JavaMethodSpecificityComparisonForTesting CompareJavaMethodSpecificityForTesting(
    const std::vector<std::string>& left_parameter_descriptors,
    const std::vector<std::string>& right_parameter_descriptors,
    JavaDescriptorIsAssignableFromForTesting is_assignable_from,
    void* opaque) {
    const auto is_reference_like = [](const std::string& descriptor) {
        return !descriptor.empty() && (descriptor.front() == 'L' || descriptor.front() == '[');
    };

    if (left_parameter_descriptors.size() != right_parameter_descriptors.size()) {
        return JavaMethodSpecificityComparisonForTesting::kIncomparable;
    }

    bool left_more_specific = false;
    bool right_more_specific = false;
    for (size_t index = 0; index < left_parameter_descriptors.size(); ++index) {
        const std::string& left = left_parameter_descriptors[index];
        const std::string& right = right_parameter_descriptors[index];
        if (left == right) {
            continue;
        }
        if (!is_reference_like(left) || !is_reference_like(right) || is_assignable_from == nullptr) {
            return JavaMethodSpecificityComparisonForTesting::kIncomparable;
        }

        const bool left_assignable_to_right = is_assignable_from(right, left, opaque);
        const bool right_assignable_to_left = is_assignable_from(left, right, opaque);
        if (left_assignable_to_right && !right_assignable_to_left) {
            left_more_specific = true;
            if (right_more_specific) {
                return JavaMethodSpecificityComparisonForTesting::kIncomparable;
            }
            continue;
        }
        if (right_assignable_to_left && !left_assignable_to_right) {
            right_more_specific = true;
            if (left_more_specific) {
                return JavaMethodSpecificityComparisonForTesting::kIncomparable;
            }
            continue;
        }
        return JavaMethodSpecificityComparisonForTesting::kIncomparable;
    }

    if (left_more_specific == right_more_specific) {
        return JavaMethodSpecificityComparisonForTesting::kIncomparable;
    }
    return left_more_specific
               ? JavaMethodSpecificityComparisonForTesting::kLeftMoreSpecific
               : JavaMethodSpecificityComparisonForTesting::kRightMoreSpecific;
}

JavaOverloadMatchResultForTesting ResolveMostSpecificJavaOverloadForTesting(
    const std::vector<std::vector<std::string>>& candidate_parameter_descriptors,
    const std::vector<std::string>& argument_descriptors,
    JavaDescriptorIsAssignableFromForTesting is_assignable_from,
    void* opaque,
    size_t* matched_index) {
    const auto accepts_argument = [&](const std::string& parameter_descriptor,
                                      const std::string& argument_descriptor) {
        if (parameter_descriptor == argument_descriptor) {
            return true;
        }
        if (parameter_descriptor.empty() || argument_descriptor.empty()) {
            return false;
        }
        if (argument_descriptor == "__nook_null__") {
            return parameter_descriptor.front() == 'L' || parameter_descriptor.front() == '[';
        }
        if ((parameter_descriptor.front() == 'L' || parameter_descriptor.front() == '[') &&
            (argument_descriptor.front() == 'L' || argument_descriptor.front() == '[') &&
            is_assignable_from != nullptr &&
            is_assignable_from(parameter_descriptor, argument_descriptor, opaque)) {
            return true;
        }
        return JavaParameterDescriptorAcceptsArgumentForTesting(parameter_descriptor, argument_descriptor);
    };

    if (matched_index != nullptr) {
        *matched_index = 0u;
    }

    std::vector<size_t> matching_indices;
    matching_indices.reserve(candidate_parameter_descriptors.size());
    for (size_t candidate_index = 0; candidate_index < candidate_parameter_descriptors.size();
         ++candidate_index) {
        const auto& parameters = candidate_parameter_descriptors[candidate_index];
        if (parameters.size() != argument_descriptors.size()) {
            continue;
        }

        bool matched = true;
        for (size_t argument_index = 0; argument_index < argument_descriptors.size(); ++argument_index) {
            if (!accepts_argument(parameters[argument_index], argument_descriptors[argument_index])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            matching_indices.push_back(candidate_index);
        }
    }

    if (matching_indices.empty()) {
        return JavaOverloadMatchResultForTesting::kNoMatch;
    }
    if (matching_indices.size() == 1u) {
        if (matched_index != nullptr) {
            *matched_index = matching_indices[0];
        }
        return JavaOverloadMatchResultForTesting::kUniqueMatch;
    }

    size_t best_index = 0u;
    bool found_best = false;
    for (size_t candidate_position = 0; candidate_position < matching_indices.size(); ++candidate_position) {
        const size_t candidate_index = matching_indices[candidate_position];
        bool better_than_all = true;
        for (size_t other_position = 0; other_position < matching_indices.size(); ++other_position) {
            if (candidate_position == other_position) {
                continue;
            }
            const size_t other_index = matching_indices[other_position];
            const auto comparison = CompareJavaMethodSpecificityForTesting(
                candidate_parameter_descriptors[candidate_index],
                candidate_parameter_descriptors[other_index],
                is_assignable_from,
                opaque);
            if (comparison != JavaMethodSpecificityComparisonForTesting::kLeftMoreSpecific) {
                better_than_all = false;
                break;
            }
        }
        if (!better_than_all) {
            continue;
        }
        if (found_best) {
            return JavaOverloadMatchResultForTesting::kAmbiguous;
        }
        found_best = true;
        best_index = candidate_index;
    }

    if (!found_best) {
        return JavaOverloadMatchResultForTesting::kAmbiguous;
    }
    if (matched_index != nullptr) {
        *matched_index = best_index;
    }
    return JavaOverloadMatchResultForTesting::kUniqueMatch;
}

bool NormalizeJavaDescriptorForClassLookupForTesting(const std::string& descriptor,
                                                     std::string* class_name_out) {
    if (class_name_out == nullptr) {
        return false;
    }
    if (descriptor.size() >= 2u && descriptor.front() == 'L' && descriptor.back() == ';') {
        *class_name_out = descriptor.substr(1u, descriptor.size() - 2u);
        std::replace(class_name_out->begin(), class_name_out->end(), '/', '.');
        return true;
    }
    *class_name_out = descriptor;
    if (!class_name_out->empty()) {
        std::replace(class_name_out->begin(), class_name_out->end(), '/', '.');
    }
    return true;
}

bool ChooseJavaArrayElementDescriptorForTesting(
    const std::string& target_array_descriptor,
    const std::string& source_array_type_name,
    JavaDescriptorIsAssignableFromForTesting is_assignable_from,
    void* opaque,
    std::string* element_descriptor_out) {
    if (element_descriptor_out == nullptr ||
        target_array_descriptor.empty() ||
        target_array_descriptor.front() != '[') {
        return false;
    }

    const auto type_name_to_descriptor = [&](const std::string& type_name,
                                             const auto& self,
                                             std::string* descriptor_out) -> bool {
        if (descriptor_out == nullptr || type_name.empty()) {
            return false;
        }
        if (type_name == "void") {
            *descriptor_out = "V";
            return true;
        }
        if (type_name == "boolean") {
            *descriptor_out = "Z";
            return true;
        }
        if (type_name == "byte") {
            *descriptor_out = "B";
            return true;
        }
        if (type_name == "char") {
            *descriptor_out = "C";
            return true;
        }
        if (type_name == "short") {
            *descriptor_out = "S";
            return true;
        }
        if (type_name == "int") {
            *descriptor_out = "I";
            return true;
        }
        if (type_name == "long") {
            *descriptor_out = "J";
            return true;
        }
        if (type_name == "float") {
            *descriptor_out = "F";
            return true;
        }
        if (type_name == "double") {
            *descriptor_out = "D";
            return true;
        }
        if (type_name.size() >= 2u && type_name.compare(type_name.size() - 2u, 2u, "[]") == 0) {
            std::string element_descriptor;
            if (!self(type_name.substr(0u, type_name.size() - 2u), self, &element_descriptor)) {
                return false;
            }
            *descriptor_out = "[" + element_descriptor;
            return true;
        }
        std::string normalized = type_name;
        std::replace(normalized.begin(), normalized.end(), '.', '/');
        *descriptor_out = "L" + normalized + ";";
        return true;
    };

    const std::string target_component_descriptor = target_array_descriptor.substr(1u);
    *element_descriptor_out = target_component_descriptor;
    if (source_array_type_name.empty()) {
        return true;
    }

    std::string source_array_descriptor;
    if (!type_name_to_descriptor(source_array_type_name,
                                 type_name_to_descriptor,
                                 &source_array_descriptor) ||
        source_array_descriptor.empty() ||
        source_array_descriptor.front() != '[') {
        return true;
    }

    const std::string source_component_descriptor = source_array_descriptor.substr(1u);
    if (target_component_descriptor == source_component_descriptor) {
        *element_descriptor_out = source_component_descriptor;
        return true;
    }
    if (is_assignable_from != nullptr &&
        is_assignable_from(target_component_descriptor, source_component_descriptor, opaque)) {
        *element_descriptor_out = source_component_descriptor;
    }
    return true;
}

std::string FormatJavaArrayElementErrorForTesting(const std::string& array_type_name,
                                                  std::size_t index,
                                                  const std::string& nested_error) {
    return FormatJavaArrayElementError(array_type_name, index, nested_error);
}

bool ConvertJavaReturnArrayForTesting(const std::string& descriptor,
                                      const std::vector<JavaJsValue>& source_elements,
                                      JavaJsValue* out_value,
                                      std::string* error_message) {
    if (out_value == nullptr) {
        SetError(error_message, "java js value output is required");
        return false;
    }
    if (descriptor.empty() || descriptor.front() != '[') {
        SetError(error_message, "java array descriptor is invalid");
        return false;
    }
    out_value->kind = JavaJsValueKind::kArray;
    out_value->object_handle = 0u;
    out_value->object_handle_is_global = false;
    out_value->object_class_name.clear();
    if (!DescriptorToJavaArrayTypeName(descriptor, &out_value->array_type_name, error_message)) {
        return false;
    }
    out_value->array_elements = source_elements;
    return true;
}

}  // namespace agent_runtime
}  // namespace nook
