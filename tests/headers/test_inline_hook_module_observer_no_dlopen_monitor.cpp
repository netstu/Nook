#include <fstream>
#include <string>

int main() {
    std::ifstream input("src/native_hook/inline_hook/inline_hook_module_observer.cpp");
    if (!input.is_open()) {
        input.open("../../src/native_hook/inline_hook/inline_hook_module_observer.cpp");
    }
    if (!input.is_open()) {
        return 1;
    }

    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    if (contents.find("HookedDlopen") != std::string::npos) {
        return 1;
    }
    if (contents.find("android_dlopen_ext") != std::string::npos) {
        return 1;
    }
    if (contents.find("call_constructors") == std::string::npos &&
        contents.find("CallConstructors") == std::string::npos) {
        return 1;
    }

    return 0;
}
