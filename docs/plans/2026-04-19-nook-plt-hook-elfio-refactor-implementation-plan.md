# Nook PLT Hook ELFIO Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor `Nook`'s PLT/GOT hook path with a low-risk migration that fixes current correctness issues first, then replaces handwritten ELF metadata parsing with `ELFIO` while preserving the runtime patch model and public API.

**Architecture:** Keep `NookNativeHookHookSymbol()` as the stable entry point. Split the internals into a runtime patch layer and an `ELFIO`-based metadata parser. Use the old `ElfReader` only as a temporary compatibility layer during migration.

**Tech Stack:** C++, Android NDK, Android.mk, CMake, ELFIO, ELF dynamic symbols, relocation parsing, GOT/PLT slot patching

---

### Task 1: Lock down the current PLT hook behavior with focused regression tests

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_runtime_patch.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_native_hook_manifest.txt`

**Step 1: Write the failing test**

Add targeted tests that model:

- multiple relocation entries where the match is not the first entry
- repeated patching of the same slot
- preservation of the original function pointer value

**Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_runtime_patch.cpp -o .\tests\headers\test_native_hook_runtime_patch.exe`

Expected: FAIL because the runtime patch helper abstractions and safer iteration behavior do not exist yet.

**Step 3: Write minimal implementation**

Create only the minimum scaffolding needed for the tests to build and expose the existing weaknesses.

**Step 4: Run test to verify it passes or fails for the right reason**

Run: `.\tests\headers\test_native_hook_runtime_patch.exe`

Expected: FAIL on the current behavior before the bug fixes are applied.

**Step 5: Commit**

```bash
git add tests/headers/test_native_hook_runtime_patch.cpp tests/structure/test_native_hook_manifest.txt
git commit -m "test: add native hook runtime regression coverage"
```

### Task 2: Fix the existing correctness issues before any parser refactor

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elf_reader.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\tools.cpp`

**Step 1: Write the failing test**

Use the regression tests from Task 1 plus a small host-side guard for `get_module_base()` failure handling.

**Step 2: Run test to verify it fails**

Run:
- `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_runtime_patch.cpp -o .\tests\headers\test_native_hook_runtime_patch.exe`
- `.\tests\headers\test_native_hook_runtime_patch.exe`

Expected: FAIL due to current relocation iteration and safety issues.

**Step 3: Write minimal implementation**

Apply the smallest safe fixes:

- correct the non-PLT relocation iteration to use the current entry
- guard `fclose()` behind a null check
- calculate the correct page protection range
- restore original page permissions after patching

**Step 4: Run test to verify it passes**

Run:
- `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_runtime_patch.cpp -o .\tests\headers\test_native_hook_runtime_patch.exe`
- `.\tests\headers\test_native_hook_runtime_patch.exe`

Expected: PASS.

**Step 5: Commit**

```bash
git add src/native_hook/elfhooker/elf_reader.cpp src/native_hook/elfhooker/tools.cpp tests/headers/test_native_hook_runtime_patch.cpp
git commit -m "fix: harden current native plt hook implementation"
```

### Task 3: Introduce an ELFIO metadata parser with parser-only tests

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elfio_image_parser.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elfio_image_parser.cpp`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_elfio_image_parser.cpp`

**Step 1: Write the failing test**

Add parser-only tests that:

- load a known ELF file
- find a target symbol in `.dynsym`
- enumerate relocation entries
- return relocation records that reference the requested symbol

**Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I .\include -I .\src -I .\third_party\elfio .\tests\headers\test_elfio_image_parser.cpp -o .\tests\headers\test_elfio_image_parser.exe`

Expected: FAIL because `elfio_image_parser` does not exist yet.

**Step 3: Write minimal implementation**

Implement a metadata-only parser that returns:

- symbol index or symbol metadata
- matching relocation entries with offsets and types

Do not add runtime patching responsibilities.

**Step 4: Run test to verify it passes**

Run:
- `g++ -std=c++17 -I .\include -I .\src -I .\third_party\elfio .\tests\headers\test_elfio_image_parser.cpp -o .\tests\headers\test_elfio_image_parser.exe`
- `.\tests\headers\test_elfio_image_parser.exe`

Expected: PASS.

**Step 5: Commit**

```bash
git add src/native_hook/elfhooker/elfio_image_parser.* tests/headers/test_elfio_image_parser.cpp
git commit -m "feat: add elfio metadata parser for native hook relocations"
```

### Task 4: Extract the runtime patch logic into a dedicated helper

**Files:**
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\runtime_patch.h`
- Create: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\runtime_patch.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elf_reader.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_runtime_patch.cpp`

**Step 1: Write the failing test**

Point the runtime patch tests at the new helper interface.

**Step 2: Run test to verify it fails**

Run:
- `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_runtime_patch.cpp -o .\tests\headers\test_native_hook_runtime_patch.exe`

Expected: FAIL because `runtime_patch.*` is not wired yet.

