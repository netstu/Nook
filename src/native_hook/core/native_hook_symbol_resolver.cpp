#include "native_hook_symbol_resolver.h"

#include "native_hook/plt_hook/elfio_image_parser.h"

#if defined(__ANDROID__)
#include "xdl.h"
#endif

#include <cstdint>
#include <string>

#if defined(__ANDROID__) || defined(__linux__)
#include <dlfcn.h>
#include "native_hook/core/module_info.h"
#endif

namespace NookNativeInternal {

namespace {

bool InitializeResolverOutput(const char* module_name, const char* symbol_name, void** symbol_address) {
    if (symbol_address == nullptr) {
        return false;
    }
    *symbol_address = nullptr;

    if (module_name == nullptr || module_name[0] == '\0' ||
        symbol_name == nullptr || symbol_name[0] == '\0') {
        return false;
    }
    return true;
}

bool TryResolveWithStrategy(const char* module_name,
                            const char* symbol_name,
                            void** symbol_address,
                            OpenSymbolModuleFn open_module,
                            FindSymbolInModuleFn find_symbol,
                            CloseSymbolModuleFn close_module,
                            void* context) {
    if (open_module == nullptr || find_symbol == nullptr) {
        return false;
    }

    void* handle = open_module(module_name, context);
    if (handle == nullptr) {
        return false;
    }

    *symbol_address = find_symbol(handle, symbol_name, context);
    if (close_module != nullptr) {
        close_module(handle, context);
    }
    return *symbol_address != nullptr;
}

#if defined(__ANDROID__)
void* OpenLoadedModuleWithXdl(const char* module_name, void*) {
    return xdl_open(module_name, XDL_DEFAULT);
}

void* FindSymbolInLoadedModuleWithXdl(void* handle, const char* symbol_name, void*) {
    void* address = xdl_sym(handle, symbol_name, nullptr);
    if (address != nullptr) {
        return address;
    }
    return xdl_dsym(handle, symbol_name, nullptr);
}

void CloseLoadedModuleWithXdl(void* handle, void*) {
    if (handle != nullptr) {
        xdl_close(handle);
    }
}
#endif

#if defined(__ANDROID__) || defined(__linux__)
void* OpenModuleWithDlopen(const char* module_name, void*) {
    return dlopen(module_name, RTLD_NOW);
}

void* FindSymbolWithDlsym(void* handle, const char* symbol_name, void*) {
    return dlsym(handle, symbol_name);
}

void CloseModuleWithDlclose(void* handle, void*) {
    if (handle != nullptr) {
        dlclose(handle);
    }
}
#endif

}  // namespace

bool ResolveSymbolAddressWithDependencies(const char* module_name,
                                          const char* symbol_name,
                                          void** symbol_address,
                                          const SymbolResolverDependencies& dependencies) {
    if (!InitializeResolverOutput(module_name, symbol_name, symbol_address)) {
        return false;
    }

    if (TryResolveWithStrategy(module_name,
                               symbol_name,
                               symbol_address,
                               dependencies.open_preferred_module,
                               dependencies.find_preferred_symbol,
                               dependencies.close_preferred_module,
                               dependencies.context)) {
        return true;
    }

    return TryResolveWithStrategy(module_name,
                                  symbol_name,
                                  symbol_address,
                                  dependencies.open_fallback_module,
                                  dependencies.find_fallback_symbol,
                                  dependencies.close_fallback_module,
                                  dependencies.context);
}

bool ResolveSymbolAddressInModuleFile(const char* module_path,
                                      const void* module_base,
                                      const char* symbol_name,
                                      void** symbol_address) {
    if (!InitializeResolverOutput(module_path, symbol_name, symbol_address) || module_base == nullptr) {
        return false;
    }

    ElfHooker::ElfioImageParser parser;
    if (!parser.LoadFromFile(module_path)) {
        return false;
    }

    uint64_t symbol_value = 0;
    if (!parser.FindDynamicSymbolValue(symbol_name, &symbol_value)) {
        return false;
    }

    uintptr_t runtime_bias = 0u;
    if (!parser.ComputeRuntimeBias(reinterpret_cast<uintptr_t>(module_base), &runtime_bias)) {
        return false;
    }

    *symbol_address = reinterpret_cast<void*>(runtime_bias + static_cast<uintptr_t>(symbol_value));
    return true;
}

bool ResolveSymbolAddressInLoadedModule(const char* module_name,
                                        const char* symbol_name,
                                        void** symbol_address) {
    if (!InitializeResolverOutput(module_name, symbol_name, symbol_address)) {
        return false;
    }

#if defined(__ANDROID__) || defined(__linux__)
    void* module_base = nullptr;
    std::string module_path;
    if (!ElfHooker::get_module_info(0, module_name, &module_base, &module_path) ||
        module_base == nullptr || module_path.empty()) {
        return false;
    }

    return ResolveSymbolAddressInModuleFile(module_path.c_str(),
                                            module_base,
                                            symbol_name,
                                            symbol_address);
#else
    return false;
#endif
}

bool ResolveSymbolAddress(const char* module_name, const char* symbol_name, void** symbol_address) {
    SymbolResolverDependencies dependencies = {};

#if defined(__ANDROID__)
    dependencies.open_preferred_module = &OpenLoadedModuleWithXdl;
    dependencies.find_preferred_symbol = &FindSymbolInLoadedModuleWithXdl;
    dependencies.close_preferred_module = &CloseLoadedModuleWithXdl;
#endif

#if defined(__ANDROID__) || defined(__linux__)
    dependencies.open_fallback_module = &OpenModuleWithDlopen;
    dependencies.find_fallback_symbol = &FindSymbolWithDlsym;
    dependencies.close_fallback_module = &CloseModuleWithDlclose;
#endif

    return ResolveSymbolAddressWithDependencies(
            module_name, symbol_name, symbol_address, dependencies);
}

}  // namespace NookNativeInternal
