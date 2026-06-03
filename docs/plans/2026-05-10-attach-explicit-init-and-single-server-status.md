# Attach Explicit-Init And Single-Server Status

## Context

Recent device debugging on `com.ad2001.frida0x8` showed that Nook attach no longer failed at the raw loader stage, but still timed out waiting for `AGENT_READY`.

Observed log shape:

- `NookAgentInitialize begin process=com.ad2001.frida0x8`
- `runtime bridge ensure begin process=com.ad2001.frida0x8`
- then no `runtime bridge ensure ok`
- no `AGENT_READY sent`
- server side ended with `attach agent-ready timeout`

This happened after the remote loader path had already been aligned closer to Android's real loader entrypoints.

## What Was Fixed

Attach was changed from constructor-driven initialization to explicit initialization:

1. before attach-side `dlopen`, injector sets `NOOK_SKIP_AUTO_INIT=1` in the remote process
2. the agent image is loaded without relying on constructor-time full bootstrap
3. after load, injector explicitly calls `NookAgentInitialize`
4. this explicit call does not depend on unstable `handle + dlsym` in the null-handle case
5. injector resolves `NookAgentInitialize` by ELF export offset and maps it to the remote module base
6. `NOOK_SKIP_AUTO_INIT` is then cleared

Files changed:

- `src/framework/NookComm.cpp`
- `server/ninjector_compat.cpp`

Result:

- attach for `com.ad2001.frida0x8` resumed working
- tested hook scripts loaded and ran normally again

## Important Clarification: Yes, Embedded Agent + memfd/fd Was Already Explored

This repo already did part of the Frida-style direction earlier:

1. `nook-server` can embed the agent blob
2. server-side runtime can materialize `libnook-agent.so` beside the server automatically
3. there is also an embedded-agent injection path based on memfd/fd style staging

Relevant prior design/docs:

- `docs/plans/2026-04-29-nook-embedded-agent-delivery.md`
- `docs/plans/2026-05-09-nook-frida-style-single-server-spawn-design.md`
- `docs/plans/2026-05-09-nook-frida-style-single-server-spawn-implementation-plan.md`

## Why That Still Does Not Mean "Frida-Like Single Stable Server" Is Done

The missing piece is not "whether we ever implemented embedding".
The missing piece is "whether embedded delivery is the default stable mainline for both attach and spawn".

Current state:

### Attach

- embedded / memfd fallback exists
- but the stable path that just passed real-device testing is still the sidecar path-based load plus explicit init
- the embedded fallback path has previously hit loader/init instability such as:
  - `undefined symbol: JNI_OnLoad`
  - remote `dlopen` inconsistency
  - remote init failures in some runs

So for attach, the Frida-like embedded path exists as capability/prototype/fallback material, but is not yet the proven default stable path on this device.

### Spawn

- stable spawn still rides on the legacy `ncore` backend
- `spawn_markers` / `spawn_result.json` style coordination has been reduced, and stable flow is moving toward pure IPC
- but the zygote-side control responsibility for the stable path has not yet been fully moved out of `ncore`
- current experimental `zygote-control` path is still not stable enough to replace legacy spawn by default

So for spawn, the main blocker is not agent embedding itself, but the fact that stable zygote/fork control is still owned by `ncore`.

## Practical Status Summary

What is already true:

- user-facing deployment can be made much closer to "push one server"
- attach now has a stable explicit-init model
- embedded-agent delivery work already exists in the codebase

What is not yet true:

- attach does not yet default to a fully proven embedded memfd/fd path
- spawn does not yet have a fully stable non-`ncore` backend
- therefore Nook is not yet at the same architectural end-state as Frida's "single visible server, stable embedded runtime, stable default spawn/attach paths"

## Next Steps

To actually finish the Frida-like single-server goal, the work should stay in this order:

1. keep the new explicit-init attach path as the stable baseline
2. re-validate the embedded attach path until it can replace sidecar load as default
3. finish moving stable spawn control off `ncore`
4. only then remove `libncore.so` from the default runtime requirement
5. after both attach and spawn are stable, collapse deployment docs to true single-server wording

## Decision Boundary

Do not treat "embedded agent exists" as equivalent to "single-server runtime is complete".

The correct interpretation is:

- embedding/delivery work: done in part
- stable explicit-init attach baseline: now done
- stable Frida-like single-server runtime across attach + spawn: not done yet

## 2026-05-10 follow-up: embedded-first runtime selection

Another gap was identified in `server/server_main.cpp`:

- even after embedded agent/ncore support had been added, the server could still auto-prefer stale sidecar files if they happened to already exist in the runtime directory
- this meant the real execution path could silently drift away from the intended single-server embedded baseline

That behavior has now been tightened:

