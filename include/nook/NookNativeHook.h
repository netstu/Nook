#pragma once

#include "nook/Nook.h"

#ifdef __cplusplus
extern "C" {
#endif

NookStatus NookNativeHookInitialize(void);
NookStatus NookNativeHookIsAvailable(int* available);

#ifdef __cplusplus
}
#endif
