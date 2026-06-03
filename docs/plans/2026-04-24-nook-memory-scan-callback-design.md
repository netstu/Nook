# Nook Memory Scan Callback Design

**Goal**

Add a first callback-based `Memory.scan(address, size, pattern, callbacks)` API so scripts can use a Frida-like memory scanning shape without waiting for a later fully asynchronous runtime.

This phase builds directly on the already implemented:

- `Memory.scanSync(...)`
- pattern parser
- readability validation

## Scope

In scope:

- add `Memory.scan(address, size, pattern, callbacks): void`
- support callback object fields:
  - `onMatch(address, size)`
  - `onError(reason)`
  - `onComplete()`
- support `'stop'` as an `onMatch` return value to terminate the scan early
- keep implementation synchronous internally

Out of scope:

- background scanning threads
- event-loop scheduling
- promise-based scanning
- partial-range recovery across unreadable subranges

## Recommended API

Example:

```javascript
Memory.scan(base, size, '68 65 6c 6c 6f', {
  onMatch(address, size) {
    send({ type: 'send', payload: `match:${address}:${size}` });
  },
  onComplete() {
    send({ type: 'send', payload: 'complete' });
  }
});
```

Early stop:

```javascript
Memory.scan(base, size, '68 65 6c 6c 6f', {
  onMatch(address, size) {
    return 'stop';
  }
});
```

## Behavior

### Validation

Invalid arguments still throw immediately:

- missing args
- invalid pointer
- invalid size
- invalid pattern
- callbacks not an object
- `onMatch` present but not a function
- `onError` present but not a function
- `onComplete` present but not a function

### Scan flow

1. validate args
2. if the range is unreadable:
   - call `onError(reason)` if provided
   - call `onComplete()` if provided
   - return `undefined`
3. otherwise invoke `onMatch(address, size)` for each match
4. if `onMatch` returns `'stop'`, stop scanning early
5. call `onComplete()` once
6. return `undefined`

### Exceptions thrown by callbacks

If a user callback throws, propagate that JS exception to the caller. This keeps failures visible and avoids silently swallowing script bugs.

## Architecture

Implementation should stay in `src/agent_runtime/js_runtime.cpp`.

Recommended structure:

1. factor the byte scan into a small helper reusable by both APIs
2. keep `Memory.scanSync(...)` using the shared helper
3. implement `Memory.scan(...)` as:
   - parse + validate
   - call shared scan helper
   - invoke JS callbacks synchronously

No extra runtime state is needed for this phase.

## Testing Strategy

Add runtime tests for:

1. `onMatch` invoked twice and `onComplete` invoked once
2. `'stop'` halts after the first match
3. unreadable range triggers `onError` and `onComplete`
4. invalid callback object throws

After runtime tests pass, extend the smoke script with one callback-based scan message.
