# Nook Java Overload Resolution Tightening Design

## Goal

Continue aligning Nook's Java direct-invoke path with Frida by tightening overload resolution in the most common ambiguous cases, without introducing Nook-only fake semantics.

## Problem

Nook's current Java invoke path already supports:

- exact `.overload(...)`
- primitive numeric fallback
- primitive/object arrays
- static fallback from instance lookup

But its default direct invoke path is still too narrow in several real cases:

- `null` cannot participate in overload resolution at all
- JS booleans and numbers only infer primitive candidates, not boxed/object candidates
- JS strings only infer `java.lang.String`
- object wrappers only contribute their concrete class name, not even `java.lang.Object`
- the native resolver still effectively reasons in an "exact descriptor plus Object/array exception" model

This leaves a gap between:

- "exact wrapper path works"
- "real Frida-style direct invoke is robust in messy app code"

## Scope

This pass will tighten only the default direct invoke resolution path used by:

- `Java.use(...).method(...)`
- instance wrappers inside hook callbacks
- loader-aware `ClassFactory` wrappers

This pass will not change:

- explicit `.overload(...)`
- hook install exact-signature path
- `registerClass(...)`
- JNI `Env` architecture boundaries like local refs or monitor pairing

## Approaches Considered

### Approach 1: Keep adding one-off JS-side heuristics

Examples:

- special-case `null`
- special-case boxed numerics
- special-case strings-to-Object

Pros:

- smallest patch

Cons:

- quickly becomes a pile of unrelated exceptions
- hard to reason about
- drifts away from Frida's "real overload compatibility" direction

Conclusion:

Not recommended.

### Approach 2: Expand candidate generation and add minimal native-side reference compatibility

This is the recommended approach.

JS runtime changes:

- enrich inferred candidates for primitive JS values
- add a dedicated internal `null` candidate
- always include safe common-reference fallbacks like `java.lang.Object`

Native resolver changes:

- treat the internal `null` candidate as matching reference parameters but not primitive parameters
- keep current exact/array/Object behavior
- only add the smallest extra compatibility needed by this pass

Pros:

- preserves the current architecture
- gives immediate user-visible improvement
- avoids pretending Nook has a full reflected overload scorer already

Cons:

- still not full Frida overload semantics
- some multi-reference `null` cases may remain ambiguous

Conclusion:

Best trade-off for this phase.

### Approach 3: Build a full reflected overload scoring engine now

Pros:

- closest to full Frida semantics

Cons:

- larger rewrite
- much broader validation burden
- higher regression risk across already-stable Java paths

Conclusion:

Good long-term direction, not the right size for this pass.

## Recommended Design

### 1. Enrich JS-side candidate generation

Keep the current candidate-order model, but broaden the candidate sets.

#### `null`

When a JS argument parses to `JavaJsValueKind::kUndefined` because the source value is `null` or `undefined`:

- emit a dedicated internal candidate token
- do not throw "cannot infer overload" immediately

Target behavior:

- `null` participates in overload resolution for reference types
- `null` remains invalid for purely primitive overload sets

#### `boolean`

Current:

- `boolean`

New candidate order:

1. `boolean`
2. `java.lang.Boolean`
3. `java.lang.Object`

#### `string`

Current:

- `java.lang.String`

New candidate order:

1. `java.lang.String`
2. `java.lang.CharSequence`
3. `java.lang.Object`

#### numeric JS values

Current numeric behavior is already directionally correct for primitive overloads, but too narrow for boxed/object signatures.

Broaden candidates while preserving primitive-first ordering.

Example for integral JS number:

1. primitive exact-ish candidates (`int`, `long`, then current float/double fallbacks in existing order)
2. boxed numeric wrappers
3. `java.lang.Number`
4. `java.lang.Object`

Example for fractional JS number:

1. `double`
2. `float`
3. boxed wrappers
4. `java.lang.Number`
5. `java.lang.Object`

This keeps current primitive wins stable while unlocking object-facing overloads.

#### object wrappers

Current:

- only concrete `$className`

New:

1. concrete wrapper class name
2. `java.lang.Object`

This is intentionally small. This pass will not attempt to synthesize full superclass/interface chains in JS.

#### arrays

Current array handling is already much stronger than the generic object path.

Keep:

1. concrete array type name

Add:

2. `java.lang.Object`

Do not invent broader fake array supertypes in JS; existing native descriptor compatibility already handles important array-object cases.

### 2. Add internal native handling for the `null` candidate

`ResolveJavaMethodSignatureByTypeNames(...)` currently converts every candidate type-name through `TypeNameToDescriptor(...)`.

This pass should add one internal token, for example:

- `__nook_null__`

Resolver rule:

- matches any non-primitive parameter descriptor
- never matches primitive parameters

This keeps `null` behavior explicit and avoids overloading real Java type names.

### 3. Preserve current ambiguity behavior where we still lack full ranking

Important boundary:

- if `null` can match multiple unrelated reference overloads
- and we do not yet have a principled "most specific reference type" ranking

then ambiguity is preferable to silently picking the wrong overload.

This is closer to Frida's correctness direction than hardcoding a Nook-only preference.

### 4. Keep explicit `.overload(...)` as the escape hatch

This pass is about making default invoke stronger, not eliminating the need for explicit exact overload selection.

When users need exact control:

- `.overload(...)` remains the authoritative path

## Testing Strategy

Host-side first.

Add red/green coverage for:

- `null` resolving to a nullable overload
- `null` still failing against primitive-only overloads
- JS boolean resolving to boxed/object-facing overloads
- JS string resolving to `Object` or `CharSequence` facing overloads
- JS number resolving to boxed/object-facing overloads without regressing existing primitive preference
- object wrappers preserving concrete-class-first but still allowing `Object` fallback

Keep existing tests green for:

- primitive overload selection
- array overload selection
- static fallback
- loader-aware invoke paths

## Boundary

This pass intentionally does not claim:

- full Frida-grade reflected overload scoring
- complete inheritance/interface ranking for arbitrary object wrappers
- automatic "most specific reference overload" selection in every ambiguous `null` case

It is a tightening pass, not a full overload-engine rewrite.

## Recommendation

Implement Approach 2:

- broaden JS candidate generation
- add internal `null` support in the native resolver
- keep ambiguity behavior conservative where full ranking is not yet justified

This is the best next step toward Frida without crossing into unstable or fake semantics.
