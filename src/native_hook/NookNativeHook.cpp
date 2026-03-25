#include "nook/NookNativeHook.h"

extern "C" {

NookStatus NookNativeHookInitialize(void) {
    return NOOK_STATUS_NOT_IMPLEMENTED;
}

NookStatus NookNativeHookIsAvailable(int* available) {
    if (available == nullptr) {
        return NOOK_STATUS_INVALID_ARGUMENT;
    }

    *available = 0;
    return NOOK_STATUS_NOT_IMPLEMENTED;
}

}
