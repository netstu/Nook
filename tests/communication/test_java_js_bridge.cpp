#include <cassert>
#include <string>

#include "agent_runtime/nook_java_js_bridge.h"

using namespace nook::agent_runtime;

namespace {

struct InstallCallCapture {
    int call_count = 0;
    JavaJsHookRequest request = {};
};

int& GetResolveSignatureCallCount() {
    static int count = 0;
    return count;
}

InstallCallCapture& GetInstallCallCapture() {
    static InstallCallCapture capture;
    return capture;
}

bool FakeInstallJavaHook(const JavaJsHookRequest& request,
                         JavaJsHookRecord* out_record,
                         std::string* error_message) {
    (void)error_message;
    InstallCallCapture& capture = GetInstallCallCapture();
    ++capture.call_count;
    capture.request = request;
    if (out_record != nullptr) {
        out_record->hook_id = request.hook_id;
        out_record->class_name = request.class_name;
        out_record->method_name = request.method_name;
        out_record->signature = request.signature;
        out_record->is_static = request.is_static;
        out_record->deferred = request.deferred;
        out_record->installed_hook_id = static_cast<int>(1000u + request.hook_id);
    }
    return true;
}

bool FailingInstallJavaHook(const JavaJsHookRequest& request,
                            JavaJsHookRecord* out_record,
                            std::string* error_message) {
    (void)request;
    (void)out_record;
    if (error_message != nullptr) {
        *error_message = "fake java install failed";
    }
    return false;
}

bool CountingResolveJavaMethodSignature(const std::string& class_name,
                                        const std::string& method_name,
                                        const std::vector<std::string>& argument_type_names,
                                        uint64_t loader_handle,
                                        bool is_static,
                                        std::string* signature,
                                        std::string* error_message) {
    (void)class_name;
    (void)method_name;
    (void)argument_type_names;
    (void)loader_handle;
    (void)is_static;
    ++GetResolveSignatureCallCount();
    if (signature != nullptr) {
        *signature = "()I";
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool FakeInstallDeferredWildcardJavaHook(const JavaJsHookRequest& request,
                                         JavaJsHookRecord* out_record,
                                         std::string* error_message) {
    (void)error_message;
    if (out_record != nullptr) {
        out_record->hook_id = request.hook_id;
        out_record->class_name = request.class_name;
        out_record->method_name = request.method_name;
        out_record->signature = request.signature;
        out_record->is_static = request.is_static;
        out_record->deferred = request.deferred;
        out_record->installed_hook_id = 0x40000000 + static_cast<int>(request.hook_id);
        out_record->callback_slot = 0u;
    }
    return true;
}

bool FakeDescriptorIsAssignableFrom(const std::string& target_descriptor,
                                    const std::string& source_descriptor,
                                    void* opaque) {
    (void)opaque;
    if (target_descriptor == source_descriptor) {
        return true;
    }
    if (target_descriptor == "Ljava/lang/CharSequence;" &&
        source_descriptor == "Ljava/lang/String;") {
        return true;
    }
    if (target_descriptor == "[Ljava/lang/CharSequence;" &&
        source_descriptor == "[Ljava/lang/String;") {
        return true;
    }
    if (target_descriptor == "[[Ljava/lang/CharSequence;" &&
        source_descriptor == "[[Ljava/lang/String;") {
        return true;
    }
    return false;
}

void TestInstallJavaJsHookAssignsIncrementingIdsAndStoresMetadata() {
    ResetJavaJsHookRegistryForTesting();
    GetInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;

    JavaJsHookRequest first = {};
    first.class_name = "com.demo.target.LoginFragment";
    first.method_name = "verifyPassword";
    first.signature = "(Ljava/lang/String;)Z";

    JavaJsHookRecord first_record = {};
    std::string error_message;
    assert(InstallJavaJsHook(first, dependencies, &first_record, &error_message));
    assert(first_record.hook_id == 1u);
    assert(first_record.class_name == "com.demo.target.LoginFragment");
    assert(first_record.method_name == "verifyPassword");
    assert(first_record.signature == "(Ljava/lang/String;)Z");
    assert(first_record.installed_hook_id == 1001);

    const InstallCallCapture& first_capture = GetInstallCallCapture();
    assert(first_capture.call_count == 1);
    assert(first_capture.request.hook_id == 1u);
    assert(first_capture.request.class_name == "com.demo.target.LoginFragment");
    assert(first_capture.request.method_name == "verifyPassword");

    JavaJsHookRequest second = {};
    second.class_name = "com.demo.target.LoginFragment";
    second.method_name = "verifyPasswordNative";
    second.signature = "(Ljava/lang/String;)Ljava/lang/String;";
    second.is_static = true;

    JavaJsHookRecord second_record = {};
    assert(InstallJavaJsHook(second, dependencies, &second_record, &error_message));
    assert(second_record.hook_id == 2u);
    assert(second_record.installed_hook_id == 1002);
    assert(GetInstalledJavaJsHookCountForTesting() == 2u);

    JavaJsHookRecord stored = {};
    assert(GetJavaJsHookRecordForTesting(1u, &stored));
    assert(stored.class_name == "com.demo.target.LoginFragment");
    assert(stored.method_name == "verifyPassword");
    assert(stored.signature == "(Ljava/lang/String;)Z");
}

void TestInstallJavaJsHookRejectsInvalidRequestAndInstallerFailure() {
    ResetJavaJsHookRegistryForTesting();

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;

    JavaJsHookRequest invalid = {};
    invalid.method_name = "verifyPassword";
    invalid.signature = "(Ljava/lang/String;)Z";

    JavaJsHookRecord record = {};
    std::string error_message;
    assert(!InstallJavaJsHook(invalid, dependencies, &record, &error_message));
    assert(error_message.find("class name") != std::string::npos);

    dependencies.install_hook = &FailingInstallJavaHook;

    JavaJsHookRequest failing = {};
    failing.class_name = "com.demo.target.LoginFragment";
    failing.method_name = "verifyPassword";
    failing.signature = "(Ljava/lang/String;)Z";

    error_message.clear();
    assert(!InstallJavaJsHook(failing, dependencies, &record, &error_message));
    assert(error_message.find("fake java install failed") != std::string::npos);
    assert(GetInstalledJavaJsHookCountForTesting() == 0u);
}

void TestInstallJavaJsHookNormalizesConstructorAliasToInit() {
    ResetJavaJsHookRegistryForTesting();
    GetInstallCallCapture() = {};

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;

    JavaJsHookRequest request = {};
    request.class_name = "com.zj.wuaipojie.Demo";
    request.method_name = "$init";
    request.signature = "(Ljava/lang/String;)V";

    JavaJsHookRecord record = {};
    std::string error_message;
    assert(InstallJavaJsHook(request, dependencies, &record, &error_message));

    const InstallCallCapture& capture = GetInstallCallCapture();
    assert(capture.call_count == 1);
    assert(capture.request.method_name == "<init>");

    JavaJsHookRecord stored = {};
    assert(GetJavaJsHookRecordForTesting(record.hook_id, &stored));
    assert(stored.method_name == "<init>");
}

void TestInstallJavaJsHookKeepsWildcardSignatureForDeferredRequest() {
    ResetJavaJsHookRegistryForTesting();
    GetInstallCallCapture() = {};
    GetResolveSignatureCallCount() = 0;

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;
    dependencies.resolve_signature = &CountingResolveJavaMethodSignature;

    JavaJsHookRequest request = {};
    request.class_name = "com.ad2001.frida0x1.MainActivity";
    request.method_name = "get_random";
    request.signature = "*";
    request.deferred = true;

    JavaJsHookRecord record = {};
    std::string error_message;
    assert(InstallJavaJsHook(request, dependencies, &record, &error_message));

    const InstallCallCapture& capture = GetInstallCallCapture();
    assert(capture.call_count == 1);
    assert(capture.request.signature == "*");
    assert(record.signature == "*");
    assert(GetResolveSignatureCallCount() == 0);
}

void TestDeferredWildcardHookCanResolveInstalledSignatureDuringCallback() {
    ResetJavaJsHookRegistryForTesting();

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallDeferredWildcardJavaHook;

    JavaJsHookRequest request = {};
    request.class_name = "com.ad2001.frida0x1.MainActivity";
    request.method_name = "get_random";
    request.signature = "*";
    request.deferred = true;

    JavaJsHookRecord record = {};
    std::string error_message;
    assert(InstallJavaJsHook(request, dependencies, &record, &error_message));

    JavaJsHookRecord stored = {};
    assert(GetJavaJsHookRecordForTesting(record.hook_id, &stored));
    assert(stored.installed_hook_id >= 0x40000000);
}

void TestUninstallJavaJsHookRemovesStoredRecord() {
    ResetJavaJsHookRegistryForTesting();

    JavaJsHookInstallerDependencies dependencies = {};
    dependencies.install_hook = &FakeInstallJavaHook;

    JavaJsHookRequest request = {};
    request.class_name = "com.demo.target.LoginFragment";
    request.method_name = "verifyPassword";
    request.signature = "(Ljava/lang/String;)Z";

    JavaJsHookRecord record = {};
    std::string error_message;
    assert(InstallJavaJsHook(request, dependencies, &record, &error_message));
    assert(GetInstalledJavaJsHookCountForTesting() == 1u);

    assert(UninstallJavaJsHook(record.hook_id, &error_message));
    assert(GetInstalledJavaJsHookCountForTesting() == 0u);

    ResetJavaJsHookRegistryForTesting();
}

void TestJavaParameterDescriptorAcceptsArrayCovariance() {
    assert(JavaParameterDescriptorAcceptsArgumentForTesting(
        "Ljava/lang/Object;", "Ljava/lang/String;"));
    assert(JavaParameterDescriptorAcceptsArgumentForTesting(
        "[Ljava/lang/Object;", "[Ljava/lang/String;"));
    assert(JavaParameterDescriptorAcceptsArgumentForTesting(
        "[[Ljava/lang/Object;", "[[Ljava/lang/String;"));
    assert(!JavaParameterDescriptorAcceptsArgumentForTesting(
        "Ljava/lang/String;", "Ljava/lang/Object;"));
    assert(!JavaParameterDescriptorAcceptsArgumentForTesting(
        "[Ljava/lang/String;", "[Ljava/lang/Object;"));
    assert(!JavaParameterDescriptorAcceptsArgumentForTesting(
        "[Ljava/lang/Object;", "[I"));
}

void TestChooseJavaArrayElementDescriptorSupportsGenericReferenceCovariance() {
    std::string element_descriptor;
    assert(ChooseJavaArrayElementDescriptorForTesting("[Ljava/lang/CharSequence;",
                                                      "java.lang.String[]",
                                                      &FakeDescriptorIsAssignableFrom,
                                                      nullptr,
                                                      &element_descriptor));
    assert(element_descriptor == "Ljava/lang/String;");

    assert(ChooseJavaArrayElementDescriptorForTesting("[[Ljava/lang/CharSequence;",
                                                      "java.lang.String[][]",
                                                      &FakeDescriptorIsAssignableFrom,
                                                      nullptr,
                                                      &element_descriptor));
    assert(element_descriptor == "[Ljava/lang/String;");
}

void TestFormatJavaArrayElementErrorIncludesArrayNameAndIndex() {
    const std::string formatted =
        FormatJavaArrayElementErrorForTesting("int[]", 1u, "Java int expects JS int32");
    assert(formatted == "Java array int[] element[1]: Java int expects JS int32");

    const std::string nested =
        FormatJavaArrayElementErrorForTesting(
            "int[][]",
            0u,
            FormatJavaArrayElementErrorForTesting("int[]", 1u, "Java int expects JS int32"));
    assert(nested ==
           "Java array int[][] element[0]: Java array int[] element[1]: Java int expects JS int32");
}

}  // namespace

int main() {
    TestInstallJavaJsHookAssignsIncrementingIdsAndStoresMetadata();
    TestInstallJavaJsHookRejectsInvalidRequestAndInstallerFailure();
    TestInstallJavaJsHookNormalizesConstructorAliasToInit();
    TestInstallJavaJsHookKeepsWildcardSignatureForDeferredRequest();
    TestDeferredWildcardHookCanResolveInstalledSignatureDuringCallback();
    TestUninstallJavaJsHookRemovesStoredRecord();
    TestJavaParameterDescriptorAcceptsArrayCovariance();
    TestChooseJavaArrayElementDescriptorSupportsGenericReferenceCovariance();
    TestFormatJavaArrayElementErrorIncludesArrayNameAndIndex();
    return 0;
}
