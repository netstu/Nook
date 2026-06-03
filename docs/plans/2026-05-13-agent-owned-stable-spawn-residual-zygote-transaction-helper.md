# Agent-Owned Stable Spawn Residual Zygote Transaction Helper

## Context

After introducing explicit helpers for success commit, terminal classification,
failed zygote classification, and deferred-route owner release, `FinalizeSpawn()`
still contained one inline owner/session mutation:

- extract residual `zygote_control_transaction` from `active_spawn_owner_`
- clear the stored transaction state

This mutation belonged to finalize-time owner/session transition logic and was a
good candidate for the next helper boundary.

## Change

Extracted explicit helper:

- `TakeResidualZygoteControlTransactionForFinalize()`

`FinalizeSpawn()` now delegates residual zygote-control transaction extraction
to this helper instead of manipulating `active_spawn_owner_` inline.

The helper owns the contract:

- move the residual zygote-control transaction only when identifier matches
- clear stored residual transaction state after extraction
- preserve foreign transaction state otherwise

## Why It Matters

This is another direct owner/session transition helper, not just an outcome
write helper.

At this point, the host-side spawn lifecycle has explicit helper boundaries for:

- success route commit
- terminal classification
- zygote-control failure classification
- deferred-route active owner release
- finalize-time residual zygote transaction extraction

That is close to the point where the remaining lifecycle logic can be raised
from scattered helper extraction into more explicit owner/session transition
composition.

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
  -o build/test_ninjector_spawn_injector_residual_txn_red.exe
```

Observed failure:

- `NinjectorSpawnInjector` had no member
  `TakeResidualZygoteControlTransactionForFinalize`

Green:

```powershell
g++ -std=c++17 -I . -I include -I src `
  tests/communication/test_ninjector_spawn_injector.cpp `
  server/ninjector_spawn_injector.cpp `
  server/server_runtime.cpp `
  server/ninjector_compat.cpp `
  src/communication/protocol/messages.cpp `
  src/communication/protocol/tlv.cpp `
  -o build/test_ninjector_spawn_injector_residual_txn_green.exe

& "E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_residual_txn_green.exe"
```
