# Native Hook Structure Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reorganize the native hook code into framework/core/plt_hook layers so PLT hook remains clean today and inline hook can be added later without reshuffling the project again.

**Architecture:** Move the public native hook entrypoint into `src/framework/`, extract native-hook shared coordination/helpers into `src/native_hook/core/`, and keep PLT-specific ELF parsing/fallback logic under `src/native_hook/plt_hook/`. Preserve runtime behavior while improving layout and build clarity.

**Tech Stack:** C++17, Android NDK, ELFIO, existing host-side manifest/build verification.

---

### Task 1: Lock the target layout with a structure manifest

**Files:**
- Modify: `tests/structure/test_native_hook_manifest.txt`
- Test: `tests/structure/test_native_hook_manifest.txt`

### Task 2: Move framework-facing native hook entrypoint

**Files:**
- Move/Modify: `src/native_hook/NookNativeHook.cpp`
- Create: `src/framework/NookNativeHook.cpp`

### Task 3: Split shared native-hook coordination into core

**Files:**
- Move/Modify: `src/native_hook/native_hook_fallback.h`
- Move/Modify: `src/native_hook/native_hook_fallback.cpp`
- Move/Modify: `src/native_hook/elfhooker/runtime_patch.h`
- Move/Modify: `src/native_hook/elfhooker/runtime_patch.cpp`
- Move/Modify: `src/native_hook/elfhooker/tools.h`
- Move/Modify: `src/native_hook/elfhooker/tools.cpp`
- Move/Modify: `src/native_hook/elfhooker/module_match.h`
- Move/Modify: `src/native_hook/elfhooker/module_match.cpp`

### Task 4: Move PLT-specific implementation under plt_hook

**Files:**
- Move/Modify: `src/native_hook/elfhooker/elfio_image_parser.h`
- Move/Modify: `src/native_hook/elfhooker/elfio_image_parser.cpp`
- Move/Modify: `src/native_hook/elfhooker/elf_reader.h`
- Move/Modify: `src/native_hook/elfhooker/elf_reader.cpp`
- Move/Modify: `src/native_hook/elfhooker/def.h`
- Move/Modify: `src/native_hook/elfhooker/elf_arm.h`
- Move/Modify: `src/native_hook/elfhooker/logger.h`

### Task 5: Update build/test/docs to the new layout

**Files:**
- Modify: `build/android/Android.mk`
- Modify: `build/android/CMakeLists.txt`
- Modify: `tests/headers/test_native_hook_runtime_patch.cpp`
- Modify: `tests/headers/test_elfio_image_parser.cpp`
- Modify: `tests/headers/test_native_hook_fallback.cpp`
- Modify: `README.md`

### Task 6: Verify host checks and Android arm64 build

**Files:**
- Test: `tests/headers/test_native_hook_runtime_patch.cpp`
- Test: `tests/headers/test_elfio_image_parser.cpp`
- Test: `tests/headers/test_native_hook_fallback.cpp`
- Test: `build/android/Android.mk`