1. if `NOOK_AGENT_PATH` is explicitly set, it is still honored
2. if `NOOK_NCORE_PATH` is explicitly set, it is still honored
3. otherwise, if an embedded blob is present, the default path is now embedded-first
4. sidecar files are only used as fallback when the corresponding embedded blob is absent

Practical consequence:

- leftover `libnook-agent.so` / `libncore.so` files no longer silently change the default path
- the default runtime path is now closer to the intended Frida-like single-server model
- but this still does not remove `ncore` architecturally from stable spawn; it only makes the embedded path the preferred baseline when available

What still remains before `libncore.so` can be considered fully gone from the stable model:

1. stable spawn still uses the `ncore` mechanism logically, even if it is now usually carried as an embedded blob instead of a visible sidecar file
2. embedded `ncore` prepare/clear must be proven stable enough that the sidecar fallback can be removed, not just deprioritized
3. only after that can the `ncore` compatibility path itself be deleted from the stable backend

## 2026-05-10 follow-up: embedded ncore remote-memfd alignment

One more structural weakness was identified in the embedded `ncore` spawn path.

Previous behavior:

- `PrepareSpawnInZygoteEmbedded(...)` created a memfd in the server process
- then it built `/proc/<server-pid>/fd/<n>`
- zygote was asked to `dlopen()` that cross-process procfs path

This worked as a prototype, but it is more fragile than Frida's model because:

1. the target process is opening another process' procfs fd path
2. correctness depends on procfs visibility and timing of the server-owned fd
3. it is not the same "target opens its own fd" pattern used by the embedded agent attach path

Updated behavior:

- `PrepareSpawnInZygoteEmbedded(...)` now uses `RemoteCreateMemfdWithBytes(...)` to create and populate the memfd inside zygote itself
- the injected path becomes `/proc/self/fd/<remote_fd>`
- zygote now `dlopen()`s its own remote memfd path, matching the same pattern already used by embedded agent attach

Why this matters:

- this is closer to Frida's "deliver bytes, let the target load from its own fd" model
- it removes one cross-process procfs dependency from the default embedded `ncore` path
- it should reduce one class of embedded spawn fragility before removing the sidecar fallback

Scope:

- this does not yet remove `ncore` as a logical stable-spawn component
- it only makes the preferred embedded `ncore` transport path less brittle

## 2026-05-10 real-device validation: single visible server path working

Latest real-device verification on the Android 11 test phone confirms the current default spawn path is now working with only one visible runtime file on device.

Observed runtime directory after starting server:

- `/data/local/tmp/nook/nook-server`

Observed runtime directory did **not** contain:

- `libncore.so`
- `libnook-agent.so`
- `spawn_markers`
- `spawn_result.json`

Server-side runtime log was also clean:

- `server.err` empty
- `server.out` empty

Key log evidence from the successful `com.ad2001.frida0x1` spawn run:

1. embedded runtime selection:
   - `embedded agent selected path=/data/local/tmp/nook/libnook-agent.so materialization=deferred`
2. stable default spawn backend still chosen:
   - `zygote control disabled; using legacy spawn path pkg=com.ad2001.frida0x1`
3. embedded `ncore` delivery now uses remote self-fd in zygote:
   - `PrepareSpawnInZygoteEmbedded: begin zygote_pid=... path=/proc/self/fd/52 ...`
4. zygote-side `ncore` load succeeded:
   - `PrepareSpawnInZygote: ncore handle ready`
   - `NookNcore: ainject begin ...`
5. child payload load and explicit agent init succeeded:
   - `target matched, loading payload ...`
   - `NookAgentInitialize returned status=0`
6. host/server handshake completed:
   - `AGENT_READY sent pid=17484 name=com.ad2001.frida0x1 token=spawn-1-1-com.ad2001.frida0x1`
   - `spawn success pkg=com.ad2001.frida0x1 pid=17484 agent=/data/local/tmp/nook/libnook-agent.so`
7. hook result was confirmed by runtime callback logs:
   - `lab:frida-0x1:installed`
   - `lab:frida-0x1:hit:get_random`
   - `lab:frida-0x1:hit:check:left=5:right=14`

This is the first verified state in this branch where all of the following are true at the same time:

- only `nook-server` is visible in `/data/local/tmp/nook`
- stable default spawn still works
- no silent sidecar `libncore.so` fallback is required
- no sidecar `libnook-agent.so` needs to be pre-pushed
- hook execution is confirmed by end-to-end real-device logs

Important boundary:

- this proves "single visible server" for the stable default spawn path on the current device
- it does **not** yet prove that `ncore` has been architecturally removed
- `ncore` is still part of the stable spawn backend, but it is now being delivered from the embedded blob path instead of as a visible sidecar file
