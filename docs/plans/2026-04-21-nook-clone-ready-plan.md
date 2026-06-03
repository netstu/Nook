# Nook Clone-Ready Cleanup Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the Nook repository understandable and runnable for a new external user after clone, without relying on prior local context.

**Architecture:** Keep the current code layout and public API split, but remove environment-specific assumptions, clarify the supported workflows, and make the repository self-describing. Prioritize documentation and packaging consistency before adding new features.

**Tech Stack:** C++17, Android NDK, Java Hook, PLT Hook, arm64 Inline Hook, Android shared libraries, Ninjector-based injection workflow.

---

## Scope

This plan focuses on "clone-ready" usability, not new hook capabilities. The target outcome is:

- A new user can understand the repository structure in one pass.
- A new user can build the Android artifacts without editing hard-coded local paths.
- A new user can distinguish the supported workflows:
  - app-side `System.loadLibrary(...)`
  - payload injection via `Ninjector`
  - direct use of public `Nook*` APIs
- A new user can identify which modules are stable, experimental, or workflow-specific.

---

## Priority 0: Define the External User Story

### Checklist

- Decide whether the repository is primarily:
  - a reusable framework
  - an internal research project
  - a reference implementation for your own injector workflow
- Freeze the current support statement:
  - Java Hook: supported
  - PLT Hook: supported
  - Inline Hook arm64: supported for current workflow
  - arm32 Inline Hook: not yet supported
- Add one explicit sentence for each feature level:
  - stable
  - usable with caveats
  - planned only

### Output

- One "Current Support Matrix" section in `README.md`
- One "Non-goals / Current Limits" section in `README.md`

---

## Priority 1: Rewrite README as a True Quick Start

### Problems to fix

- Hard-coded local NDK path
- Garbled Chinese text
- Missing inline runtime/probe explanation
- Missing artifact list
- No end-to-end usage guide for different loading paths

### Checklist

- Replace hard-coded NDK path with parameterized examples:
  - `ndk-build`
  - optional `NDK_HOME`
  - optional `ANDROID_NDK_HOME`
- Add a top-level quick-start sequence:
  1. clone
  2. build
  3. produced libraries
  4. choose usage mode
- Add a workflow section for each mode:
  - `System.loadLibrary(...)`
  - `Ninjector`
  - custom payload development
- For app-side loading, document:
  - must load: `c++_shared`, `nook`, payload so
  - payload-internal probe loading is automatic
- For `Ninjector`, document:
  - injected payload so
  - required runtime so files in `/data/local/tmp/Ninjector`
  - `libnook_inline_observer_probe.so` is required but not directly injected
- Add complete artifact list:
  - `libnook.so`
  - `libnook_inline_observer_probe.so`
  - example payloads
- Add a minimal inline example and minimal PLT example

### Output

- Clean rewritten `README.md`

---

## Priority 2: Clarify Public API Semantics

### Problems to fix

- `NookNativeHook*` currently looks like a unified native API, but implementation still maps to PLT only.
- New users can easily assume `NookNativeHookHookSymbol()` also covers inline behavior.

### Checklist

- Decide one of these directions:

Option A: Keep `NookNativeHook` as PLT facade for now
- Document explicitly that `NookNativeHook*` currently routes to PLT hook only
- Mark inline usage as requiring `NookInlineHook*`

Option B: Turn `NookNativeHook` into a real strategy facade
- Add API or enum to choose `PLT` vs `INLINE`
- Keep old entry as compatibility wrapper

- Add an API comparison table:
  - `NookPltHookSymbol`
  - `NookInlineHookAddress`
  - `NookInlineHookSymbol`
  - `NookInlineHookSymbolDeferred`
  - `NookNativeHookHookSymbol`
- Add one short usage example per public header

### Recommended direction

- Use Option A first.

Reason:
- Lowest churn
- Matches current implementation truth
- Avoids premature abstraction before arm32 inline is added

### Output

- Header comments in public headers
- README API table
- Optional `docs/api/native-hook.md`

---

## Priority 3: Remove Workflow-Specific Hardcoding from Examples

