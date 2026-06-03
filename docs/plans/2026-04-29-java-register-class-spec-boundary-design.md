# Nook Java.registerClass Unsupported-Spec Boundary Design

## Goal

Make `Java.registerClass(spec)` fail fast when the caller provides Frida-style
spec fields that Nook's current proxy-based architecture does not actually
support.

## Confirmed Scope

This pass will:

- reject `spec.fields`
- reject `spec.superClass`
- keep existing supported `registerClass` method semantics unchanged

This pass will not:

- add fake `fields`
- add fake `superClass`
- change the current `Proxy.newProxyInstance(...)` architecture

## Why This Scope

Nook's current `registerClass` surface is already much closer to Frida than it
was earlier:

- declaration objects work
- declaration arrays work
- runtime signature-aware callback dispatch works

The remaining risk is not "missing one more supported form". The real risk is
that a Frida user passes a richer spec and Nook silently ignores part of it.

That creates worse behavior than an explicit failure:

- the script appears accepted
- the object is created
- but the requested semantics never existed

For `fields` and `superClass`, silent acceptance is misleading because Nook
still builds a Java `Proxy`, not a real generated subclass.

## Recommended Approach

### Approach A: Explicit bootstrap validation and rejection

This is the recommended approach.

Behavior:

1. `Java.registerClass(spec)` validates the common required fields as before
2. it additionally checks:
   - `spec.fields`
   - `spec.superClass`
3. if either is present and non-null / non-undefined, it throws a clear
   `TypeError`

Why this is the right shape:

- minimal change
- prevents fake compatibility
- matches the current architecture honestly
- keeps future room to implement real semantics later

## Alternatives Considered

### Approach B: Ignore unsupported keys silently

Pros:

- zero code change

Cons:

- misleading
- harder to debug for script authors
- encourages false assumptions about Nook's Frida parity

Conclusion:

Not acceptable.

### Approach C: Accept the keys but print warnings

Pros:

- softer migration path

Cons:

- warnings are easy to miss
- script still "works" in a broken semantic state

Conclusion:

Explicit failure is better.

## Testing Strategy

Host tests first:

- `spec.fields` rejects with a clear error
- `spec.superClass` rejects with a clear error
- existing supported registration paths remain green

Optional device smoke second:

- because this is bootstrap validation, host tests are the real proof
- device smoke is only needed as a convenience check

## Recommendation

Do this now. It is small, honest, and directly improves Frida-style usability by
preventing silent misconfiguration.
