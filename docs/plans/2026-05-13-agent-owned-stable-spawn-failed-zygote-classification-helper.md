# Agent-Owned Stable Spawn Failed Zygote Classification Helper

## Context

After converging successful route commit and terminal classification behind
explicit helpers, the zygote-control failure path still mixed two
responsibilities inside `ApplyFailedZygoteControlOutcome()`:

- decide whether fallback is allowed
- write failed transaction / fallback policy / final status into outcome

That meant failure lifecycle application was still handled inline instead of
through an explicit helper boundary.

## Change

Extracted explicit helper:

- `ApplyFailedZygoteControlClassification()`

This helper now owns the failed zygote-control outcome write:

- snapshot failed transaction
- record fallback policy
- record final status

`ApplyFailedZygoteControlOutcome()` now decides only whether fallback is allowed
and then delegates the actual failure classification write.

## Why It Matters

This completes the same helper pattern across the three key spawn lifecycle
surfaces:

- success route commit
- terminal classification
- zygote-control failure classification

The host-side model is becoming progressively less backend-function-centric and
more explicit about lifecycle application boundaries. That is the prerequisite
for moving further toward a true owner/session-driven stable spawn model.

## Verification

Red:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_failed_zygote_classification_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member `ApplyFailedZygoteControlClassification`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_failed_zygote_classification_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_failed_zygote_classification_green.exe"
```
