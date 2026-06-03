# Nook Arm64 Inline Hook Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first-party `arm64` inline hook backend to `Nook`, split the public API into explicit PLT and inline surfaces, and provide install plus unhook support without changing the current repository architecture.

**Architecture:** Introduce `NookPltHook` and `NookInlineHook` framework entrypoints, keep shared helpers in `src/native_hook/core/`, and build the new inline backend in `src/native_hook/inline_hook/` around hook records, trampoline allocation, arm64 instruction relocation, and target patch/restore logic.

**Tech Stack:** C++17, Android NDK, arm64/aarch64 machine code patching, `mprotect`, executable `mmap`, instruction cache flushing, existing Nook host-side compile checks, Android.mk, CMake

---

### Task 1: Lock the new public API shape with header compile checks

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookPltHook.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookInlineHook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookNativeHook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`

**Step 1: Write the failing test**

Update `test_public_headers.cpp` so it includes:

- `nook/NookPltHook.h`
- `nook/NookInlineHook.h`
- `nook/NookNativeHook.h`

and references the new public function declarations.

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_inline_api.o
```

Expected: FAIL because the new headers and symbols do not exist yet.

**Step 3: Write minimal implementation**

Create the new headers with declarations only, and reduce `NookNativeHook.h` to a compatibility wrapper or forwarding include.

**Step 4: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_inline_api.o
```

Expected: PASS.

**Step 5: Commit**

If working in a git repo:

```bash
git add include/nook/NookPltHook.h include/nook/NookInlineHook.h include/nook/NookNativeHook.h tests/headers/test_public_headers.cpp
git commit -m "feat: split public native hook headers into plt and inline APIs"
```

### Task 2: Lock the new project structure with manifest coverage

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_native_hook_manifest.txt`

**Step 1: Write the failing test**

Add expected paths for:

- `src/framework/NookPltHook.cpp`
- `src/framework/NookInlineHook.cpp`
- `src/native_hook/inline_hook/inline_hook_impl.h`
- `src/native_hook/inline_hook/inline_hook_impl.cpp`
- `src/native_hook/inline_hook/arm64_instruction_relocator.h`
- `src/native_hook/inline_hook/arm64_instruction_relocator.cpp`
- `src/native_hook/inline_hook/trampoline_allocator.h`
- `src/native_hook/inline_hook/trampoline_allocator.cpp`
- `src/native_hook/inline_hook/inline_hook_record.h`
- `src/native_hook/inline_hook/inline_hook_record.cpp`

**Step 2: Run test to verify it fails**

Run:

```powershell
Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }
```

Expected: FAIL on the new file paths.

**Step 3: Write minimal implementation**

Create empty or stub source/header files that match the target layout.

**Step 4: Run test to verify it passes**

Run:

```powershell
Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }
```

Expected: PASS.

**Step 5: Commit**

If working in a git repo:

```bash
git add tests/structure/test_native_hook_manifest.txt src/framework/NookPltHook.cpp src/framework/NookInlineHook.cpp src/native_hook/inline_hook
git commit -m "test: lock inline hook file layout"
```

### Task 3: Add hook record and trampoline allocator scaffolding

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\inline_hook_record.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\inline_hook_record.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\trampoline_allocator.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\trampoline_allocator.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_inline_hook_record.cpp`

**Step 1: Write the failing test**

Add a host-side test that validates:

- record initialization state
- storage of target/replacement/trampoline pointers
- active/inactive transitions
- trampoline allocation returns executable memory

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_inline_hook_record.cpp .\src\native_hook\inline_hook\inline_hook_record.cpp .\src\native_hook\inline_hook\trampoline_allocator.cpp -o .\tests\headers\test_inline_hook_record.exe
```

Expected: FAIL because the record and allocator are not implemented.

**Step 3: Write minimal implementation**

Implement:

- opaque record storage structure
- minimal allocation/free helpers around `mmap`/`munmap` for host-compatible builds where possible
- cache/page helper abstraction only where needed

**Step 4: Run test to verify it passes**

Run:

```powershell
.\tests\headers\test_inline_hook_record.exe
```

Expected: PASS.

**Step 5: Commit**

If working in a git repo:

```bash
git add src/native_hook/inline_hook/inline_hook_record.* src/native_hook/inline_hook/trampoline_allocator.* tests/headers/test_inline_hook_record.cpp
git commit -m "feat: add inline hook record and trampoline allocator"
```