### Problems to fix

- Example runtime loader currently hardcodes:
  - `/data/local/tmp/Ninjector/libnook.so`
  - `/data/local/tmp/Ninjector/libc++_shared.so`
- This is correct for your injector workflow, but not a general framework default.

### Checklist

- Split "framework sample loader" from "Ninjector sample loader"
- Define loader policy clearly:
  - prefer already-loaded runtime in-process
  - fallback to explicit injected runtime path when needed
- Move injector-specific defaults into a named config block or sample-only header
- Keep probe loading internal, but document it
- Make sure example code communicates:
  - which parts are framework behavior
  - which parts are sample convenience

### Recommended direction

- Keep `examples/native_hook/common/nook_runtime_loader.h` as sample-only code, not framework contract.
- Rename or comment it as example runtime glue.

### Output

- Cleaner sample loader semantics
- Fewer surprises for external users

---

## Priority 4: Clean Repository Layout

### Problems to fix

- Build artifacts and temporary files live in repo root
- The repository currently exposes local/debug leftovers that confuse new users

### Checklist

- Remove or ignore root-level temporary files:
  - `tmp_device_ninjector_libnook.so`
  - `tmp_device_verify_payload.so`
  - local scratch files
- Ensure generated folders are ignored or excluded from source expectations:
  - `libs/`
  - `obj/`
- Verify `.gitignore` covers:
  - Android NDK outputs
  - local temp binaries
  - editor-specific scratch files
- Keep only source, docs, tests, and intentional examples visible

### Output

- Cleaner root directory
- Better first impression for cloned repo users

---

## Priority 5: Package Examples by Use Case

### Problems to fix

- Examples exist, but the usage story is still implicit
- New users cannot quickly tell which example matches which workflow

### Checklist

- Group examples by category:
  - Java Hook examples
  - PLT Hook examples
  - Inline Hook examples
- Add one-line explanation to each example directory:
  - target
  - loading mode
  - expected behavior
- Add one dedicated "recommended first example"
- Add expected test result for each example

### Output

- Example index in `README.md`
- Optional `examples/README.md`

---

## Priority 6: Add a Support Matrix

### Checklist

- Document supported architecture:
  - arm64: yes
  - arm32: planned / pending
- Document supported hook types:
  - Java Hook
  - PLT Hook
  - Inline Hook
- Document supported loading modes:
  - App load
  - Injector load
- Document known caveats:
  - inline deferred install currently depends on runtime and probe artifacts
  - examples may rely on injected runtime path unless generalized

### Output

- Simple matrix in `README.md`

---

## Priority 7: Make Verification Repeatable

### Checklist

- Add a "Verification" section with exact commands for:
  - host header tests
  - Android NDK build
  - example payload generation
- Add expected outputs
- Add one example log sequence for a successful inline deferred install:
  - observer init success
  - `soinfo offsets ready`
  - `pending install ... status=0`
  - replacement function log

### Output

- Repeatable smoke-test instructions for external users

---

## Recommended Execution Order

1. Rewrite `README.md`
2. Add support matrix and workflow quick-start
3. Clarify `NookNativeHook` semantics
4. Separate sample runtime-loader assumptions from framework assumptions
5. Clean repository root and ignore generated artifacts
6. Add example index
7. Add verification section

---

## Minimum Bar for "Clone-Ready"

The repository should not be considered clone-ready until all of the following are true:

- No absolute local machine paths are required by docs
- README text is readable and current
- A new user can tell which API to use for PLT vs inline
- The role of `libnook_inline_observer_probe.so` is documented
- The role of `/data/local/tmp/Ninjector` is documented as workflow-specific, not universal
- Root directory does not contain misleading temp/debug artifacts
- At least one documented smoke test is reproducible end-to-end

---

## Final Assessment

Current state:

- Codebase organization: good
- Public API split: mostly good
- New-user onboarding: not yet sufficient
- Clone-ready status: not yet

The shortest path to clone-ready is not more hook-core work. It is documentation cleanup, API truthfulness, workflow separation, and repository hygiene.
