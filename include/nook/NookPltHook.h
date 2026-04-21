#pragma once

#include "nook/Nook.h"

#ifdef __cplusplus
extern "C" {
#endif

NookStatus NookPltHookInitialize(void);
NookStatus NookPltHookIsAvailable(int* available);
NookStatus NookPltHookSymbol(const char* module_name,
                             const char* symbol_name,
                             void* replacement,
                             void** original);

#ifdef __cplusplus
}
#endif
