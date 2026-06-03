# Java Env object ref type design

Date: 2026-04-29

## Goal

Add one more safe low-level `Env` helper:

- `env.getObjectRefType(obj)`

This helper must stay within the current `Env` architecture boundary and work as a single-shot JNI query.

## Why this next

After:

- monitor rollback
- `getSuperclass(...)`
- `isAssignableFrom(...)`

the next safest Frida-aligned JNI helper is one that:

- accepts an existing Java object wrapper
- performs exactly one JNI query
- does not depend on cross-call lifetimes

`GetObjectRefType` fits that bucket.

## Public shape

### `env.getObjectRefType(obj)`

- accepts one Java object wrapper
- returns one of:
  - `"invalid"`
  - `"local"`
  - `"global"`
  - `"weak-global"`
- rejects non-object input

## Return format choice

Options considered:

### Option 1: return raw JNI enum number

Pros:

- closest to JNI

Cons:

- less readable in scripts
- leaks JNI enum details into JS

### Option 2: return stable strings

Pros:

- easier to read
- easier to assert in smoke/tests
- still preserves low-level meaning

Cons:

- one small translation layer

### Option 3: return a richer object

Pros:

- extensible

Cons:

- unnecessary scope

## Recommendation

Choose option 2.

## Input model

This phase stays object-wrapper-only:

- accept normal Java object wrappers
- do not accept raw pointers
- do not accept class wrappers

Reason:

- current public `Env` style for ref helpers is wrapper-first
- class wrappers in Nook are metadata wrappers, not stable live class handles

## Testing strategy

Desktop first:

- `env.getObjectRefType(obj)` returns `"global"` when the host callback reports `JNIGlobalRefType`
- `env.getObjectRefType(obj)` returns `"invalid"` when the host callback reports `JNIInvalidRefType`
- invalid input is rejected

Android smoke second:

- binding exists
- calling `env.getObjectRefType(...)` on a real object wrapper returns a readable string

## Success criteria

- helper is a single-shot JNI query
- desktop regression passes
- Android build passes
- device smoke prints a stable readable result
