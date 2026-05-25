#include <fstream>
#include <string>

namespace {

std::string ReadFileWithFallback(const char* primary_path, const char* fallback_path) {
    std::ifstream input(primary_path);
    if (!input.is_open() && fallback_path != nullptr) {
        input.open(fallback_path);
    }
    if (!input.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool Contains(const std::string& contents, const char* needle) {
    return needle != nullptr && contents.find(needle) != std::string::npos;
}

bool VerifyJavaObjectHandleValueKindExists() {
    const std::string header = ReadFileWithFallback("src/agent_runtime/nook_java_js_bridge.h",
                                                    "../../src/agent_runtime/nook_java_js_bridge.h");
    if (header.empty()) {
        return false;
    }

    return Contains(header, "kObject = 7") &&
           Contains(header, "uint64_t object_handle = 0u;") &&
           Contains(header, "std::string object_class_name;");
}

bool VerifyJavaObjectArgumentsConvertToJsWrappers() {
    const std::string bridge = ReadFileWithFallback("src/agent_runtime/nook_java_js_bridge.cpp",
                                                    "../../src/agent_runtime/nook_java_js_bridge.cpp");
    if (bridge.empty()) {
        return false;
    }

    return Contains(bridge, "out_value->kind = JavaJsValueKind::kObject;") &&
           Contains(bridge, "out_value->object_handle = reinterpret_cast<uint64_t>(object);") &&
           Contains(bridge, "return DescribeJavaObject(env, object, &out_value->object_class_name, error_message);") &&
           Contains(bridge, "unsupported Java descriptor for JS conversion");
}

bool VerifyJavaReadyBootstrapHelpersExist() {
    const std::string runtime = ReadFileWithFallback("src/agent_runtime/js_runtime.cpp",
                                                     "../../src/agent_runtime/js_runtime.cpp");
    if (runtime.empty()) {
        return false;
    }

    return Contains(runtime, "JSValue JsJavaUpdateClassLoader(") &&
           Contains(runtime, "JSValue JsJavaIsClassLoaderReady(") &&
           Contains(runtime, "JSValue JsJavaIsApplicationReady(") &&
           Contains(runtime, "JSValue JsJavaIsLifecycleReady(") &&
           Contains(runtime, "Java.ready = function (fn)") &&
           Contains(runtime, "Java.__nookDispatchReady = function ()") &&
            Contains(runtime, "Java._updateClassLoader") &&
            Contains(runtime, "Java._isClassLoaderReady") &&
            Contains(runtime, "\"_isLifecycleReady\"") &&
            Contains(runtime, "if (readyFired ||") &&
            Contains(runtime, "Java._isClassLoaderReady() ||") &&
            Contains(runtime, "(typeof Java._isLifecycleReady === 'function' && Java._isLifecycleReady())") &&
           !Contains(runtime, "setImmediate(pollReadyState);") &&
           !Contains(runtime, "java-ready-debug:queued:") &&
           !Contains(runtime, "java-ready-debug:poll-drain") &&
           !Contains(runtime, "java-ready-debug:immediate") &&
           Contains(runtime, "if (typeof Java._isClassLoaderReady === 'function' &&") &&
           !Contains(runtime, "java-ready-debug:install-hooks") &&
           !Contains(runtime, "Instrumentation.newApplication") &&
           !Contains(runtime, "callApplicationOnCreate-drain-before");
}

bool VerifyLoaderResolverSupportsExplicitClassLoaderCache() {
    const std::string header = ReadFileWithFallback("src/java_hook/deferred/java_hook_loader_resolver.h",
                                                    "../../src/java_hook/deferred/java_hook_loader_resolver.h");
    const std::string source = ReadFileWithFallback("src/java_hook/deferred/java_hook_loader_resolver.cpp",
                                                    "../../src/java_hook/deferred/java_hook_loader_resolver.cpp");
    if (header.empty() || source.empty()) {
        return false;
    }

    return Contains(header, "bool UpdateApplicationClassLoader(JNIEnv* env, jobject class_loader);") &&
           Contains(header, "bool IsApplicationClassLoaderReady(JNIEnv* env);") &&
           Contains(header, "bool IsCurrentApplicationReady(JNIEnv* env);") &&
           Contains(header, "void MarkApplicationLifecycleReady(JNIEnv* env, jobject application);") &&
           Contains(header, "bool IsApplicationLifecycleReady(JNIEnv* env);") &&
           Contains(header, "void SetRequireApplicationLifecycleReady(bool required);") &&
           Contains(source, "std::mutex g_application_class_loader_mutex;") &&
           Contains(source, "jobject g_application_class_loader = nullptr;") &&
           Contains(source, "env->NewGlobalRef(class_loader);") &&
           Contains(source, "GetCachedApplicationClassLoader(") &&
           Contains(source, "bool IsCurrentApplicationReady(JNIEnv* env)") &&
           Contains(source, "void MarkApplicationLifecycleReady(JNIEnv* env, jobject application)") &&
           Contains(source, "bool IsApplicationLifecycleReady(JNIEnv* env)") &&
           Contains(source, "void SetRequireApplicationLifecycleReady(bool required)") &&
           Contains(source, "g_require_application_lifecycle_ready = required;") &&
           Contains(source, "if (require_lifecycle_ready) {") &&
           Contains(source, "return IsApplicationLifecycleReady(env);") &&
           Contains(source, "g_application_lifecycle_ready = true;") &&
           Contains(source, "jobject application = GetCurrentApplication(env);") &&
           Contains(source, "if (application == nullptr) {");
}

}  // namespace

int main() {
    if (!VerifyJavaObjectHandleValueKindExists()) {
        return 1;
    }
    if (!VerifyJavaObjectArgumentsConvertToJsWrappers()) {
        return 1;
    }
    if (!VerifyJavaReadyBootstrapHelpersExist()) {
        return 1;
    }
    if (!VerifyLoaderResolverSupportsExplicitClassLoaderCache()) {
        return 1;
    }
    return 0;
}
