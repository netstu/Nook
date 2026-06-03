# Nook Deployment Static Runtime Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `nook-server` directly runnable on Android without `LD_LIBRARY_PATH`, while preserving the existing shared-STL build for the rest of the project.

**Architecture:** Keep the canonical `ndk-build` module graph in one `Android.mk`, and introduce a separate `Application_static.mk` used only for explicitly selected modules through `APP_MODULES`. This minimizes build-graph drift and creates a clean path for a later self-contained `libnook-agent.so`.

**Tech Stack:** Android NDK `ndk-build`, GNU Make Android module definitions, PowerShell deployment commands

---

### Task 1: Add static STL application config for dedicated builds

**Files:**
- Create: `build/android/Application_static.mk`

**Step 1: Write the failing test**

There is no meaningful unit test for this build-configuration-only change. Treat this as a configuration-file exception to normal TDD and verify through build output instead.

**Step 2: Verify the current state is missing**

Run:

```powershell
Get-ChildItem build/android/Application_static.mk
```

Expected:

- file not found

**Step 3: Write minimal implementation**

Create `build/android/Application_static.mk` with:

```make
APP_ABI := arm64-v8a
APP_PLATFORM := android-30
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17
APP_OPTIM := release
```

**Step 4: Verify the file exists**

Run:

```powershell
Get-Content build/android/Application_static.mk
```

Expected:

- the five lines above

### Task 2: Document the dedicated static build invocation

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Write the failing test**

Again, this is documentation and build-workflow guidance, so verify through repository content rather than a unit test.

**Step 2: Verify the current README still documents linker-based startup**

Run:

```powershell
rg -n "linker64|LD_LIBRARY_PATH" host/nook-py/README.md
```

Expected:

- existing linker-based startup instructions are still present

**Step 3: Write minimal implementation**

Update the README to:

1. mention the new static-server build invocation
2. prefer direct server launch once Phase A is validated
3. keep the linker-based command as fallback/troubleshooting guidance until Phase B is complete

**Step 4: Verify the new instructions are present**

Run:

```powershell
rg -n "Application_static.mk|APP_MODULES=nook_server|/data/local/tmp/nook/nook-server" host/nook-py/README.md
```

Expected:

- the new static build and direct launch instructions appear

### Task 3: Build `nook_server` with the static STL config

**Files:**
- Use: `build/android/Android.mk`
- Use: `build/android/Application_static.mk`

**Step 1: Write the failing test**

The failing state is that the current shipped `nook-server` build is expected to depend on `libc++_shared.so`.

**Step 2: Run the build command**

Run:

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_MODULES=nook_server -j4
```

Expected:

- `nook-server` builds successfully

**Step 3: Verify the resulting binary no longer depends on `libc++_shared.so`**

Run a local dependency inspection command appropriate for the toolchain in use.

Expected:

- no `NEEDED` entry for `libc++_shared.so`

### Task 4: Update deployment guidance for direct startup

**Files:**
- Modify: `host/nook-py/README.md`

**Step 1: Write the failing test**

Current docs still imply the linker-based startup is the primary path.

**Step 2: Confirm current wording**

Run:

```powershell
rg -n "typical command|linker path" host/nook-py/README.md
```

Expected:

- wording still prefers the older startup path

**Step 3: Write minimal implementation**

Reword the startup section so it says:

1. direct launch is the preferred path for a static `nook-server`
2. the linker-based command remains a fallback for older artifacts

**Step 4: Verify the wording**

Run:

```powershell
Get-Content host/nook-py/README.md | Select-String -Pattern "preferred|fallback|nook-server"
```

Expected:

- direct launch is primary
- linker-based launch is fallback

### Task 5: Device validation

**Files:**
- Use: `libs/arm64-v8a/nook-server`

**Step 1: Push the binary**

Run:

```powershell
adb push libs/arm64-v8a/nook-server /data/local/tmp/nook/nook-server
adb shell su -c 'chmod 755 /data/local/tmp/nook/nook-server'
```

Expected:

- push succeeds
- permissions applied

**Step 2: Launch directly**

Run:

```powershell
adb shell "su -c '/data/local/tmp/nook/nook-server'"
```

Expected:

- server starts without `LD_LIBRARY_PATH`

**Step 3: Verify host connectivity**

Run:

```powershell
nook-cli apps --usb
```

Expected:

- host connects to the server successfully

### Task 6: Prepare Phase B notes without implementing them

**Files:**
- Modify: `docs/step7.md`

**Step 1: Add the boundary note**

Document that:

1. Phase A is about server direct startup only
2. Phase B will address agent self-containment
3. export filtering for `libnook-agent.so` is postponed until required exported symbols are audited

**Step 2: Verify note presence**

Run:

```powershell
rg -n "Phase A|Phase B|export" docs/step7.md
```

Expected:

- the staging note is present
