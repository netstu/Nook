#include <fstream>
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

bool ContainsModuleBlockWith(const std::string& contents,
                             const char* module_name,
                             const char* required_text) {
    if (module_name == nullptr || required_text == nullptr) {
        return false;
    }

    const std::string marker = std::string("LOCAL_MODULE := ") + module_name;
    const size_t block_start = contents.find(marker);
    if (block_start == std::string::npos) {
        return false;
    }

    size_t block_end = contents.find("include $(CLEAR_VARS)", block_start + marker.size());
    if (block_end == std::string::npos) {
        block_end = contents.size();
    }

    return contents.find(required_text, block_start) != std::string::npos &&
           contents.find(required_text, block_start) < block_end;
}

bool ContainsModuleBlockWithout(const std::string& contents,
                                const char* module_name,
                                const char* forbidden_text) {
    if (module_name == nullptr || forbidden_text == nullptr) {
        return false;
    }

    const std::string marker = std::string("LOCAL_MODULE := ") + module_name;
    const size_t block_start = contents.find(marker);
    if (block_start == std::string::npos) {
        return false;
    }

    size_t block_end = contents.find("include $(CLEAR_VARS)", block_start + marker.size());
    if (block_end == std::string::npos) {
        block_end = contents.size();
    }

    const size_t forbidden_pos = contents.find(forbidden_text, block_start);
    return forbidden_pos == std::string::npos || forbidden_pos >= block_end;
}

bool ContainsCmakeTargetBlockWith(const std::string& contents,
                                  const char* target_name,
                                  const char* required_text) {
    if (target_name == nullptr || required_text == nullptr) {
        return false;
    }

    const std::string marker = std::string("add_library(") + target_name;
    const size_t target_start = contents.find(marker);
    if (target_start == std::string::npos) {
        return false;
    }

    const size_t link_start =
            contents.find(std::string("target_link_libraries(") + target_name, target_start);
    if (link_start == std::string::npos) {
        return false;
    }

    const size_t link_end = contents.find(")", link_start);
    if (link_end == std::string::npos) {
        return false;
    }

    return contents.find(required_text, link_start) != std::string::npos &&
           contents.find(required_text, link_start) < link_end;
}

bool ContainsCmakeTargetBlockWithout(const std::string& contents,
                                     const char* target_name,
                                     const char* forbidden_text) {
    if (target_name == nullptr || forbidden_text == nullptr) {
        return false;
    }

    const std::string marker = std::string("add_library(") + target_name;
    const size_t target_start = contents.find(marker);
    if (target_start == std::string::npos) {
        return false;
    }

    const size_t next_target = contents.find("add_library(", target_start + marker.size());
    const size_t target_end = next_target == std::string::npos ? contents.size() : next_target;

    const size_t forbidden_pos = contents.find(forbidden_text, target_start);
    return forbidden_pos == std::string::npos || forbidden_pos >= target_end;
}

bool ContainsAndroidMkModule(const std::string& contents, const char* module_name) {
    if (module_name == nullptr) {
        return false;
    }
    return contents.find(std::string("LOCAL_MODULE := ") + module_name) != std::string::npos;
}

bool ContainsCmakeTarget(const std::string& contents, const char* target_name) {
    if (target_name == nullptr) {
        return false;
    }
    return contents.find(std::string("add_library(") + target_name) != std::string::npos;
}

int ExpectAndroidMkPayloadExamplesDoNotLinkNook() {
    const std::string contents =
            ReadFileWithFallback("build/android/Android.mk", "../../build/android/Android.mk");
    if (contents.empty()) {
        return 1;
    }

    const char* modules[] = {
            "nook_native_strcmp_test",
            "nook_native_inline_test",
            "nook_native_verify_password_inline_test"};
    for (const char* module_name : modules) {
        if (!ContainsModuleBlockWithout(contents, module_name, "LOCAL_SHARED_LIBRARIES := nook")) {
            return 1;
        }
        if (!ContainsModuleBlockWithout(contents, module_name, "$(NOOK_RUNTIME_SRC)")) {
            return 1;
        }
    }

    return 0;
}

