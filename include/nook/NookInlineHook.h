#pragma once

#include "nook/Nook.h"

#ifdef __cplusplus
extern "C" {
#endif

NookStatus NookInlineHookInitialize(void);
NookStatus NookInlineHookIsAvailable(int* available);
NookStatus NookInlineHookAddress(void* target_address,
                                 void* replacement,
                                 void** original,
                                 void** hook_handle);
NookStatus NookInlineHookSymbol(const char* module_name,
                                const char* symbol_name,
                                void* replacement,
                                void** original,
                                void** hook_handle);
NookStatus NookInlineHookSymbolDeferred(const char* module_name,
                                        const char* symbol_name,
                                        void* replacement,
                                        void** original,
                                        void** hook_handle);
NookStatus NookInlineUnhook(void* hook_handle);

#ifdef __cplusplus
}
#endif
