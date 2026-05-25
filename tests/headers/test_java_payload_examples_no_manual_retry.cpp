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

bool Contains(const std::string& contents, const char* text) {
    return text != nullptr && contents.find(text) != std::string::npos;
}

int ExpectDeferredOnlyJavaPayload(const char* primary_path, const char* fallback_path) {
    const std::string contents = ReadFileWithFallback(primary_path, fallback_path);
    if (contents.empty()) {
        return 1;
    }

    if (!Contains(contents, "NookJavaHookHookDeferred(")) {
        return 1;
    }

    const char* forbidden_tokens[] = {
        "install_hook_now(",
        "NookJavaHookInitialize(",
        "usleep(",
        "sleep_for(",
        "Hooks installed successfully:",
        "NookJavaHookInitialize status="
    };
    for (const char* token : forbidden_tokens) {
        if (Contains(contents, token)) {
            return 1;
        }
    }

    return 0;
}

}  // namespace

int main() {
    if (ExpectDeferredOnlyJavaPayload("examples/java_hook/nook_adwall_loadad_block.cpp",
                                      "../../examples/java_hook/nook_adwall_loadad_block.cpp") != 0) {
        return 1;
    }

    if (ExpectDeferredOnlyJavaPayload("examples/java_hook/nook_java_hook_example.cpp",
                                      "../../examples/java_hook/nook_java_hook_example.cpp") != 0) {
        return 1;
    }

    return 0;
}