int ExpectCmakePayloadExamplesDoNotLinkNook() {
    const std::string contents =
            ReadFileWithFallback("build/android/CMakeLists.txt",
                                 "../../build/android/CMakeLists.txt");
    if (contents.empty()) {
        return 1;
    }

    const char* targets[] = {
            "nook_native_strcmp_test",
            "nook_native_inline_test",
            "nook_native_verify_password_inline_test"};
    for (const char* target_name : targets) {
        if (!ContainsCmakeTargetBlockWithout(contents, target_name, "${NOOK_RUNTIME_SRC}")) {
            return 1;
        }
        if (!ContainsCmakeTargetBlockWithout(contents, target_name, "    nook")) {
            return 1;
        }
    }

    return 0;
}

int ExpectNativePayloadsUseRuntimeLoaderHeader() {
    const char* payload_paths[] = {
            "examples/native_hook/nook_native_strcmp_test/payload.cpp",
            "examples/native_hook/nook_native_inline_test/payload.cpp",
            "examples/native_hook/nook_native_verify_password_inline_test/payload.cpp"};
    const char* fallback_paths[] = {
            "../../examples/native_hook/nook_native_strcmp_test/payload.cpp",
            "../../examples/native_hook/nook_native_inline_test/payload.cpp",
            "../../examples/native_hook/nook_native_verify_password_inline_test/payload.cpp"};

    for (size_t index = 0; index < 3; ++index) {
        const std::string contents = ReadFileWithFallback(payload_paths[index], fallback_paths[index]);
        if (contents.empty()) {
            return 1;
        }
        if (contents.find("../common/nook_runtime_loader.h") == std::string::npos) {
            return 1;
        }
    }

    const std::string loader_contents =
            ReadFileWithFallback("examples/native_hook/common/nook_runtime_loader.h",
                                 "../../examples/native_hook/common/nook_runtime_loader.h");
    if (loader_contents.empty()) {
        return 1;
    }
    if (loader_contents.find("dlopen") == std::string::npos) {
        return 1;
    }
    if (loader_contents.find("libnook.so") == std::string::npos) {
        return 1;
    }

    return 0;
}

int ExpectInlineObserverProbeBuildArtifact() {
    const std::string android_mk_contents =
            ReadFileWithFallback("build/android/Android.mk", "../../build/android/Android.mk");
    if (android_mk_contents.empty()) {
        return 1;
    }
    if (!ContainsAndroidMkModule(android_mk_contents, "nook_inline_observer_probe")) {
        return 1;
    }

    const std::string cmake_contents =
            ReadFileWithFallback("build/android/CMakeLists.txt",
                                 "../../build/android/CMakeLists.txt");
    if (cmake_contents.empty()) {
        return 1;
    }
    if (!ContainsCmakeTarget(cmake_contents, "nook_inline_observer_probe")) {
        return 1;
    }

    const std::string observer_contents =
            ReadFileWithFallback("src/native_hook/inline_hook/inline_hook_module_observer.cpp",
                                 "../../src/native_hook/inline_hook/inline_hook_module_observer.cpp");
    if (observer_contents.empty()) {
        return 1;
    }
    if (observer_contents.find("libnook_inline_observer_probe.so") == std::string::npos) {
        return 1;
    }
    if (observer_contents.find("constexpr char kProbeLibraryName[] = \"libnook.so\";") !=
        std::string::npos) {
        return 1;
    }

    return 0;
}

}  // namespace

int main() {
    if (ExpectAndroidMkPayloadExamplesDoNotLinkNook() != 0) {
        return 1;
    }
    if (ExpectCmakePayloadExamplesDoNotLinkNook() != 0) {
        return 1;
    }
    if (ExpectNativePayloadsUseRuntimeLoaderHeader() != 0) {
        return 1;
    }
    if (ExpectInlineObserverProbeBuildArtifact() != 0) {
        return 1;
    }
    return 0;
}
