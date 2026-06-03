# Nook Java.array Reference-Array Covariance Design

## Goal

Bring `Java.array(typeName, elements)` one step closer to Frida by handling more general
reference-array covariance during Java invocation, without changing Nook's current
`Java.array(...)` JS-facing shape.

## Confirmed Scope

This pass will support:

- converting `Java.array('java.lang.String', [...])` into targets like:
  - `CharSequence[]`
  - `Object[]`
- recursive conversion for nested reference arrays like:
  - `String[][] -> CharSequence[][]`
  - `String[][] -> Object[][]`
- reusing the existing reflective assignability logic already used by overload resolution

This pass will not support:

- primitive-array covariance
- fake `registerClass.fields`
- broader `registerClass` architecture changes
- new local-ref or monitor semantics

## Why This Scope

`step6.md` still has one real `Java.array(...)` gap after the mutation work landed:

- richer covariance behavior

The current bridge already handles:

- exact reference-array element descriptors
- the special practical case `String[] -> Object[]`
- recursive nesting when the component descriptors already match

But it still does not generally reuse Java assignability when choosing how to materialize
reference-array elements. That leaves a real Frida-alignment gap for cases like:

- `String[] -> CharSequence[]`

This is a better next step than `registerClass.fields`, because current `registerClass`
is still based on `Proxy.newProxyInstance(...)`, so real Frida-style fields would require
fake semantics or an architectural redesign.

## Recommended Approach

### Approach A: Reuse reflective descriptor assignability in array element conversion

This is the recommended approach.

Flow:

1. `ConvertJavaJsArrayToJniArray(...)` sees a target descriptor like:
   - `[Ljava/lang/CharSequence;`
2. if the source `Java.array(...)` carries a more specific source array type like:
   - `java.lang.String[]`
3. derive the source component descriptor:
   - `Ljava/lang/String;`
4. ask the existing reflective assignability helper whether:
   - `CharSequence <- String`
5. if assignable, convert each JS element using the more specific source descriptor
   instead of the broader target component descriptor

Why this is the right shape:

- stays inside the current Java bridge architecture
- matches the same Java type-assignability rules already used elsewhere
- avoids hardcoding only `Object[]` as the single widened target
- keeps recursive nested-array behavior coherent

## Alternatives Considered

### Approach B: Keep special-casing only `Object[]`

Pros:

- minimal code

Cons:

- leaves obvious Frida-alignment gaps
- inconsistent with the runtime's newer reference-specificity work
- does not scale to `CharSequence[]`, interface arrays, or nested variants

Conclusion:

Not enough.

### Approach C: Add JS-side array wrapper type metadata for every element

Pros:

- could support richer conversions later

Cons:

- larger surface-area change
- unnecessary for this step

Conclusion:

YAGNI for now.

## Testing Strategy

Host tests first:

- add pure bridge-level tests for descriptor acceptability with assignability callbacks
- add focused tests for choosing a more specific source component descriptor

Device smoke second:

- call a target that expects a widened reference-array type while the script builds a
  more specific source array
- verify the invocation succeeds and returns the expected formatted result

## Recommendation

Do this next. It is a real remaining gap from `step6.md`, it is architecturally honest,
and it improves Frida compatibility without introducing fake semantics.
