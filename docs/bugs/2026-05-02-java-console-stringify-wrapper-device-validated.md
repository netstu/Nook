# Java wrapper stringify report was invalidated by later runs

Date: 2026-05-02

## Summary

This is no longer an active compatibility bug for the validated article path.

Later runtime and real-device validation showed that Nook can now stringify:

- Java class wrappers
- reflected Java method arrays returned by `Class.getDeclaredMethods()`
- reflected Java method instances inside those arrays

The earlier report about wrapper stringify being broadly unsupported is now historical.

## Validated reproduction

Target script:

- [block-14.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/Test_Lab/nook-frida-articles/raw-scripts/article-13/block-14.js)

Validated device-side output shape:

```text
com.zj.wuaipojie.Demo
<JavaClass com.zj.wuaipojie.Demo>
<JavaObject java.lang.reflect.Method>,...
enum-complete
```

This proves:

- `Java.enumerateLoadedClasses(...)` runs correctly
- `Java.use(name)` stringifies as a class wrapper
- `clazz.class.getDeclaredMethods()` no longer misclassifies the returned array as a class wrapper
- reflected method instances no longer stringify as `<JavaClass ...>`

## What was fixed

1. Java reflection arrays returned from JNI are now converted to JS arrays instead of plain Java objects
2. Java wrapper fallback formatting now distinguishes:
   - class wrappers -> `<JavaClass ...>`
   - instance wrappers -> `<JavaObject ...>`
3. Java instance wrapper `toString()` now distinguishes receiver-backed wrappers from class wrappers

Relevant files:

- [src/agent_runtime/nook_java_js_bridge.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/nook_java_js_bridge.cpp)
- [src/agent_runtime/js_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/agent_runtime/js_runtime.cpp)
- [tests/communication/test_js_runtime_native_attach.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_js_runtime_native_attach.cpp)

## Conclusion

Current validated status:

- `console.log(clazz)`: supported on the validated path
- `console.log(methods)` where `methods` is `clazz.class.getDeclaredMethods()`: supported on the validated path
- `String(methods[0])` for reflected method instances: supported on the validated path

This item should no longer be treated as an open blocker for article-13 `block-14`.

## Remaining limitations

This validation does not claim full Frida parity for every possible Java reflection object display case.

Still unproven here:

1. richer pretty-printing of reflection objects beyond wrapper labels
2. stable stringify for every exotic reflected object graph
3. Frida-identical inspect formatting

But the previously reported crash / misclassification path is fixed.
