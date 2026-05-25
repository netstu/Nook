#include "../../src/native_hook/core/module_match.h"

int main() {
    if (!ElfHooker::module_path_matches("/data/app/com.demo.target/lib/arm64/libnative-lib.so",
                                        "libnative-lib.so")) {
        return 1;
    }

    if (ElfHooker::module_path_matches("/data/app/com.demo.target/lib/arm64/libother.so",
                                       "libnative-lib.so")) {
        return 1;
    }

    return 0;
}
