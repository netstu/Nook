# Nook PLT Hook Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add PLT hook support to `Nook` by integrating the backup `elfhooker` implementation under `src/native_hook/` and exposing a stable `NookNativeHook` API.

**Architecture:** Keep the public API in `include/nook/NookNativeHook.h`, implement the adapter in `src/native_hook/NookNativeHook.cpp`, and place the migrated ELF parsing and GOT/PLT patching code under `src/native_hook/elfhooker/`. Update both Android build entry points so the new native hook sources build with the existing framework.

**Tech Stack:** C++, Android NDK, Android.mk, CMake, ELF parsing, PLT/GOT rewrite

---

### Task 1: Document the native hook file set

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_native_hook_manifest.txt`

**Step 1: Write the failing test**

Create a manifest listing the expected native hook files, including `src/native_hook/elfhooker/*` and `src/native_hook/NookNativeHook.cpp`.

**Step 2: Run test to verify it fails**

Run: `Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: FAIL because `src/native_hook/elfhooker/` and related files do not exist yet.

**Step 3: Write minimal implementation**

Create the missing files under `src/native_hook/elfhooker/`.

**Step 4: Run test to verify it passes**

Run: `Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: PASS with no missing paths.

**Step 5: Commit**

```bash
git add tests/structure src/native_hook
git commit -m "feat: add native hook source layout"
```

### Task 2: Expand the public native hook API

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\include\nook\NookNativeHook.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_stub.cpp`

**Step 1: Write the failing test**

Update the header test to compile against a new `NookNativeHookHookSymbol(...)` declaration.

```cpp
#include "nook/NookNativeHook.h"

int main() {
    int available = 0;
    void* original = nullptr;
    NookStatus status = NookNativeHookIsAvailable(&available);
    NookStatus hook_status = NookNativeHookHookSymbol("libc.so", "open", reinterpret_cast<void*>(1), &original);
    return (status <= 0 && hook_status <= 0) ? 0 : 1;
}
```

**Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I .\include .\tests\headers\test_native_hook_stub.cpp -c -o .\tests\headers\test_native_hook_stub.o`
Expected: FAIL because `NookNativeHookHookSymbol` is not declared yet.

**Step 3: Write minimal implementation**

Add the function declaration to `include/nook/NookNativeHook.h`.

**Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -I .\include .\tests\headers\test_native_hook_stub.cpp -c -o .\tests\headers\test_native_hook_stub.o`
Expected: PASS.

**Step 5: Commit**

```bash
git add include/nook/NookNativeHook.h tests/headers/test_native_hook_stub.cpp
git commit -m "feat: declare native plt hook api"
```

### Task 3: Migrate and adapt elfhooker internals

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\def.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elf_arm.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elf_reader.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elf_reader.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\logger.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\tools.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\tools.cpp`

**Step 1: Write the failing test**

Use the manifest test from Task 1 to prove these files are missing first.

**Step 2: Run test to verify it fails**

Run: `Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: FAIL on the elfhooker files.

**Step 3: Write minimal implementation**

Copy the backup files into `src/native_hook/elfhooker/` and make only the minimal path, naming, and compatibility fixes needed by the current project.

**Step 4: Run test to verify it passes**

Run: `Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
Expected: PASS.

**Step 5: Commit**

```bash
git add src/native_hook/elfhooker tests/structure/test_native_hook_manifest.txt
git commit -m "feat: migrate elfhooker into native hook module"
```

### Task 4: Implement the Nook native hook adapter

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\NookNativeHook.cpp`

**Step 1: Write the failing test**

Use the updated header compilation test to require the new API surface and current implementation contract.

**Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_native.o`
Expected: FAIL if the implementation introduces include issues or missing declarations.

**Step 3: Write minimal implementation**

Implement:
- `NookNativeHookInitialize`
- `NookNativeHookIsAvailable`
- `NookNativeHookHookSymbol`

Wire the new hook API to the internal `ElfReader`.

**Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_native.o`
Expected: PASS.

**Step 5: Commit**

```bash
git add src/native_hook/NookNativeHook.cpp
git commit -m "feat: implement nook native plt hook adapter"
```

### Task 5: Update the Android build entry points

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\CMakeLists.txt`

**Step 1: Write the failing test**

Check the build files for the new `src/native_hook/elfhooker/*` sources before modifying them.

**Step 2: Run test to verify it fails**

Run: `Select-String -Path .\build\android\Android.mk,.\build\android\CMakeLists.txt -Pattern 'elfhooker|elf_reader|tools.cpp'`
Expected: FAIL with no matches.

**Step 3: Write minimal implementation**

Add the elfhooker include path and source files to both build systems.

**Step 4: Run test to verify it passes**

Run: `Select-String -Path .\build\android\Android.mk,.\build\android\CMakeLists.txt -Pattern 'elfhooker|elf_reader|tools.cpp'`
Expected: PASS with matches in both files.

**Step 5: Commit**

```bash
git add build/android/Android.mk build/android/CMakeLists.txt
git commit -m "build: include native plt hook sources"
```

### Task 6: Verify the focused test surface

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_native_hook_manifest.txt`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_stub.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`

**Step 1: Write the failing test**

Reuse the targeted structure and compile tests added in earlier tasks.

**Step 2: Run test to verify it fails**

Run the same checks before implementation changes and observe failures.

**Step 3: Write minimal implementation**

Finish any missing declaration, include path, or adapter issues required to satisfy the focused test set.

**Step 4: Run test to verify it passes**

Run:
- `Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
- `g++ -std=c++17 -I .\include .\tests\headers\test_native_hook_stub.cpp -c -o .\tests\headers\test_native_hook_stub.o`
- `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_native.o`

Expected: PASS for all three commands.

**Step 5: Commit**

```bash
git add include src build tests docs/plans
git commit -m "feat: add plt hook support to nook native hook"
```
