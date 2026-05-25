#include <fstream>
#include <string>

namespace {

std::string ReadFile() {
    std::ifstream input("src/framework/NookInlineHook.cpp");
    if (!input.is_open()) {
        input.open("../../src/framework/NookInlineHook.cpp");
    }
    if (!input.is_open()) {
        return std::string();
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool FunctionBodyContains(const std::string& contents, const char* function_name, const char* token) {
    if (function_name == nullptr || token == nullptr) {
        return false;
    }

    const size_t function_start = contents.find(function_name);
    if (function_start == std::string::npos) {
        return false;
    }

    const size_t body_start = contents.find('{', function_start);
    if (body_start == std::string::npos) {
        return false;
    }

    int depth = 0;
    for (size_t index = body_start; index < contents.size(); ++index) {
        if (contents[index] == '{') {
            ++depth;
        } else if (contents[index] == '}') {
            --depth;
            if (depth == 0) {
                const std::string body =
                        contents.substr(body_start, index - body_start + 1u);
                return body.find(token) != std::string::npos;
            }
        }
    }

    return false;
}

}  // namespace

int main() {
    const std::string contents = ReadFile();
    if (contents.empty()) {
        return 1;
    }

    if (FunctionBodyContains(contents,
                             "NookInlineHookSymbolDeferred",
                             "TryInstallInlineHookSymbolNow(")) {
        return 1;
    }

    if (FunctionBodyContains(contents,
                             "NookInlineHookSymbolDeferred",
                             "TryInstallPendingInlineHooksForModule(")) {
        return 1;
    }

    if (contents.find("deferred hook immediate pending retry") != std::string::npos) {
        return 1;
    }

    return 0;
}
