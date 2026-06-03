# Nook Inline Deferred Install Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add an event-driven deferred inline symbol hook path to Nook so payloads can register an inline hook before the target module loads and have it installed automatically when that module is loaded.

**Architecture:** Keep the current immediate inline hook path intact, add a pending inline hook registry that is testable on host, and add an Android-specific module load observer that hooks `dlopen` and `android_dlopen_ext` to trigger installation for matching pending entries.

**Tech Stack:** C++17, Android NDK, arm64 inline patching, `dlopen`, `android_dlopen_ext`, existing Nook inline hook backend, xdl-based symbol resolution, host-side g++

---

### Task 1: Lock the new public deferred inline API with a header test

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookInlineHook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`

**Step 1: Write the failing test**

Update `test_public_headers.cpp` to reference:

- `NookInlineHookSymbolDeferred(...)`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_deferred.o
```

Expected: FAIL because the new function is not declared yet.

**Step 3: Write minimal implementation**

Add the declaration to `include/nook/NookInlineHook.h`.

**Step 4: Run test to verify it passes**

Run the same command and expect PASS.

### Task 2: Add a failing host test for the pending registry

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_pending_inline_hook_registry.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\pending_inline_hook_registry.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\pending_inline_hook_registry.cpp`

**Step 1: Write the failing test**

Add host checks that verify:

- invalid records are rejected
- matching module notifications install pending hooks
- non-matching notifications do not install
- failed install attempts remain pending
- installed records are not installed twice

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_pending_inline_hook_registry.cpp .\src\native_hook\inline_hook\pending_inline_hook_registry.cpp .\src\native_hook\core\module_match.cpp -o .\tests\headers\test_pending_inline_hook_registry.exe
```

Expected: FAIL because the registry does not exist yet.

**Step 3: Write minimal implementation**

Implement only the storage and install-attempt flow needed to satisfy the test.

**Step 4: Run test to verify it passes**

Run the same compile command, then:

```powershell
.\tests\headers\test_pending_inline_hook_registry.exe
```

Expected: PASS.

### Task 3: Add the Android module load observer

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\inline_hook_module_observer.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\inline_hook_module_observer.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\CMakeLists.txt`

**Step 1: Write the failing test**

Create a compile-level host or structure test that requires the observer files to exist and be part of the build definitions.

**Step 2: Run test to verify it fails**

Run:

```powershell
Select-String -Path .\build\android\Android.mk,.\build\android\CMakeLists.txt -Pattern 'pending_inline_hook_registry|inline_hook_module_observer'
```

Expected: no matches before the files are wired.

**Step 3: Write minimal implementation**

Implement:

- one-time observer initialization
- `dlopen` and `android_dlopen_ext` resolution
- inline hook install for available loader symbols
- reentrancy guard around module notifications

**Step 4: Run test to verify it passes**

Run the same command and expect matches in both Android build files.

### Task 4: Add the deferred framework API and integrate the registry

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookInlineHook.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookInlineHook.h`

**Step 1: Write the failing test**

Extend a public-header or stub test so the new deferred API must compile and the registry-backed code path is linked.

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_stub.cpp -o .\tests\headers\test_native_hook_stub.exe
```

Expected: FAIL until the new framework code is linked correctly.

**Step 3: Write minimal implementation**

Implement `NookInlineHookSymbolDeferred(...)` as:

1. validate args
2. initialize inline runtime and module observer
3. try immediate symbol install once
4. on miss, register pending hook

**Step 4: Run test to verify it passes**

Compile and run the stub/header tests and expect PASS.

### Task 5: Move the verify-password example from polling to deferred registration

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\examples\native_hook\nook_native_verify_password_inline_test\payload.cpp`

**Step 1: Write the failing test**

Add a compile check or structure expectation that the payload no longer references retry thread logic and instead calls `NookInlineHookSymbolDeferred(...)`.

**Step 2: Run test to verify it fails**

Run:

```powershell
Select-String -Path .\examples\native_hook\nook_native_verify_password_inline_test\payload.cpp -Pattern 'NookInlineHookSymbolDeferred|pthread_create|Retry'
```

Expected: missing deferred API usage before the change.

**Step 3: Write minimal implementation**

Replace the detached install thread with one constructor-time deferred registration call and concise logging.

**Step 4: Run test to verify it passes**

Run the same search command and expect the deferred API call to exist while retry-thread patterns are gone.

### Task 6: Final verification

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_stub.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_pending_inline_hook_registry.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\examples\native_hook\nook_native_verify_password_inline_test\payload.cpp`

**Step 1: Run host tests**

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_deferred.o
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_pending_inline_hook_registry.cpp .\src\native_hook\inline_hook\pending_inline_hook_registry.cpp .\src\native_hook\core\module_match.cpp -o .\tests\headers\test_pending_inline_hook_registry.exe
cmd /c ".\tests\headers\test_pending_inline_hook_registry.exe & echo EXIT:%ERRORLEVEL%"
```

Expected: PASS.

**Step 2: Run existing header/stub tests**

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_stub.cpp -o .\tests\headers\test_native_hook_stub.exe
cmd /c ".\tests\headers\test_native_hook_stub.exe & echo EXIT:%ERRORLEVEL%"
```

Expected: PASS.

**Step 3: Run Android arm64 build**

```powershell
E:\SDK\ndk\25.1.8937393\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\build\android\Android.mk NDK_APPLICATION_MK=.\build\android\Application.mk APP_ABI=arm64-v8a -B -j4
```

Expected: PASS and produce updated test shared objects.

**Step 4: Runtime validation**

Push the rebuilt verify-password inline test `.so` to the injector directory and confirm logs show:

- deferred registration at constructor time
- install after `libnative-lib.so` load
- replacement hit during login

