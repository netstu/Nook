# Nook Java.registerClass Method Spec Alignment Design

## Goal

Bring `Java.registerClass(spec)` closer to Frida by supporting Frida-style method declarations in `spec.methods`, without changing Nook's current proxy-based architecture.

## Confirmed Scope

This pass will support:

- existing form:
  - `methods: { onClick: function (...) { ... } }`
- new Frida-style declaration object form:
  - `methods: { onClick: { returnType: 'void', argumentTypes: ['android.view.View'], implementation: function (view) { ... } } }`
- new Frida-style declaration array form:
  - `methods: { onClick: [{ returnType: 'void', argumentTypes: ['android.view.View'], implementation: function (view) { ... } }] }`
- preserving the current proxy/listener instantiation path through `$new()`

This pass will not support:

- `fields`
- `superClass` / `extends`
- constructor declarations
- true Java class generation
- Java-side overload dispatch by signature

## Why This Scope

Nook's current `registerClass` implementation is a JS-to-native-to-Java proxy bridge. It produces a real Java proxy object that can satisfy interface/listener scenarios, but it does not generate a real named Java class.

Because of that, the safest Frida-aligned improvement is to accept the same `methods` declaration shapes that scripts already expect, while keeping dispatch semantics unchanged:

- one JS callback selected by method name
- no runtime overload selection by JNI signature

This improves compatibility with Frida-style scripts without pretending Nook already supports the rest of Frida's dynamic class model.

## API Shape

Supported examples after this change:

```javascript
var KlassA = Java.registerClass({
  name: 'com.nook.ProxyClickListener',
  implements: [OnClickListener],
  methods: {
    onClick: function (view) {
      return undefined;
    }
  }
});

var KlassB = Java.registerClass({
  name: 'com.nook.ProxyClickListener',
  implements: [OnClickListener],
  methods: {
    onClick: {
      returnType: 'void',
      argumentTypes: ['android.view.View'],
      implementation: function (view) {
        return undefined;
      }
    }
  }
});

var KlassC = Java.registerClass({
  name: 'com.nook.ProxyClickListener',
  implements: [OnClickListener],
  methods: {
    onClick: [{
      returnType: 'void',
      argumentTypes: ['android.view.View'],
      implementation: function (view) {
        return undefined;
      }
    }]
  }
});
```

## Semantics

- Plain function form remains unchanged.
- Declaration object form is accepted if:
  - `implementation` is a function
  - `returnType`, if present, is a string
  - `argumentTypes`, if present, is an array of strings
- Declaration array form is accepted only when it contains exactly one declaration.
- Multiple declarations for the same method name are rejected for now.

## Why Reject Multiple Declarations

Frida's full model can carry richer overload metadata. Nook's current proxy dispatch path cannot reliably choose among multiple JS implementations at callback time because the Java proxy callback currently dispatches by method name only.

Accepting multiple declarations now would create misleading compatibility. Rejecting them is the correct boundary until callback dispatch grows a signature-aware path.

## Implementation Shape

The change stays in the current layers:

- bootstrap:
  - `Java.registerClass(spec)` keeps forwarding `spec.methods` as-is
- runtime parsing:
  - `CollectJavaRegisterClassMethods(...)` learns to normalize each method entry into:
    - callback function
    - optional metadata validation
- bridge request:
  - keep existing `JavaJsRegisteredClassMethodRecord`
  - preserve current native callback dispatch by method name

No Android helper-dex or proxy-instantiation redesign is needed.

## Error Handling

Reject with clear errors when:

- a method declaration object is missing `implementation`
- `implementation` is not a function
- `returnType` is present but not a string
- `argumentTypes` is present but not an array of strings
- a declaration array is empty
- a declaration array contains more than one declaration

## Testing Strategy

Host tests first:

- existing plain-function tests must keep passing
- declaration object form should forward method name and callback
- declaration array form with one entry should work
- invalid declaration shapes should fail with stable messages
- multiple declarations should fail explicitly

Device smoke second:

- reuse the existing listener callback path
- add one smoke script using declaration object form

## Recommendation

This is the right next step because it improves Frida script compatibility without expanding Nook's architecture surface or introducing fake `fields` semantics.
