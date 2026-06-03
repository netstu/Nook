# Java overload crash report was invalidated by later runs

Date: 2026-05-02

## Summary

This was not a stable overload bug.

Later real-device validation showed that the exact-signature overload path for `com.zj.wuaipojie.Demo.a(String)` can install and execute correctly, including `callOriginal(...)`. The earlier crash report should be treated as historical noise from an unstable intermediate state, not as an active compatibility boundary.

## Reproduction

Target method:

- class: `com.zj.wuaipojie.Demo`
- method: `a`
- signature: `(Ljava/lang/String;)Ljava/lang/String;`

Real-device script shape that is expected to work:

```javascript
Java.perform(function () {
  var Demo = Java.use("com.zj.wuaipojie.Demo");
  Demo.a.overload("java.lang.String").implementation = function (str) {
    var newStr = "52pojie";
    return this.a.callOriginal(newStr);
  };
});
```

Updated behavior:

- `Demo.a.overload("java.lang.String").implementation = ...` installs successfully
- callback execution works on device
- `this.a.callOriginal(...)` works on the validated path

## Updated findings

- the target APK really does expose `Demo.a(String)`
- Nook runtime tests and later device runs both align with:
  - `overload("java.lang.String")`
  - `implementation = fn`
  - `callOriginal(...)`
- this item is no longer a good candidate for active debugging

## Conclusion

Current status:

- exact-signature overload install: supported on the validated article path
- overload callback dispatch: supported on the validated article path
- overload `callOriginal(...)`: supported on the validated article path

This document remains only as a historical note so the earlier report is not rediscovered and debugged again as if it were still current.

## Remaining work

1. Keep overload regressions covered by tests so this does not regress silently
2. Focus future Java compatibility work on still-open gaps such as constructor hooks and broader wrapper parity
