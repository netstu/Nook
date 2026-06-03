# Nook Frida-Labs Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a dedicated Nook test suite that maps all 11 upstream Frida-Labs APK challenges to Nook scripts, per-challenge runbooks, and a manifest.

**Architecture:** Keep the suite isolated under `tests/test_lab/nook-frida-labs/`. Represent each challenge with one script and one short README, and maintain one manifest for machine-readable indexing. Use one unittest for structural verification because device execution is outside this workspace.

**Tech Stack:** Nook JavaScript runtime, Python `unittest`, Markdown, JSON

---

### Task 1: Add structural verification

**Files:**
- Create: `tests/test_lab/test_nook_frida_labs.py`

**Step 1: Write the failing test**

Add a unittest that expects:
- `tests/test_lab/nook-frida-labs/README.md`
- `tests/test_lab/nook-frida-labs/manifest.json`
- 11 manifest entries
- one `script.js` and one `README.md` per challenge directory

**Step 2: Run test to verify it fails**

Run:

```powershell
python -m unittest discover -s tests/test_lab -p 'test_nook_frida_labs.py' -v
```

Expected: FAIL because the suite directory does not exist yet.

**Step 3: Write minimal implementation**

Create the suite root, manifest, and per-challenge directories/files.

**Step 4: Run test to verify it passes**

Run:

```powershell
python -m unittest discover -s tests/test_lab -p 'test_nook_frida_labs.py' -v
```

Expected: PASS

### Task 2: Add suite index

**Files:**
- Create: `tests/test_lab/nook-frida-labs/README.md`
- Create: `tests/test_lab/nook-frida-labs/manifest.json`

**Step 1: Write the data**

Document:
- challenge id
- package
- script path
- recommended `nook-cli` command
- expected effect
- status
- notes

**Step 2: Keep it minimal**

Avoid extra metadata that is not needed by the current suite.

### Task 3: Add challenge scripts and runbooks

**Files:**
- Create: `tests/test_lab/nook-frida-labs/frida-0x1/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x2/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x3/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x4/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x5/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x6/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x7/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x8/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0x9/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0xA/*`
- Create: `tests/test_lab/nook-frida-labs/frida-0xB/*`

**Step 1: Implement supported Java mappings**

Add Nook scripts for:
- method hook
- static call
- static field write
- object construction
- `Java.choose(...)`
- object-argument call

**Step 2: Implement supported native mappings**

Add Nook scripts for:
- `Interceptor.attach(...)`
- `retval.replace(...)`
- `NativeFunction`

**Step 3: Mark partial parity honestly**

Document:
- `frida-0x7` as outcome-equivalent but not constructor-hook-verified
- `frida-0xB` as outcome-equivalent through `Memory.patchCode(...)`

### Task 4: Record design context

**Files:**
- Create: `docs/plans/2026-04-29-nook-frida-labs-design.md`

**Step 1: Save the agreed design**

Capture:
- directory layout
- status model
- behavior-parity rule
- verification rule
