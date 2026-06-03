# 2026-05-11 Symbi Spawn Internal RPC Wildcard Fix

## Background

Real-device `spawn -f com.ad2001.frida0x1` on the legacy symbi path was unstable.

The debugging path converged in two stages:

1. Early failures that looked like `remote_dlopen_failed` were partly injector-state issues around ptrace stop semantics.
2. After stabilizing the injector side enough to observe real child behavior, the actual blocker moved into agent initialization inside the spawned child.

## Observed Symptoms

On the device, the child process reached:

- `NookAgentInitialize begin process=com.ad2001.frida0x1`
- `spawn gate bootstrap hook ok process=com.ad2001.frida0x1`
- `runtime bridge ensure begin process=com.ad2001.frida0x1`

Then it crashed with:

- `terminating with uncaught exception of type std::overflow_error: __next_prime overflow`

After adding fine-grained runtime bridge logs, the last successful line was:

- `bridge init: register internal rpc wildcard`

and the process died before:

- `bridge init: register internal rpc wildcard ok`

## Root Cause

`SetInternalRpcRequestHandler(OnRpc)` registered the wildcard internal RPC handler by inserting `"*"` into:

- `std::unordered_map<std::string, RpcRequestHandler> g_internal_rpc_handlers`

In the symbi/zygote-derived child, that container state was not safe to reuse. In this execution mode, inserting the wildcard handler could trigger:

- `std::overflow_error: __next_prime overflow`

This was not a generic `dlopen` failure. The library had already loaded and agent initialization was in progress. `remote_dlopen_failed` was only the host-side surface symptom after the child aborted.

## Fix

Changed internal RPC wildcard handling in [`src/framework/NookCommInternal.cpp`](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookCommInternal.cpp):

- Added a dedicated `RpcRequestHandler g_default_internal_rpc_handler`
- Stopped storing `"*"` inside `g_internal_rpc_handlers`
- Kept named internal RPC methods in the `unordered_map`
- Updated `HasInternalRpcRequestHandlers()` and `DispatchInternalRpcRequest()` to consult the dedicated wildcard handler
- Updated child reset logic to clear both:
  - `g_default_internal_rpc_handler`
  - `g_internal_rpc_handlers`

Also kept the earlier child-reset fix:

- clear inherited comm callbacks / public RPC handlers in [`src/framework/NookComm.cpp`](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookComm.cpp)
- clear inherited internal RPC state in [`src/framework/NookCommInternal.cpp`](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/framework/NookCommInternal.cpp)

## Validation

Real-device logs now show the child fully crossing the previous crash point:

- `bridge init: register internal rpc wildcard`
- `bridge init: register internal rpc wildcard ok`
- `script runtime bridge initialized (lazy runtime init)`
- `runtime bridge ensure ok process=com.ad2001.frida0x1`
- `AGENT_READY sent ...`
- `spawn success pkg=com.ad2001.frida0x1 ...`

Hook execution also succeeded:

- `lab:frida-0x1:installed`
- `lab:frida-0x1:hit:get_random`
- `lab:frida-0x1:result:forced-random=5:expected-input=14`
- `lab:frida-0x1:hit:check:left=5:right=14`

## Notes

Two important findings from this round:

1. On the current symbi path, the child already loads the agent during the stub-triggered path. The later host-side framing can still report `remote_dlopen_failed`, but that message is not authoritative unless correlated with child logs.
2. The current "embedded agent" path is still not a true single-binary delivery path for symbi spawn. Logs still show:
   - `embedded agent selected path=/data/local/tmp/nook/libnook-agent.so materialization=deferred`
   - symbi copies `/data/local/tmp/nook/libnook-agent.so` into the target app cache and loads that file

So for symbi spawn, `libnook-agent.so` is still a required deployed artifact today.

## Follow-ups

1. Fix `nook_server` Android NDK build so `symbi_injector.cpp` no longer depends on a fragile source path outside the project tree.
2. Finish the real single-server path for symbi by switching child delivery from filesystem `.so` copy to an in-memory or server-materialized path owned entirely by `nook-server`.
