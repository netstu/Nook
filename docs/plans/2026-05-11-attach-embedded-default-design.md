# Attach Embedded Default Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make Nook `attach` default to the embedded-agent path so its runtime behavior is closer to Frida.

**Architecture:** Keep the current attach protocol and agent-ready handshake, but change injector selection so `attach` prefers embedded agent delivery through memfd/fd injection. Preserve sidecar path injection only as an explicit or fallback path.

**Tech Stack:** `nook-server`, `ninjector_compat`, embedded agent blob, Android ptrace/dlopen/memfd injection.

---

## Design

1. `attach` should prefer `InjectEmbeddedAgentByPid()` whenever an embedded agent blob is available and the user did not explicitly request an external `NOOK_AGENT_PATH`.
2. Path-based `InjectSoByPid()` remains supported for:
   - explicit external agent path debugging
   - embedded path unavailable or failed cases
3. This round does not redesign the server/agent handshake. It keeps the current `AGENT_READY`-based attach completion rule and only narrows injector-path ambiguity.

## Scope

- Modify injector-side attach decision logic in `server/ninjector_spawn_injector.cpp`
- Keep existing `spawn` logic unchanged in this round
- Keep host CLI / protocol unchanged

## Verification

- Build `nook_server`
- Push only `nook-server`
- Real-device `attach` on `frida0x8` and verify hook still works
- Confirm logs show embedded attach chosen as default path unless explicit sidecar config is present
