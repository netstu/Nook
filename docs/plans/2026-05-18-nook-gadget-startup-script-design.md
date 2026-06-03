# Nook Gadget Startup Script Design

## Goal

Make `nook-gadget` behave more like Frida Gadget by allowing a patched APK to carry a default startup script that is loaded automatically when the target app process starts.

The first version should prove one clear user-visible outcome:

- patch APK
- install APK
- launch app
- hook logic is already active without running `attach -l <script>`

## Problem Statement

The current `nook-gadget` path proves embedded runtime delivery, but it still relies on the host to push script source after attach. That makes the runtime feel closer to "embedded agent waiting for manual script load" than to Frida Gadget.

This leaves one missing capability:

- the gadget runtime should be able to discover, load, and run a default script from APK assets on its own

Without that capability:

- cold-start hook validation is awkward
- early startup hook coverage is weak
- gadget value is less obvious than injector-style attach flows

## Frida Gadget Alignment

The design should follow the Frida Gadget direction, not invent a separate Nook-specific workflow:

- the runtime is packaged into the target app
- the runtime initializes automatically on library load
- configuration is read from app-bundled assets
- a default script can be loaded automatically
- host attach remains optional for observation, RPC, and debugging

For Nook v1, "Frida-aligned" does not require every Frida Gadget feature. It requires matching the core deployment and startup semantics.

## Approaches Considered

### Approach A: Asset-backed startup script

Patch the APK with:

- `assets/nook-gadget/config.json`
- `assets/nook-gadget/startup.js`

Then have `libnook-gadget.so` read the config, resolve the script asset, and load it automatically.

Pros:

- closest to Frida Gadget behavior
- clean separation between config and script payload
- simple to validate and debug
- easy to extend later

Cons:

- requires a small runtime asset-reading path
- requires a gadget-local script load entry, not only host-driven load

### Approach B: Inline script in config JSON

Keep only `config.json` and store script source inside JSON.

Pros:

- fewer APK assets
- slightly smaller patch-tool surface

Cons:

- poor readability and maintainability
- escaping/formatting is awkward
- less aligned with Gadget-style packaging

### Approach C: Hardcoded demo hook inside gadget

Bake one login-page demo hook into the runtime itself.

Pros:

- fastest demo

Cons:

- not a real gadget script model
- not reusable
- immediately creates cleanup debt

## Recommendation

Use **Approach A**.

The first version should add one optional patch-time input:

- `--startup-script <path>`

If provided, the patch flow must:

- copy the file into `assets/nook-gadget/startup.js`
- write matching metadata into `assets/nook-gadget/config.json`

If not provided:

- keep current gadget behavior unchanged
- do not auto-load a startup script

## Runtime Contract

The gadget runtime startup sequence should be:

1. initialize control channel
2. initialize script runtime bridge
3. read `assets/nook-gadget/config.json`
4. if a startup script is configured, read `assets/nook-gadget/startup.js`
5. create and load the script in-process
6. keep host attach/control available after that

This preserves the existing control plane while making script execution self-starting.

## Configuration Shape

The first version should extend `config.json` to include a startup script section:

```json
{
  "gadget_version": "0.1",
  "startup_mode": "auto-start",
  "transport_mode": "default",
  "debug_logging": false,
  "startup_script": {
    "mode": "asset",
    "path": "assets/nook-gadget/startup.js",
    "required": false
  }
}
```

### Semantics

- `mode`
  - v1 only supports `"asset"`
- `path`
  - APK asset path written by the patch tool
- `required`
  - if `false`, startup script failure degrades gracefully
  - if missing, treat as `false` in v1

## Failure Policy

Startup script load failure must not break app launch.

If config read, asset read, script create, or script load fails:

- log the failure clearly
- continue gadget initialization
- keep the process attachable by the host
- fall back to attach/control-only mode

This is the safest first version and matches the user's confirmed boundary.

## Minimal Code Shape

### Patch-time changes

`tools/nook_patchapk.py`

- add `--startup-script <path>`
- emit `assets/nook-gadget/startup.js`
- extend `config.json` with startup script metadata

### Runtime changes

`src/gadget/nook_gadget_runtime.cpp`

- add gadget-owned post-bridge startup-script bootstrap

New small helper module, for example:

- `src/gadget/nook_gadget_config.h`
- `src/gadget/nook_gadget_config.cpp`

Responsibilities:

- read gadget asset files
- parse config JSON
- expose resolved startup-script config to the runtime

### Script load entry

Reuse the current script runtime as much as possible, but add a thin gadget-local entry that can:

- create a script from source
- load it
- return success/failure without requiring a host session

This should stay deliberately small and should not create a second script-management model.

## Testing Strategy

### Patch tool tests

Verify that `--startup-script` causes:

- `assets/nook-gadget/startup.js` to exist in the output APK
- config JSON to contain startup script metadata

Verify that omitting it preserves current output behavior.

### Runtime tests

Add narrow gadget runtime tests that prove:

- startup-script bootstrap is attempted when configured
- startup-script bootstrap is skipped when not configured
- startup-script load failure falls back instead of failing whole gadget init

### Real-device validation

For `TargetDemo`:

- patch APK with a login-page startup script
- install patched APK
- launch app
- do not run `attach -l <script>`
- open login page and submit password

Expected:

- hook is already active
- later `attach` without `-l` still works for observation/debugging

## Success Criteria

This design is successful when all of the following are true:

- patched APK can include a startup script asset
- gadget reads config from APK assets
- gadget auto-loads the startup script on app launch
- login-page hook works without host-side script injection
- startup-script failure degrades to attach-only mode
- host attach still works after auto-start
