#include <fstream>
#include <string>

int main() {
    std::ifstream input("src/native_hook/inline_hook/inline_hook_module_observer.cpp");
    if (!input.is_open()) {
        return 1;
    }

    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    if (contents.find("thread_local") != std::string::npos) {
        return 1;
    }

    return 0;
}
