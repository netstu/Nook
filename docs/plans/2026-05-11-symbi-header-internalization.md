# Symbi Header Internalization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove `symbi` header/include dependence on the external `Ninjector/jni` tree while keeping current runtime behavior unchanged.

**Architecture:** Copy the minimal `symbi` headers and stub header assets into Nook-local paths, switch `symbi_injector_local.cpp` to those local headers, add a Nook-local logging header, then remove the external include paths from the Android build and verify `nook_server` still builds.

**Tech Stack:** Android NDK build, C/C++ headers, existing Nook server injector code

---

### Task 1: Vendor minimal symbi headers into Nook

**Files:**
- Create: `server/symbi/symbi_injector.h`
- Create: `server/symbi/symbi_stub.h`
- Create: `server/symbi/stub_src/stub.h`
- Create: `server/symbi/stub_src/generated_stub.h`

**Step 1: Copy the exact header content**

Bring over the existing declarations only. Do not refactor names or structure in this step.

**Step 2: Check include closure**

Make sure `symbi_stub.h` resolves against the vendored `stub_src` directory inside Nook.

### Task 2: Add a Nook-local logging header

**Files:**
- Create: `server/log.h`

**Step 1: Copy the small Android log macro wrapper**

Keep the same `LOGD/LOGI/LOGE` macro surface so no behavior code changes are needed.

### Task 3: Switch symbi local implementation to Nook-local headers

**Files:**
- Modify: `server/symbi_injector_local.cpp`

**Step 1: Replace external includes**

Change:

- `symbi/symbi_injector.h`
- `symbi/symbi_stub.h`
- `../../Ninjector/jni/common/log.h`

to Nook-local include paths.

**Step 2: Do not touch runtime logic**

No behavioral edits in this step.

### Task 4: Remove external include paths from Android build

**Files:**
- Modify: `build/android/Android.mk`

**Step 1: Delete external `Ninjector/jni` include paths**

Remove:

- `$(ROOT_PATH)/../Ninjector/jni`
- `$(ROOT_PATH)/../../Ninjector/jni`

**Step 2: Ensure server-local includes still resolve**

If needed, add only Nook-local include roots.

### Task 5: Build verification

**Files:**
- Verify: `build/android/Android.mk`

**Step 1: Build `nook_server`**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_ABI=arm64-v8a APP_MODULES=nook_server -j4
```

Expected:

- build succeeds
- no compile-time dependency remains on external `Ninjector/jni`

**Step 2: Record result**

If build passes, this step is done. Runtime testing is intentionally deferred to the next change set.
