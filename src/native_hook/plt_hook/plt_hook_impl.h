#ifndef NOOK_NATIVE_HOOK_PLT_HOOK_IMPL_H
#define NOOK_NATIVE_HOOK_PLT_HOOK_IMPL_H

#include "native_hook/core/native_hook_dispatcher.h"

namespace NookNativeHookInternal {

bool TryPltHookWithElfio(const ResolvedHookTarget& target, void* context);
bool TryPltHookWithElfReader(const ResolvedHookTarget& target, void* context);

}  // namespace NookNativeHookInternal

#endif  // NOOK_NATIVE_HOOK_PLT_HOOK_IMPL_H
