#ifndef JAVAHOOK_ARTSTRUCTDETECTOR_H
#define JAVAHOOK_ARTSTRUCTDETECTOR_H

#include <jni.h>
#include <cstdint>
#include "JavaHookLog.h"
#include "../java_hook/JavaHook.h"

// Android API level
int android_get_device_api_level();

namespace ArtStructDetector {

    // 探测 Runtime 偏移
    bool getArtRuntimeSpec(
        void* runtime,
        void* javaVM,
        ArtRuntimeSpecOffsets* outSpec);

    // 探测 ClassLinker 偏移
    bool tryGetArtClassLinkerSpec(
        void* runtime,
        ArtRuntimeSpecOffsets* runtimeSpec,
        ClassLinkerSpecOffsets* output);

    // 探测 ArtMethod 布局
    bool detect_artmethod_layout(JNIEnv* env, ArtMethodSpec* output);

} // namespace ArtStructDetector

#endif // JAVAHOOK_ARTSTRUCTDETECTOR_H