**Step 3: Write minimal implementation**

Move the page-protection and slot-write logic out of `ElfReader::hookInternally()` into a dedicated helper. Keep behavior unchanged.

**Step 4: Run test to verify it passes**

Run:
- `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_runtime_patch.cpp -o .\tests\headers\test_native_hook_runtime_patch.exe`
- `.\tests\headers\test_native_hook_runtime_patch.exe`

Expected: PASS.

**Step 5: Commit**

```bash
git add src/native_hook/elfhooker/runtime_patch.* src/native_hook/elfhooker/elf_reader.cpp tests/headers/test_native_hook_runtime_patch.cpp
git commit -m "refactor: extract native hook runtime patch helper"
```

### Task 5: Switch the main hook path to ELFIO metadata plus runtime patch

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\NookNativeHook.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elf_reader.h`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\src\native_hook\elfhooker\elf_reader.cpp`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`

**Step 1: Write the failing test**

Add an integration-focused host test that exercises:

- module path resolution
- parser result handling
- runtime slot address calculation
- original function capture

**Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I .\include -I .\src -I .\third_party\elfio .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_native.o`

Expected: FAIL until the new internal wiring is complete.

**Step 3: Write minimal implementation**

Update `NookNativeHookHookSymbol()` to:

- resolve the loaded module base and path
- invoke the `ELFIO` metadata parser
- compute runtime slot addresses from relocation offsets
- delegate the final patching to `runtime_patch`

Keep the public function signature unchanged.

**Step 4: Run test to verify it passes**

Run:
- `g++ -std=c++17 -I .\include -I .\src -I .\third_party\elfio .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_native.o`

Expected: PASS.

**Step 5: Commit**

```bash
git add src/native_hook/NookNativeHook.cpp src/native_hook/elfhooker/elf_reader.* src/native_hook/elfhooker/runtime_patch.* src/native_hook/elfhooker/elfio_image_parser.* tests/headers/test_public_headers.cpp
git commit -m "refactor: switch native plt hook to elfio metadata path"
```

### Task 6: Update build files and verify Android integration

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\Android.mk`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\build\android\CMakeLists.txt`
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\structure\test_native_hook_manifest.txt`

**Step 1: Write the failing test**

Add the new helper/parser files to the structure manifest and verify both build systems are missing them.

**Step 2: Run test to verify it fails**

Run:
- `Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
- `Select-String -Path .\build\android\Android.mk,.\build\android\CMakeLists.txt -Pattern 'elfio_image_parser|runtime_patch'`

Expected: FAIL or no matches before build files are updated.

**Step 3: Write minimal implementation**

Add the new sources and include paths to both Android build entry points.

**Step 4: Run test to verify it passes**

Run:
- `Get-Content .\tests\structure\test_native_hook_manifest.txt | ForEach-Object { if (-not (Test-Path $_)) { Write-Error $_ } }`
- `Select-String -Path .\build\android\Android.mk,.\build\android\CMakeLists.txt -Pattern 'elfio_image_parser|runtime_patch'`

Expected: PASS with all files present and both build files updated.

**Step 5: Commit**

```bash
git add build/android/Android.mk build/android/CMakeLists.txt tests/structure/test_native_hook_manifest.txt
git commit -m "build: wire elfio native hook refactor sources"
```

### Task 7: Run end-to-end verification

**Files:**
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_native_hook_runtime_patch.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_elfio_image_parser.cpp`
- Test: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\headers\test_public_headers.cpp`
- Test: existing Android payload integration path used for `strcmp`

**Step 1: Write the failing test**

Reuse the regression, parser, and API compilation tests written in earlier tasks.

**Step 2: Run test to verify it fails**

Run the relevant host-side tests before the final wiring is complete and observe the expected failures.

**Step 3: Write minimal implementation**

Finish any remaining gaps in file wiring, parser output conversion, or runtime patch delegation.

**Step 4: Run test to verify it passes**

Run:
- `g++ -std=c++17 -I .\include -I .\src .\tests\headers\test_native_hook_runtime_patch.cpp -o .\tests\headers\test_native_hook_runtime_patch.exe`
- `.\tests\headers\test_native_hook_runtime_patch.exe`
- `g++ -std=c++17 -I .\include -I .\src -I .\third_party\elfio .\tests\headers\test_elfio_image_parser.cpp -o .\tests\headers\test_elfio_image_parser.exe`
- `.\tests\headers\test_elfio_image_parser.exe`
- `g++ -std=c++17 -I .\include -I .\src -I .\third_party\elfio .\tests\headers\test_public_headers.cpp -c -o .\tests\headers\test_public_headers_native.o`
- Android build verification command for the current Nook build entry point
- Existing target-app injection verification for the `strcmp` payload

Expected: PASS for host-side tests, successful Android build, and successful `strcmp` hook integration.

**Step 5: Commit**

```bash
git add include src build tests docs/plans
git commit -m "refactor: complete elfio-backed native plt hook migration"
```
