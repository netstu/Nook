# Java.choose instance field write report was too broad

Date: 2026-05-02

## Summary

The original report overstated the bug.

Later device validation showed that Nook can read and write private instance fields when the code is operating on the active live receiver instance inside a Java hook callback. The article-demo reproduction that motivated this note was mainly an object-identity / timing mismatch, not proof that the field bridge itself was broken.

## Reproduction

Observed script shape:

```javascript
function hookTest5(){
    Java.perform(function(){
        var utils = Java.use("com.zj.wuaipojie.Demo");
        utils.staticField.value = "我是被修改的静态变量";

        Java.choose("com.zj.wuaipojie.Demo", {
            onMatch: function(obj){
                obj._privateInt.value = "123456";
                obj.privateInt.value = 9999;
            },
            onComplete: function(){
            }
        });
    });
}
```

Observed behavior:

- static field mutation is reflected by target logs
- mutating an object returned by `Java.choose(...)` did not change the later log output that came from a different `new Demo()` instance

## Updated findings

- `this.privateInt.value` read/write works on the real device inside an active hook callback receiver
- `_privateInt` alias fallback and declared-field reflection lookup were added for Frida-style private-field access
- the validation script below now works on device:
  - [private-field-instance-verify.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/Test_Lab/nook-frida-articles/raw-scripts/article-13/private-field-instance-verify.js)
- host regression coverage now also locks the same-object `Java.choose(...)` path by retaining the matched object and re-reading the mutated field after `onMatch`:
  - [test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)
- the original article repro mixed:
  - one `Demo` instance obtained through `Java.choose(...)`
  - a later different `Demo` instance created by the app and then logged

That means the old repro could show "static field changed but instance field did not" even when the bridge write itself had succeeded on the earlier matched object.

Reference:

- [host/nook-py/README.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/README.md)

## Conclusion

Current status:

- static field write: supported
- instance field write inside a live Java callback receiver: supported
- the old article repro is not valid evidence of a bridge-wide field-write failure

Remaining caution:

- when a script mutates one instance and the app later logs another instance, the result is expected to look "not effective"

## Recommended usage

If the goal is to prove or rely on instance-field mutation, prefer:

1. Hook a normal instance method on the exact target object
2. Read/write the field through `this.fieldName.value` inside that callback

Use `Java.choose(...)` carefully when the app may later create a fresh instance and log that new object instead.

## Future work

1. Expand field regression coverage beyond `int` / `String`
2. Keep clarifying object-identity pitfalls in article-compatible examples