### Task 4: Build arm64 instruction relocation with focused tests

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\arm64_instruction_relocator.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\arm64_instruction_relocator.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_arm64_instruction_relocator.cpp`

**Step 1: Write the failing test**

Add relocator tests that exercise representative encodings for:

- `ADR`
- `ADRP`
- `B`
- `BL`
- `B.cond`
- `CBZ` / `CBNZ`
- `TBZ` / `TBNZ`
- `LDR literal`

The test should verify:

- reported rewritten length
- rewrite success/failure behavior
- failure on unsupported or unsafe patterns

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_arm64_instruction_relocator.cpp .\src\native_hook\inline_hook\arm64_instruction_relocator.cpp -o .\tests\headers\test_arm64_instruction_relocator.exe
```

Expected: FAIL because the relocator does not exist yet.

**Step 3: Write minimal implementation**

Implement only the required `arm64` relocation subset for the first release. Fail fast on unsupported instructions.

**Step 4: Run test to verify it passes**

Run:

```powershell
.\tests\headers\test_arm64_instruction_relocator.exe
```

Expected: PASS.

**Step 5: Commit**

If working in a git repo:

```bash
git add src/native_hook/inline_hook/arm64_instruction_relocator.* tests/headers/test_arm64_instruction_relocator.cpp
git commit -m "feat: add arm64 inline hook instruction relocator"
```

### Task 5: Implement the inline backend install and unhook path

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\inline_hook_impl.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\inline_hook\inline_hook_impl.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_inline_hook_impl.cpp`

**Step 1: Write the failing test**

Add a host-side logic test for:

- invalid argument rejection
- hook record creation
- patched length tracking
- unhook state transition

This test does not need to execute real arm64 patched code on host. It should validate the backend state machine and byte bookkeeping.

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_inline_hook_impl.cpp .\src\native_hook\inline_hook\inline_hook_impl.cpp .\src\native_hook\inline_hook\inline_hook_record.cpp .\src\native_hook\inline_hook\trampoline_allocator.cpp .\src\native_hook\inline_hook\arm64_instruction_relocator.cpp -o .\tests\headers\test_inline_hook_impl.exe
```

Expected: FAIL because the backend orchestration does not exist yet.

**Step 3: Write minimal implementation**

Implement:

- install helper for direct address hook
- trampoline construction
- target patch write
- original byte restore on unhook
- instruction cache flush wrappers

**Step 4: Run test to verify it passes**

Run:

```powershell
.\tests\headers\test_inline_hook_impl.exe
```

Expected: PASS.

**Step 5: Commit**

If working in a git repo:

```bash
git add src/native_hook/inline_hook/inline_hook_impl.* tests/headers/test_inline_hook_impl.cpp
git commit -m "feat: add arm64 inline hook backend with unhook support"
```

### Task 6: Add framework entrypoints for explicit PLT and inline APIs

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookPltHook.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookInlineHook.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\framework\NookNativeHook.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\core\native_hook_symbol_resolver.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\core\native_hook_symbol_resolver.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_stub.cpp`

**Step 1: Write the failing test**

Update the stub/header tests to compile against:

- `NookPltHook*`
- `NookInlineHook*`
- compatibility include path through `NookNativeHook.h`

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_stub.cpp -o .\tests\headers\test_native_hook_stub.exe
```

Expected: FAIL until the new framework entrypoints are wired.

**Step 3: Write minimal implementation**

Implement:

- explicit PLT hook entrypoints that delegate to the existing PLT backend
- explicit inline entrypoints for address and symbol hook
- symbol resolution helper reused by inline symbol hook
- compatibility behavior in `NookNativeHook.cpp`

