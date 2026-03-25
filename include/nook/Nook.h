#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NookStatus {
    NOOK_STATUS_OK = 0,
    NOOK_STATUS_NOT_IMPLEMENTED = -1,
    NOOK_STATUS_INVALID_ARGUMENT = -2,
    NOOK_STATUS_INTERNAL_ERROR = -3
} NookStatus;

const char* NookGetVersion(void);

#ifdef __cplusplus
}
#endif
