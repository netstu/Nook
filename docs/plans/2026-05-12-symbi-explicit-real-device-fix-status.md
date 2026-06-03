# 2026-05-12 Symbi Explicit Real-Device Fix Status

## Conclusion

`--spawn-symbi` on the Android 11 real device is now working again for the tested `frida0x1` flow.

The important point is that the fix was not in CLI routing. The CLI flag was already reaching the server correctly. The real failure was in the runtime delivery mode used by explicit `symbi`.

## Root Cause

The previous explicit `symbi` path used the embedded-agent route inside `SpawnViaSymbiEmbedded()`:

1. create embedded `libnook-agent` as a memfd inside `zygote`
2. patch zygote and trigger fork
3. let the child later use that fd-backed payload

On this Android 11 Xiaomi device, zygote fd whitelist enforcement rejected that extra memfd during fork.

The real-device crash log was explicit:

- `JNI FatalError called: (zygote) Not whitelisted (52): /memfd:libnook-agent (deleted)`

This means the failure happened before host-side script lifecycle or spawn fallback semantics could matter.

## Symptom Chain

What the user saw:

- explicit `--spawn-symbi` failed
- error text often ended with:
  - `spawn_symbi_failed:restore_original_slot_failed`
  - `fallback failed: start_target_app failed`
- device could reboot or restart critical services

Why that happened:

- zygote crashed during fork-time fd whitelist validation
- the symbi patch/restore flow then became incomplete
- subsequent fallback behavior was operating after zygote corruption/crash, so it was not trustworthy

## Fix Applied

The fix happened in two stages.

### Stage 1

First, explicit `symbi` was recovered by stopping the old zygote-owned embedded agent memfd path and temporarily switching to runtime file-backed delivery.

That intermediate behavior was:

1. keep single-file deployment on device
2. materialize `libnook-agent.so` into runtime dir during execution
3. pass that file path into `spawn_symbi(...)`

That removed the zygote whitelist crash and restored real-device usability.

### Stage 2

Then the path was tightened further into a true child-owned embedded route.

Current explicit `symbi` behavior is now:

1. keep single-file deployment on device:
   - only visible file is still `nook-server`
2. use `SpawnViaSymbiEmbedded()` again
3. do not create agent memfd in zygote before fork
4. let symbi create and stop the child first
5. inject the embedded agent into the stopped child through the child-owned memfd injection path

In other words:

- deployment remains single-file
- runtime execution is no longer file-backed
- zygote no longer carries the agent memfd across fork
- explicit `symbi` is now back on an embedded route without reintroducing the Android 11 whitelist crash

## Real-Device Confirmation

The final successful run showed:

- `explicit symbi spawn requested; skip zygote control`
- `symbi: prepared ... so=__embedded_remote_fd__`
- `symbi: start app force-stop ret=0 start ret=0`
- `symbi: callback handshake received ... load_ok=1`
- `symbi: restore complete ...`
- `spawn success pkg=com.ad2001.frida0x1 pid=...`
- `forward SCRIPT_CREATE`
- `forward SCRIPT_LOAD_RESP`
- `resume success`

Device runtime directory confirmation:

- only `nook-server` remained visible
- no runtime `libnook-agent.so` remained on disk

Most importantly:

- the successful path no longer showed a zygote crash involving `/memfd:libnook-agent (deleted)`
- the successful path also no longer used `so=/data/local/tmp/nook/libnook-agent.so`

## Current Status

After this fix:

- default stable spawn: working
- explicit `--spawn-symbi`: working on the tested real-device flow
- device-side deployment: still single visible file
  - `nook-server`

But the architecture status is:

- default stable spawn remains the production-safe path
- explicit `symbi` is now usable for testing and comparison
- explicit `symbi` is now using a child-owned embedded delivery path
- this is materially closer to the intended Frida-style direction than the temporary file-backed stage

## Boundary

This is not yet the final Frida-style end state.

What is true now:

- single-file deployment is preserved
- explicit `symbi` no longer corrupts zygote through embedded agent memfd
- explicit `symbi` no longer depends on runtime temporary `libnook-agent.so`
- child-side embedded injection is working on the tested real-device flow

What is not true yet:

- this has only been validated on the currently tested Android 11 real-device path
- the session/state model still has some diagnostic complexity during symbi handoff
- it still needs wider regression coverage before being considered a production default backend

## Session Notes

The successful run showed two `agent connected` events for the same child pid.

This is not currently treated as a failure.

Observed sequence:

1. child appears during symbi callback handoff
2. server resolves a pending spawn entry and sees an early agent-ready shape
3. after child-side embedded injection and final runtime initialization, the same pid connects again as the fully initialized app-side agent session

The practical effect is:

- host still ends up bound to the correct child pid
- cached/replayed `AGENT_READY` and later script lifecycle still complete successfully

So this is currently a state-machine/observability nuance, not a functional regression.

## Recommended Next Step

The next step should no longer be "make child-owned memfd work", because that part is now working on the tested path.

The next step should be:

- document and tighten the symbi session/state transitions
- add focused regression coverage for repeated explicit `--spawn-symbi` runs
- only then decide whether explicit `symbi` is ready for broader default-path consideration
