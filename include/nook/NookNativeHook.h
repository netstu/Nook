#pragma once

#include "nook/NookInlineHook.h"
#include "nook/Nook.h"
#include "nook/NookPltHook.h"

#ifdef __cplusplus
extern "C" {
#endif

NookStatus NookNativeHookInitialize(void);
NookStatus NookNativeHookIsAvailable(int* available);
NookStatus NookNativeHookHookSymbol(const char* module_name,
                                    const char* symbol_name,
                                    void* replacement,
                                    void** original);

#ifdef __cplusplus
}
#endif
