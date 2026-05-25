#include <fstream>
#include <string>

int main() {
    std::ifstream input("src/framework/NookInlineHook.cpp");
    if (!input.is_open()) {
        input.open("../../src/framework/NookInlineHook.cpp");
    }
    if (!input.is_open()) {
        return 1;
    }

    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    if (contents.find("InitializeInlineHookModuleObserver()") != std::string::npos) {
        return 1;
    }

    return 0;
}
