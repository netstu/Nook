#pragma once

namespace NookNativeHookInternal {

using OpenSymbolModuleFn = void* (*)(const char* module_name, void* context);
using FindSymbolInModuleFn = void* (*)(void* handle, const char* symbol_name, void* context);
using CloseSymbolModuleFn = void (*)(void* handle, void* context);

struct SymbolResolverDependencies {
    OpenSymbolModuleFn open_preferred_module = nullptr;
    FindSymbolInModuleFn find_preferred_symbol = nullptr;
    CloseSymbolModuleFn close_preferred_module = nullptr;
    OpenSymbolModuleFn open_fallback_module = nullptr;
    FindSymbolInModuleFn find_fallback_symbol = nullptr;
    CloseSymbolModuleFn close_fallback_module = nullptr;
    void* context = nullptr;
};

bool ResolveSymbolAddressWithDependencies(const char* module_name,
                                          const char* symbol_name,
                                          void** symbol_address,
                                          const SymbolResolverDependencies& dependencies);

bool ResolveSymbolAddressInModuleFile(const char* module_path,
                                      const void* module_base,
                                      const char* symbol_name,
                                      void** symbol_address);
bool ResolveSymbolAddressInLoadedModule(const char* module_name,
                                        const char* symbol_name,
                                        void** symbol_address);

bool ResolveSymbolAddress(const char* module_name, const char* symbol_name, void** symbol_address);
bool IsSymbolInlineHookSafeInLoadedModule(const char* module_name,
                                          const char* symbol_name,
                                          void* symbol_address);
bool IsSymbolInlineHookSafeInModuleFile(const char* module_path,
                                        const void* module_base,
                                        const char* symbol_name,
                                        void* symbol_address);

}  // namespace NookNativeHookInternal
