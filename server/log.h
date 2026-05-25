#ifndef NOOK_SERVER_LOG_H
#define NOOK_SERVER_LOG_H

#include <android/log.h>

#define TAG "Ninjector"

#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__))
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__))

#endif // NOOK_SERVER_LOG_H