**Step 4: Run test to verify it passes**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_stub.cpp -o .\tests\headers\test_native_hook_stub.exe
.\tests\headers\test_native_hook_stub.exe
```

Expected: PASS.

**Step 5: Commit**

If working in a git repo:

```bash
git add src/framework/NookPltHook.cpp src/framework/NookInlineHook.cpp src/framework/NookNativeHook.cpp src/native_hook/core/native_hook_symbol_resolver.* tests/headers/test_native_hook_stub.cpp
git commit -m "feat: expose explicit plt and inline hook framework APIs"
```

### Task 7: Update Android build integration for the new backend

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\CMakeLists.txt`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\README.md`

**Step 1: Write the failing test**

Check that the new sources are not yet present in the Android build definitions.

**Step 2: Run test to verify it fails**

Run:

```powershell
Select-String -Path .\build\android\Android.mk,.\build\android\CMakeLists.txt -Pattern 'NookInlineHook|inline_hook_impl|arm64_instruction_relocator|trampoline_allocator'
```

Expected: no matches before updating build files.

**Step 3: Write minimal implementation**

Wire the new framework/core/inline sources into both build systems and update README public API documentation.

**Step 4: Run test to verify it passes**

Run:

```powershell
Select-String -Path .\build\android\Android.mk,.\build\android\CMakeLists.txt -Pattern 'NookInlineHook|inline_hook_impl|arm64_instruction_relocator|trampoline_allocator'
```

Expected: matches found in both build files.

**Step 5: Commit**

If working in a git repo:

```bash
git add build/android/Android.mk build/android/CMakeLists.txt README.md
git commit -m "build: wire arm64 inline hook backend into android builds"
```

### Task 8: Add Android runtime validation with a dedicated example payload

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\examples\native_hook\nook_native_inline_test\payload.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\examples\native_hook\nook_native_inline_test\target_replacement.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Optionally Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_inline_api_usage.cpp`

**Step 1: Write the failing test**

Add a minimal example build target that uses:

- `NookInlineHookAddress(...)` or `NookInlineHookSymbol(...)`
- `NookInlineUnhook(...)`

and expects the new example shared object to build.

**Step 2: Run test to verify it fails**

Run:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_inline_api_usage.cpp -c -o .\tests\headers\test_inline_api_usage.o
```

Expected: FAIL until the example and API usage are valid.

**Step 3: Write minimal implementation**

Create a deterministic arm64 test payload that:

- installs inline hook
- exercises replacement behavior
- calls `original`
- optionally unhooks and validates restoration

**Step 4: Run test to verify it passes**

Run host-side compile check:

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_inline_api_usage.cpp -c -o .\tests\headers\test_inline_api_usage.o
```

Then run Android build:

```powershell
E:\SDK\ndk\25.1.8937393\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\build\android\Android.mk NDK_APPLICATION_MK=.\build\android\Application.mk APP_ABI=arm64-v8a -B -j4
```

Expected: PASS and produce the new inline test shared object.

**Step 5: Commit**

If working in a git repo:

```bash
git add examples/native_hook/nook_native_inline_test build/android/Android.mk tests/headers/test_inline_api_usage.cpp
git commit -m "test: add arm64 inline hook runtime validation payload"
```

### Task 9: Final verification

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_native_hook_manifest.txt`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_inline_hook_record.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_arm64_instruction_relocator.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_inline_hook_impl.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`

**Step 1: Run structure verification**

```powershell
Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }
```

Expected: PASS.

**Step 2: Run host-side compile and runtime checks**

```powershell
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_inline_api.o
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_inline_hook_record.cpp .\src\native_hook\inline_hook\inline_hook_record.cpp .\src\native_hook\inline_hook\trampoline_allocator.cpp -o .\tests\headers\test_inline_hook_record.exe
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_arm64_instruction_relocator.cpp .\src\native_hook\inline_hook\arm64_instruction_relocator.cpp -o .\tests\headers\test_arm64_instruction_relocator.exe
g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_inline_hook_impl.cpp .\src\native_hook\inline_hook\inline_hook_impl.cpp .\src\native_hook\inline_hook\inline_hook_record.cpp .\src\native_hook\inline_hook\trampoline_allocator.cpp .\src\native_hook\inline_hook\arm64_instruction_relocator.cpp -o .\tests\headers\test_inline_hook_impl.exe
.\tests\headers\test_inline_hook_record.exe
.\tests\headers\test_arm64_instruction_relocator.exe
.\tests\headers\test_inline_hook_impl.exe
```

Expected: PASS.

**Step 3: Run Android arm64 build**

```powershell
E:\SDK\ndk\25.1.8937393\ndk-build.cmd NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=.\build\android\Android.mk NDK_APPLICATION_MK=.\build\android\Application.mk APP_ABI=arm64-v8a -B -j4
```

Expected: PASS.

**Step 4: Optional device-side validation**

Push the generated inline test `.so` to the device and validate install/unhook behavior through the existing injector workflow.

**Step 5: Commit**

If working in a git repo:

```bash
git add .
git commit -m "feat: add arm64 inline hook backend to nook"
```
