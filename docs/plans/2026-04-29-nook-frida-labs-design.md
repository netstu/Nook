# Nook Frida-Labs Design

## Goal

Add a dedicated `tests/test_lab/nook-frida-labs/` suite that maps the upstream `Frida-Labs` APK challenges to Nook scripts, per-challenge runbooks, and a machine-readable manifest.

## Layout

- `tests/test_lab/nook-frida-labs/README.md`
- `tests/test_lab/nook-frida-labs/manifest.json`
- `tests/test_lab/nook-frida-labs/frida-0x*/script.js`
- `tests/test_lab/nook-frida-labs/frida-0x*/README.md`

## Status Model

- `supported`: current Nook APIs can express the same core behavior as the Frida challenge
- `partial`: same app outcome is reachable, but not through direct feature parity or without verified runtime parity
- `blocked`: not used in the first pass, reserved for future gaps

## Mapping Rules

- Prefer behavior parity over API-name parity.
- Keep each challenge self-contained.
- Use stable `lab:frida-0x*:...` message prefixes so later automation can grep output reliably.
- Keep `0x7` and `0xB` conservative:
  - `0x7` uses a validated object-construction path instead of claiming full constructor-hook parity.
  - `0xB` uses `Memory.patchCode(...)` on the ARM64 branch site instead of claiming writer-API parity.

## Verification

Use a small host-side unittest to verify the suite exists, the manifest is valid JSON, there are 11 challenge entries, and each challenge directory contains both `script.js` and `README.md`.
