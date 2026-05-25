#include "nook/NookNativeHook.h"

extern "C" {

NookStatus NookNativeHookInitialize(void) {
    return NookPltHookInitialize();
}

NookStatus NookNativeHookIsAvailable(int* available) {
    return NookPltHookIsAvailable(available);
}

NookStatus NookNativeHookHookSymbol(const char* module_name,
                                    const char* symbol_name,
                                    void* replacement,
                                    void** original) {
    return NookPltHookSymbol(module_name, symbol_name, replacement, original);
}

}  // extern "C"
