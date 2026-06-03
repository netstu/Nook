# Nook Gadget V2 Loader Notes

## Purpose

This note records the v2.2 bootstrap seam introduced in `tools/nook_patchapk.py`
before the proxy-loader implementation exists.

The current goal is only to make the patch plan and emitted metadata express a
future bootstrap strategy choice without changing the existing working patch
path.

## Bootstrap modes

### `minimal`

- default mode
- keeps the current validated bootstrap path
- continues to rely on the existing manifest marker rewrite plus
  `System.loadLibrary("nook-gadget")` injection into the resolved startup
  smali target

### `proxy-loader`

- reserved for the later loader/proxy path
- currently exists only as an explicit plan/config seam
- does not yet change patch-time injection behavior by itself

## Current contract

- `tools/nook_patchapk.py` now accepts `--bootstrap-mode minimal|proxy-loader`
- `PatchPlan` JSON now includes `bootstrap_mode`
- emitted gadget config metadata now includes `bootstrap_mode`
- the default remains `minimal`
- selecting `proxy-loader` is currently declarative only; it is not yet the full
  proxy-loader implementation

## Why this seam exists now

- later tasks need a stable place to hang proxy-loader-specific metadata
- the patch tool can now represent intent without overloading existing
  startup/interaction fields
- current validated `minimal` behavior remains unchanged while v2.2 work is
  staged incrementally
