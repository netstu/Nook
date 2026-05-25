#pragma once

#include "Nook.h"

#if defined(__GNUC__) || defined(__clang__)
#define NOOK_AGENT_API __attribute__((visibility("default"), used))
#else
#define NOOK_AGENT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

NOOK_AGENT_API NookStatus NookAgentInitialize(void);
NOOK_AGENT_API NookStatus NookAgentInitializeForZygoteControl(void);
NOOK_AGENT_API NookStatus NookAgentReinitializeForZygoteControl(void);
NOOK_AGENT_API NookStatus NookAgentUninstallZygoteControlHooks(void);

#ifdef __cplusplus
}
#endif

#undef NOOK_AGENT_API
