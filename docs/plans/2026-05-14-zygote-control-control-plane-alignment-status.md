# zygote-control control-plane alignment status

## Result

This checkpoint fixed the current `zygote-control` control-plane mismatch with the intended Frida-like model:

- zygote agent control session is now kept alive across:
  - `nook.spawn.status`
  - `nook.spawn.installForkHook`
  - `nook.spawn.uninstallForkHook`
- server-side install no longer depends on a post-install disconnect
- stale `NOOK_ZYGOTE_MONITOR_READY=1` without a live control session is now treated as stale and reinjected, instead of being silently reused

## Code changes

- [server/zygote_control_rpc.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/zygote_control_rpc.cpp)
  - `InstallZygoteForkHook()` now validates the control plane by waiting for `status` RPC readiness after install, instead of waiting for disconnect.
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
  - `monitor_ready && !has_authoritative_session` now means stale monitor, forcing reinject.
  - removed the old `skip install rpc` behavior for `ready-without-session`.
- [server/server_main.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_main.cpp)
  - preexisting zygote session probe now requires a real control-ready session.
- [tests/headers/test_zygote_control_connection_lifetime_regressions.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/headers/test_zygote_control_connection_lifetime_regressions.cpp)
  - updated away from the old “install must disconnect” expectation.

## Important build/runtime note

This round also reconfirmed a critical SOP detail:

- changing zygote-control agent behavior requires rebuilding:
  1. `nook_agent`
  2. `tools/build_embedded_agent_blob.ps1`
  3. `nook_server`

Rebuilding `nook_server` alone is not enough, because the server embeds `libnook-agent.so` as a generated blob.

## Verified current state

On-device logs now show:

- control-stage `AGENT_READY` arrives from `zygote64`
- `install/status/uninstall` RPCs all succeed on the same live session
- the previous install-time disconnect is gone

So the old failure:

- install succeeds
- agent intentionally disconnects
- later RPCs fail with `zygote control-ready agent session not found`

is no longer the active blocker.

## Current remaining blocker

`spawn` still ends with:

- `authoritative agent ready timeout`

and the logs show no child-side:

- `spawn match ok ...`
- `child activation prepared ...`
- runtime-stage `AGENT_READY`

This means the remaining problem is no longer control-plane lifetime. It is child activation.

## Next step

The next step should align with Frida’s actual Android zygote strategy:

- do not keep pushing on timeout handling
- move child activation from the current zygote inline-hook assumptions toward a real slot-patch trigger

Relevant Frida reference:

- [hook_program/frida/subprojects/frida-core/src/linux/linux-host-session.vala](/E:/Learn/my_program/all_my_hook/hook_program/frida/subprojects/frida-core/src/linux/linux-host-session.vala)

What Frida does there:

- patches `android.os.Process.setArgV0()` slot
- optionally patches `selinux_android_setcontext` slot
- uses that zygote-side trigger to drive child handoff

Nook already has closely related local machinery in:

- [server/symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)
- [server/symbi/stub_src/stub.c](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/stub.c)

So the next real migration target is:

- reuse/adapt the existing slot-patch machinery for agent-owned child activation
- keep the zygote agent resident
- let the inherited child agent activate itself without falling back to legacy `ncore`
