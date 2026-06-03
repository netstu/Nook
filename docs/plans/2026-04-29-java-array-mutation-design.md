# Nook Java.array Mutation Design

## Goal

Make `Java.array(typeName, elements)` behave closer to Frida for the most important mutation case:

- after `arr[i] = value`
- and then passing `arr` back into a Java call
- Java should observe the updated contents

## Confirmed Scope

This pass will support:

- primitive arrays created by `Java.array(...)`
- object arrays created by `Java.array(...)`
- multi-dimensional arrays created by nested `Java.array(...)`
- index assignment updates:
  - `arr[0] = 2`
  - `arr[1] = 'x'`
  - `rows[0] = Java.array('int', [...])`
- preserving current read usage:
  - `arr[i]`
  - `arr.length`
  - iteration patterns already supported by normal JS arrays

This pass will not support:

- full live array semantics for:
  - `push`
  - `pop`
  - `shift`
  - `unshift`
  - `splice`
  - `sort`
- arbitrary plain JS arrays automatically becoming Java arrays
- direct binding to a live Java-side array object

## Current Problem

Nook's current `Java.array(...)` support is already usable for construction and Java invocation, including:

- primitive arrays
- object arrays
- multi-dimensional arrays

But the current model is still closer to a construction-time snapshot than to a mutation-aware wrapper. That means scripts can create an array successfully, but `arr[i] = ...` does not yet have a clearly defined, intentionally preserved path back into the bridge's later Java argument serialization.

For Frida-style scripts, the most important missing behavior is simple:

- mutate a `Java.array(...)`
- pass it to Java
- Java sees the mutation

## Recommended Approach

### Approach A: Mutation-aware JS wrapper state

This is the recommended approach.

`Java.array(...)` should still return something that script authors can use like an array, but internally it should carry mutation-aware state instead of relying only on the initial construction snapshot.

Design shape:

1. `Java.array(typeName, elements)` creates:
   - a JS array-like result
   - a hidden array-state object describing:
     - Java type name
     - current element values
     - nested array wrappers where applicable
2. indexed assignment updates both:
   - visible JS element
   - hidden array-state element
3. when the value is later marshaled back into `JavaJsValue`, the runtime reads the latest hidden state instead of the stale construction snapshot

Why this is the right shape:

- closest to Frida's day-to-day script expectations
- small enough to fit Nook's current architecture
- does not require redesigning Java invocation or JNI ownership
- extends naturally to nested arrays

## Alternatives Considered

### Approach B: Re-scan the JS array at Java invocation time

Instead of preserving internal state, the bridge could walk the current JS array object every time it is passed to Java.

Pros:

- smaller implementation surface at first glance

Cons:

- weaker semantics for nested arrays and aliasing
- more runtime ambiguity
- easier to regress later
- less explicit ownership/model than a dedicated wrapper state

Conclusion:

Not recommended as the primary model.

### Approach C: True live Java-array wrapper

Expose `Java.array(...)` as a wrapper directly backed by a live Java array object and synchronize every change immediately.

Pros:

- strongest semantics

Cons:

- much higher implementation cost
- quickly expands into full object-model work
- unnecessary for the current Frida-alignment milestone

Conclusion:

Too heavy for this phase.

## Public Semantics

After this pass, the intended user-visible behavior is:

```javascript
var values = Java.array('int', [1, 2, 3]);
values[1] = 9;
SomeClass.useArray(values); // Java should observe [1, 9, 3]
```

And for nested arrays:

```javascript
var row1 = Java.array('int', [1, 2]);
var row2 = Java.array('int', [3, 4]);
var rows = Java.array('int[]', [row1, row2]);

row1[0] = 7;
rows[1] = Java.array('int', [8, 9]);
SomeClass.use2d(rows); // Java should observe [[7, 2], [8, 9]]
```

## Architecture

### JS bootstrap layer

The bootstrap should continue exposing:

- `Java.array(typeName, elements)`

But the returned object should now carry hidden metadata describing its Java-array state. This metadata should be private and implementation-specific.

### Runtime marshalling layer

When Java invocation later parses a JS value back into `JavaJsValue`, it should detect the mutation-aware array wrapper and serialize from its current state instead of relying only on the original snapshot.

### Nested-array behavior

If an element is itself a `Java.array(...)` result, the outer array should retain that nested wrapper relationship so later serialization can recurse into the nested current state.

## Error Handling

This pass should preserve current type-check quality and extend it naturally to mutation:

- wrong primitive replacement types still fail with precise messages
- wrong nested-array element types still fail with precise messages
- unsupported array-model operations should not silently invent partial semantics

## Testing Strategy

Host tests first:

- primitive mutation survives into Java invocation
- object-array mutation survives into Java invocation
- nested-array mutation survives into Java invocation

Device smoke second:

- use a small demo method that renders array contents back into a string
- mutate array in JS before passing it in
- confirm device-side observed contents are updated

## Boundary Relative to Frida

This pass gets Nook closer to Frida in the highest-value way:

- common `arr[i] = ...` mutation survives into later Java calls

But it still does not claim full Frida-like live array object semantics. That boundary should stay explicit in docs and tests.

## Recommendation

Implement mutation-aware wrapper state now, verify on host and device, and defer full array method mutation semantics until there is evidence users actually need them.
