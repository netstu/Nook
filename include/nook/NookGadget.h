#pragma once

#include "Nook.h"

#if defined(__GNUC__) || defined(__clang__)
#define NOOK_GADGET_API __attribute__((visibility("default"), used))
#else
#define NOOK_GADGET_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

NOOK_GADGET_API NookStatus NookGadgetInitialize(void);

#ifdef __cplusplus
}
#endif

#undef NOOK_GADGET_API
