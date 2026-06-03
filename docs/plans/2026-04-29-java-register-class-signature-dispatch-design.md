# Nook Java.registerClass Signature-Aware Dispatch Design

## Goal

Bring `Java.registerClass(spec)` closer to Frida by supporting multiple declarations for the same method name and dispatching callbacks by the real Java method signature at runtime.

## Confirmed Scope

This pass will support:

- `methods.foo = [{...}, {...}]` for the same method name
- runtime callback dispatch by:
  - method name
  - JNI signature derived from the actual `java.lang.reflect.Method`
- preserving existing single-function and single-declaration behavior

This pass will not support:

- cross-interface same-name conflict resolution beyond the actual resolved runtime `Method`
- fake dispatch based only on JS argument values
- `fields`
- `extends` / `superClass`
- constructor declaration overloads

## Why This Scope

Nook's current `registerClass` already supports:

- single function declarations
- single Frida-style declaration objects
- single-entry declaration arrays

But callback dispatch still uses only `method_name`, which is not enough to support:

- `methods.foo = [{ returnType, argumentTypes, implementation }, { ... }]`

Frida-aligned support needs the actual Java-side method identity, not just the JS-visible argument list. The correct source of truth is the `Method` object received by the native `InvocationHandler`.

## Recommended Approach

### Approach A: Derive JNI signature from `Method` and dispatch by `name + signature`

This is the recommended approach.

Flow:

1. `registerClass` parsing normalizes every declaration into:
   - `method_name`
   - declared JNI signature
   - JS callback
2. runtime stores callbacks keyed by:
   - method name
   - signature
3. native `InvocationHandler` callback receives the actual `Method`
4. native bridge derives the JNI signature from that reflected method
5. runtime callback dispatch first tries:
   - exact `method_name + signature`
6. if no signature-specific match exists, runtime falls back to:
   - legacy single-callback-by-name behavior

Why this is the right shape:

- closest to Frida semantics
- uses actual Java runtime method identity
- avoids brittle JS-side guessing
- preserves backward compatibility for existing scripts

## Alternatives Considered

### Approach B: Infer declaration by JS callback arguments

Pros:

- less native bridge work

Cons:

- unreliable with `null`
- unreliable with boxed primitives and inheritance
- does not reflect Java's actual selected method

Conclusion:

Not acceptable for Frida alignment.

### Approach C: Pre-enumerate and cache all interface methods at registration time

Pros:

- rich validation opportunities

Cons:

- heavier than needed for this phase
- adds reflection complexity earlier than necessary

Conclusion:

Can come later if needed, but not required for this pass.

## Public Semantics

After this pass, this shape should work:

```javascript
var Listener = Java.registerClass({
  name: 'nook.smoke.MultiDecl',
  implements: [SomeInterface],
  methods: {
    marker: [
      {
        returnType: 'java.lang.String',
        argumentTypes: ['int'],
        implementation: function (value) {
          return 'int:' + value;
        }
      },
      {
        returnType: 'java.lang.String',
        argumentTypes: ['java.lang.String'],
        implementation: function (value) {
          return 'str:' + value;
        }
      }
    ]
  }
});
```

Runtime behavior:

- if Java calls `marker(int)`, the first callback runs
- if Java calls `marker(java.lang.String)`, the second callback runs

## Signature Model

Each declaration should be normalized to a JNI signature, for example:

- `returnType: 'void', argumentTypes: ['android.view.View']`
  - `(Landroid/view/View;)V`
- `returnType: 'java.lang.String', argumentTypes: ['int']`
  - `(I)Ljava/lang/String;`

This pass should rely on the same type-name-to-signature rules already used elsewhere in Nook's Java bridge, instead of introducing a second signature grammar.

## Runtime Storage Model

Current model:

- callback map keyed by `method_name`

New model:

- callback map keyed by:
  - `method_name`
  - `signature`

Compatibility rule:

- single-function old form still stores a legacy fallback entry
- multi-declaration form requires unique signatures per method name

## Native Bridge Changes

The native `InvocationHandler` path already receives:

- `proxy`
- `method`
- `args`

This pass should add:

- reflected extraction of:
  - return type
  - parameter types
- conversion of the reflected method into a JNI signature string
- passing that signature into the JS runtime callback dispatch path

## Error Handling

Reject at registration time when:

- a declaration array contains duplicate signatures
- a multi-declaration entry omits `returnType`
- a multi-declaration entry omits `argumentTypes`
- a declaration cannot be normalized into a valid JNI signature

Reject at callback time when:

- multi-declaration dispatch is required but no matching `name + signature` entry exists

## Testing Strategy

Host tests first:

- registerClass accepts two declarations for the same method name with distinct signatures
- callback dispatch selects the correct implementation for each signature
- duplicate signature declarations fail
- old single-callback behavior still works

Device smoke second:

- use a deterministic demo callback point with two signatures if available
- if not available in the demo app, validate through host-only dispatch tests and keep device smoke on the existing single-declaration path

## Compatibility Boundary

After this pass, Nook becomes significantly closer to Frida for `registerClass` callback declarations.

It still will not claim:

- full dynamic-class semantics
- `fields`
- `extends`
- every complex interface-collision edge-case

## Recommendation

Implement exact `name + signature` dispatch with backward-compatible fallback-by-name. This gives real Frida alignment without introducing guess-based dispatch or fake semantics.
