# Spawn Symbi Explicit Flag Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `--spawn-symbi` as an explicit CLI switch so users can force `symbi` as the preferred `spawn` backend without changing the current default stable backend.

**Architecture:** Reuse existing `SpawnRequest.argv` instead of adding a new protocol field. Host CLI appends a reserved argv marker, and server-side spawn selection extracts it before deciding backend order.

**Tech Stack:** Python CLI (`host/nook-py`), existing host spawn request flow, C++ `NinjectorSpawnInjector`, current spawn backend selection logic.

---

### Task 1: Add failing CLI parsing tests

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\tests\test_cli.py`

**Step 1: Write failing tests**

- Add tests showing `spawn --spawn-symbi`
- Add tests showing Frida-style `-f ... --spawn-symbi`
- Assert the resulting spawn request argv contains the reserved marker

**Step 2: Run to verify failure**

- Run the focused CLI test selection
- Expected: parser does not recognize `--spawn-symbi` yet

### Task 2: Add CLI option and host argv propagation

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\nook\cli.py`

**Step 1: Add parser option**

- Add `--spawn-symbi` to all spawn-capable entrypoints that should support it

**Step 2: Propagate into spawn argv**

- When spawning, append a reserved marker to `SpawnRequest.argv`
- Keep the marker internal and deterministic

### Task 3: Add failing injector tests for explicit symbi preference

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\tests\communication\test_ninjector_spawn_injector.cpp`

**Step 1: Write failing tests**

- Add a test showing explicit symbi request prefers `symbi`
- Add a test showing explicit symbi request can fallback to the stable path

### Task 4: Implement server-side explicit symbi preference

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\kanxue\Nook\server\ninjector_spawn_injector.cpp`

**Step 1: Extract reserved argv marker**

- Add a helper that detects the explicit symbi flag from `SpawnRequest.argv`

**Step 2: Change backend ordering**

- If explicit symbi requested:
  - try `symbi` first
  - only then fallback
- If not requested:
  - keep current default order

### Task 5: Verify end-to-end behavior

**Files:**
- No code changes required

**Step 1: Run focused CLI tests**

- Verify parser and argv propagation

**Step 2: Run focused injector tests**

- Verify explicit `symbi` selection semantics

**Step 3: Provide real-device command**

- Example:
  - `nook-cli -U -f com.ad2001.frida0x1 --spawn-symbi -l ...\\script.js`

