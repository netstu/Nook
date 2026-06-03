# Symbi Auto Restore Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace `--spawn-symbi`'s manual `Ctrl+C` cleanup flow with an automatic callback-driven restore flow.

**Architecture:** Keep the current zymbiote-style `setArgV0` slot patching model, but add a lightweight stub-to-host callback over a local abstract Unix socket. The injector will patch zygote, start the target app, wait for one callback or timeout, and then always restore the original zygote slot and shellcode page.

**Tech Stack:** C++17, Android NDK, local Unix sockets, existing symbi stub binary generation flow

---

### Task 1: Add callback metadata to the symbi stub

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Ninjector/jni/symbi/stub_src/stub.h`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Ninjector/jni/symbi/stub_src/stub.c`

**Step 1: Write the failing behavior target**

Expected runtime behavior:

- when the target app hits the patched `setArgV0` path
- the stub sends one callback message containing at least `pid`, `ppid`, and `dlopen` result state
- the stub still calls the original `setArgV0`

**Step 2: Add minimal callback fields**

Add to `TStub`:

- `char socket_name[64]`
- `pid_t (*getpid)()`
- `pid_t (*getppid)()`

Reuse existing:

- `socket`
- `connect`
- `write`
- `close`

**Step 3: Implement minimal callback send path**

In `stub_replacement_set_argv0(...)`:

- keep current original-call-first ordering
- on target UID hit, connect to abstract socket
- send a compact JSON payload with `pid`, `ppid`, package name, and `dlopen` success
- close socket

**Step 4: Rebuild generated stub payload**

Regenerate:

- `E:/Learn/my_program/all_my_hook/kanxue/Ninjector/jni/symbi/stub_src/generated_stub.h`

**Step 5: Verify payload marker still exists**

Check:

- marker string still present
- generated header updates size if needed

### Task 2: Replace Ctrl+C wait with callback-driven wait in the injector

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Ninjector/jni/symbi/symbi_injector.cpp`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Ninjector/jni/symbi/symbi_injector.h`
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Ninjector/jni/main.cpp`

**Step 1: Add local callback wait helper**

Implement a helper in the symbi injector that:

- creates an abstract Unix socket listener
- generates a unique socket name
- waits for one callback with timeout
- returns the callback payload string and parsed child pid if available

**Step 2: Fill stub callback config**

When building `stub_copy`, also fill:

- `socket_name`
- remote `getpid`
- remote `getppid`

**Step 3: Convert the main flow to transactional behavior**

Change `inject_spawn_symbi_by_pids(...)` from:

- patch
- start app
- wait forever for `Ctrl+C`
- restore

To:

- create callback listener
- patch zygote
- start app
- wait for one callback or timeout
- always restore
- return success only if patch/start/restore all succeed, and callback success if required

**Step 4: Remove signal-based lifetime dependency**

Delete or stop using:

- `g_keep_running`
- `signal_handler`
- `signal(SIGINT, ...)`
- `while (g_keep_running) sleep(...)`

**Step 5: Keep failure cleanup strict**

Ensure restore is attempted for:

- app start failure
- callback timeout
- callback parse failure

### Task 3: Update docs and validation notes

**Files:**
- Modify: `E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/code_review.md`
- Optionally modify: `E:/Learn/my_program/all_my_hook/kanxue/Ninjector/BLOG_SPAWN_TWO_SCHEMES_CN.md`

**Step 1: Document behavior change**

Record that `--spawn-symbi` now:

- auto-restores after callback or timeout
- no longer depends on foreground `Ctrl+C`

**Step 2: Document remaining boundary**

State clearly:

- this is still a Ninjector-side transaction
- it is not yet the final Nook server-managed Frida-style spawn gate

**Step 3: Add validation commands**

Document commands and expected evidence:

- start `--spawn-symbi`
- observe callback success log
- observe restore completion log
- confirm second app launch no longer inherits stale patch

### Task 4: Local verification

**Files:**
- Verify generated files and source compile cleanly

**Step 1: Rebuild stub payload**

Run the local stub generation flow and verify `generated_stub.h` changed.

**Step 2: Rebuild Ninjector**

Run the existing NDK build command for `Ninjector`.

**Step 3: Sanity-check logs**

Expected new logs:

- callback listener ready
- callback payload received
- restore complete

**Step 4: Push to device for user validation**

User-facing validation should confirm:

- no manual `Ctrl+C` needed
- spawn path still injects successfully
- zygote patch gets cleaned automatically
