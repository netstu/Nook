# Java constructor hook report was invalidated by later device runs

Date: 2026-05-02

## Summary

This is no longer an active compatibility gap.

Later real-device validation showed that Nook can install and execute Java constructor hooks through `$init`, including callback-side argument rewrite by calling `this.$init(...)` from inside the constructor hook body.

## Earlier report

An earlier report treated constructor hooks as unsupported because:

- the callback either did not visibly fire on some runs, or
- callback-side `this.$init(...)` recursion/original-call routing was not yet stable

That earlier conclusion should no longer be used as the current boundary.

## Validated capability

Validated shape:

```javascript
Java.perform(function () {
  var Checker = Java.use("com.ad2001.frida0x7.Checker");
  Checker.$init.implementation = function (param) {
    this.$init(600, 600);
  };
});
```

Validated outcomes:

- constructor hook install succeeds
- constructor callback executes on device
- callback-side `this.$init(...)` reaches the original constructor path
- app behavior matches the upstream Frida 0x7 challenge intent

## Updated conclusion

Current status:

- ordinary Java method hook: supported
- constructor hook through `$init`: supported on validated device paths

## Follow-up

1. Keep `frida-0x7` marked as `supported`
2. Keep using device-validated constructor-hook scripts in the lab mapping
3. Focus future Java compatibility work on remaining gaps such as reflection stringify and broader wrapper parity
