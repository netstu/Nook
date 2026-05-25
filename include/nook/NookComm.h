#pragma once

#include "Nook.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

NookStatus NookCommInitialize(void);
NookStatus NookCommWaitForResumeIfSpawned(void);
NookStatus NookCommSendMessage(const char* message_json,
                               const uint8_t* data,
                               size_t data_len);

typedef void (*NookCommMessageCallback)(uint32_t script_id,
                                        const char* type,
                                        const char* message_json,
                                        const uint8_t* data,
                                        size_t data_len);
NookStatus NookCommSetMessageCallback(NookCommMessageCallback callback);

typedef NookStatus (*NookCommScriptCreateCallback)(const char* name,
                                                   const char* source,
                                                   uint32_t* script_id);
NookStatus NookCommSetScriptCreateCallback(NookCommScriptCreateCallback callback);

typedef NookStatus (*NookCommScriptLoadCallback)(uint32_t script_id);
NookStatus NookCommSetScriptLoadCallback(NookCommScriptLoadCallback callback);

typedef NookStatus (*NookCommScriptUnloadCallback)(uint32_t script_id);
NookStatus NookCommSetScriptUnloadCallback(NookCommScriptUnloadCallback callback);

typedef void (*NookCommRpcHandler)(const char* method,
                                   const char* args_json,
                                   char** result_json);
NookStatus NookCommRegisterRpc(const char* method, NookCommRpcHandler handler);

#ifdef __cplusplus
}
#endif
