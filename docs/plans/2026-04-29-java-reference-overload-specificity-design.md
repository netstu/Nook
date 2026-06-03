# Nook Java Reference Overload Specificity Design

## Goal

Continue aligning Nook's Java direct-invoke path with Frida by resolving common reference-type overload ambiguities using real assignability semantics instead of JS-side special cases.

## Problem

After the previous overload-tightening pass, Nook's direct invoke path is better at:

- `null`
- boxed primitive fallbacks
- `String` / `Object`
- `Number` / `Object`

But there is still a deeper gap:

- wrapper objects only contribute their concrete class name plus `java.lang.Object`
- native overload matching still effectively behaves like:
  - exact reference descriptor match
  - plus a narrow `Object` exception

That means several Frida-like cases still remain weaker than they should be:

- concrete class argument against superclass overload
- concrete class argument against interface overload
- `null` against both `Object` and a more specific reference overload
- multiple matching reference overloads where one is clearly more specific

## Scope

This pass is only about default Java direct invoke overload selection.

It will improve:

- instance wrapper direct invoke
- static wrapper direct invoke
- loader-aware ClassFactory direct invoke

It will not change:

- explicit `.overload(...)`
- hook install exact-signature path
- `registerClass(...)`
- JNI `Env` execution-model boundaries

## Approaches Considered

### Approach 1: Add more JS-side candidate ordering

Examples:

- guess common interfaces
- guess superclass ladders
- hardcode object-type fallback order

Pros:

- small patch in one place

Cons:

- quickly turns into guess-based logic
- still does not know the real Java overload set
- drifts away from Frida

Conclusion:

Not recommended.

### Approach 2: Use real assignability inside native overload matching

This is the recommended approach.

Flow:

1. JS runtime still generates a small, safe candidate set
2. native overload resolver reflects the candidate methods
3. for reference parameters, matching uses real assignability
4. when multiple methods match, the resolver picks the more specific one using parameter-type assignability

Pros:

- closest to Frida in behavior
- uses real Java type relationships
- avoids JS-side fake inheritance guessing

Cons:

- more native resolver complexity
- still not a complete full-engine rewrite

Conclusion:

Best trade-off for this phase.

### Approach 3: Rewrite overload resolution as a full scoring engine now

Pros:

- strongest long-term foundation

Cons:

- much larger change
- larger regression surface
- not required for the next practical gain

Conclusion:

Too large for this pass.

## Recommended Design

### 1. Keep JS candidate generation narrow

Do not expand JS-side inheritance guessing further.

Keep the current candidate model:

- concrete wrapper class
- `java.lang.Object`
- `null` internal token
- boxed/object fallbacks already added

This keeps JS behavior predictable and pushes type-relationship reasoning to the native layer where the real overload set is visible.

### 2. Upgrade native reference-parameter matching

In `ResolveJavaMethodSignatureByTypeNames(...)`, when checking whether one reflected parameter accepts one inferred candidate:

- keep primitive matching as-is
- keep array behavior as-is where already verified
- for reference descriptors:
  - exact match still succeeds
  - `null` still matches any reference type
  - otherwise resolve both sides as Java classes and use real assignability:
    - `parameterType.isAssignableFrom(argumentType)`

This lets:

- `Runnable` accept `Proxy` implementing `Runnable`
- superclass overloads accept subclass wrappers
- interface overloads accept implementation-class wrappers

without inventing fake JS-side class graphs.

### 3. Add "more specific reference overload wins" only when provable

If multiple overloads match, compare their parameter types pairwise.

Method A is more specific than method B if:

- for every parameter:
  - same type, or
  - B's parameter type is assignable from A's parameter type
- and at least one parameter is strictly more specific

This must use real Java assignability for reference parameters.

Examples:

- `String` is more specific than `Object`
- `ArrayList` is more specific than `List`
- `List` is more specific than `Object`

If the comparison cannot prove one method is more specific than the other:

- keep the result ambiguous

This is the key correctness boundary.

### 4. Let `null` benefit from the same specificity rule

For `null`, the previous pass only made reference overloads match.

This pass should additionally allow:

- `foo(String)` to win over `foo(Object)` for `foo(null)`
- `foo(List)` to win over `foo(Object)` for `foo(null)`

But if the candidates are unrelated, for example:

- `foo(String)`
- `foo(Integer)`

then ambiguity should remain.

### 5. Do not attempt full interface graph ranking beyond what Java assignability already proves

Important boundary:

- this pass is not trying to score every overload with a synthetic numeric cost model
- it only chooses a winner when assignability proves one signature is strictly more specific

That keeps the behavior principled and easier to reason about.

## Testing Strategy

Host tests first for the JS/runtime-visible parts:

- wrapper object still resolves through the default direct-invoke path
- `null` stays valid for reference targets and invalid for primitive-only targets

Host-side fake resolver tests can also validate the intended end result for:

- superclass/object fallback
- interface/object fallback

Native resolver behavior should then be exercised through real Android validation when practical, because this pass's core improvement lives in reflected assignability logic.

## Boundary

This pass does not claim:

- full Frida-grade overload scoring
- arbitrary best-match ranking across all unrelated reference overloads
- any `Env` architecture redesign

If Java assignability cannot prove a unique best match:

- ambiguity remains the correct behavior

## Recommendation

Implement real reference assignability matching and conservative specificity comparison in the native resolver.

This gives the next meaningful Frida-aligned gain without turning overload resolution into a large rewrite or a bag of special cases.
