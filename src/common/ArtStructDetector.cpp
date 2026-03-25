#include "ArtStructDetector.h"
#include "../java_hook/JVM.h"
#include <sys/system_properties.h>

// Compatible wrapper for older/newer NDKs.
static int get_device_api_level_compat() {
    static int api_level = 0;
    if (api_level != 0) {
        return api_level;
    }

    // 通过系统属性获取 API level
    char prop_value[PROP_VALUE_MAX] = {0};
    if (__system_property_get("ro.build.version.sdk", prop_value) > 0) {
        api_level = atoi(prop_value);
    }
    return api_level;
}

namespace ArtStructDetector {

    bool getArtRuntimeSpec(
            void *runtime,
            void *javaVM,
            ArtRuntimeSpecOffsets *outSpec) {

        int api_level = get_device_api_level_compat();
        size_t pointerSize = sizeof(void *);
        intptr_t startOffset = (pointerSize == 4) ? 200 : 384;
        intptr_t endOffset = startOffset + (100 * pointerSize);

        if (runtime == NULL || javaVM == NULL || outSpec == NULL) {
            return false;
        }

        for (int delta = 4; delta > 0; delta--) {
            for (intptr_t offset = startOffset; offset < endOffset; offset += pointerSize) {
                void *value = *(void **) ((uintptr_t) runtime + offset);
                if (value == javaVM) {
                    // 找到vm成员偏移，推算其它成员偏移
                    intptr_t classLinkerOffset = 0;
                    intptr_t jniIdManagerOffset = 0;

                    classLinkerOffset = offset - delta * pointerSize;
                    jniIdManagerOffset = offset - 1 * pointerSize;

                    intptr_t internTableOffset = classLinkerOffset - pointerSize;
                    intptr_t threadListOffset = internTableOffset - pointerSize;
                    intptr_t heapOffset = 0;

                    heapOffset = threadListOffset - 9 * pointerSize;
                    outSpec->heap = heapOffset;
                    outSpec->threadList = threadListOffset;
                    outSpec->internTable = internTableOffset;
                    outSpec->classLinker = classLinkerOffset;
                    outSpec->jniIdManager = jniIdManagerOffset;

                    ClassLinkerSpecOffsets tmp;
                    if (tryGetArtClassLinkerSpec(runtime, outSpec, &tmp) &&
                        tmp.quickGenericJniTrampoline != 0)
                        return true;
                }
            }
        }

        return false;
    }

    bool tryGetArtClassLinkerSpec(void *runtime, ArtRuntimeSpecOffsets *runtimeSpec,
                                  ClassLinkerSpecOffsets *output) {

        static int POINTER_SIZE = sizeof(void *);
        uintptr_t classLinkerOffset = runtimeSpec->classLinker;
        uintptr_t internTableOffset = runtimeSpec->internTable;

        void *classLinker = *(void **) ((uintptr_t) runtime + classLinkerOffset);
        void *internTable = *(void **) ((uintptr_t) runtime + internTableOffset);

        intptr_t startOffset = (POINTER_SIZE == 4) ? 100 : 200;
        intptr_t endOffset = startOffset + (100 * POINTER_SIZE);

        for (intptr_t offset = startOffset; offset < endOffset; offset += POINTER_SIZE) {
            void *value = *(void **) ((uintptr_t) classLinker + offset);
            if (value == internTable) {
                int delta = 6;

                intptr_t quickGenericJniTrampolineOffset = offset + (delta * POINTER_SIZE);
                intptr_t quickResolutionTrampolineOffset = 0;

                quickResolutionTrampolineOffset =
                        quickGenericJniTrampolineOffset - (2 * POINTER_SIZE);

                output->quickResolutionTrampoline = quickResolutionTrampolineOffset;
                output->quickImtConflictTrampoline = quickGenericJniTrampolineOffset - POINTER_SIZE;
                output->quickGenericJniTrampoline = quickGenericJniTrampolineOffset;
                output->quickToInterpreterBridgeTrampoline =
                        quickGenericJniTrampolineOffset + POINTER_SIZE;

                LOGI("quickGenericJniTrampoline offset: 0x%lx", quickGenericJniTrampolineOffset);
                return true;
            }
        }

        return false;
    }

    bool detect_artmethod_layout(JNIEnv *env, ArtMethodSpec *output) {
        size_t pointer_size = sizeof(void *);
        jclass cls = env->FindClass("android/os/Process");
        jmethodID mid = env->GetStaticMethodID(cls, "getElapsedCpuTime", "()J");
        env->DeleteLocalRef(cls);

        if (!mid) {
            LOGE("Failed to find test method for ArtMethod detection");
            return false;
        }

        void *art_method = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, mid);
        if (!art_method) {
            LOGE("Failed to decode art_method");
            return false;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(art_method);
        uintptr_t entry_jni_offset = 0;
        uintptr_t access_flags_offset = 0;
        size_t found = 0;

        const uint32_t expected_flags =
                kAccPublic | kAccStatic | kAccFinal | kAccNative;
        const uint32_t flags_mask = 0x0000FFFF;

        for (size_t offset = 0; offset < 64; offset += 4) {
            uintptr_t addr = base + offset;

            // 1. check if it's a pointer into libandroid_runtime.so
            void *maybe_ptr = *reinterpret_cast<void **>(addr);
            if (tool::is_in_module(maybe_ptr, "libandroid_runtime.so")) {
                entry_jni_offset = offset;
                found++;
                LOGI("Found: entry_jni_offset = 0x%lx", offset);
            }

            // 2. check if it looks like access_flags
            uint32_t maybe_flags = *reinterpret_cast<uint32_t *>(addr);
            if ((maybe_flags & flags_mask) == expected_flags) {
                access_flags_offset = offset;
                found++;
                LOGI("Found: access_flags_offset = 0x%lx (flags = 0x%x)", offset, maybe_flags);
            }

            if (found == 2) break;
        }

        if (found != 2) {
            LOGE("Failed to detect ArtMethod field layout");
            return false;
        }

        // 3. quick_code entry offset is next pointer
        uintptr_t entry_quick_offset = entry_jni_offset + pointer_size;

        output->offset_entry_jni = entry_jni_offset;
        output->offset_access_flags = access_flags_offset;
        output->offset_entry_quick = entry_quick_offset;
        output->art_method_size = entry_quick_offset + pointer_size;
        output->interpreterCode = output->offset_entry_jni - pointer_size;

        LOGI("ArtMethod layout detected:");
        LOGI("  offset_entry_jni     = 0x%zx", output->offset_entry_jni);
        LOGI("  offset_access_flags  = 0x%zx", output->offset_access_flags);
        LOGI("  offset_entry_quick    = 0x%zx", output->offset_entry_quick);
        LOGI("  interpreterCode       = 0x%zx", output->interpreterCode);
        LOGI("  art_method_size       = 0x%zx", output->art_method_size);

        return true;
    }

} // namespace ArtStructDetector
