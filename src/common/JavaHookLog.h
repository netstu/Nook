#ifndef JAVAHOOK_LOG_H
#define JAVAHOOK_LOG_H

#include <android/log.h>
#include <string>
#include <sstream>
#include <iomanip>

#define DEBUG
#ifdef DEBUG
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "JavaHook", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "JavaHook", __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "JavaHook", __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
#define LOGD(...) ((void)0)
#endif

namespace Logger {
    void hex_dump_log(const void *addr, size_t size, const char *tag = "DUMP");
}

#endif // JAVAHOOK_LOG_H
