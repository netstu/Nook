# Java static native hook no-hit report was invalidated by later runs

Date: 2026-05-02

## Summary

This was not a stable remaining bug.

Later real-device validation showed that Nook can install and hit the static native Java hook for `com.zj.wuaipojie.util.SecurityUtil.check(String)`. The earlier "installs but never hits" report was invalidated by later runs and should not be treated as a current compatibility boundary.

## Target

Validated method:

- class: `com.zj.wuaipojie.util.SecurityUtil`
- method: `check`
- signature: `(Ljava/lang/String;)Z`
- modifiers: `static native`

Observed script shape:

```javascript
Java.perform(function () {
  var SecurityUtil = Java.use("com.zj.wuaipojie.util.SecurityUtil");
  SecurityUtil.check.overload("java.lang.String").implementation = function (str) {
    console.log("check is called: " + str);
    return true;
  };
});
```

## Updated findings

- the target APK really does expose `SecurityUtil.check(String)` as `public static native`
- the exact-signature static wrapper installs successfully
- later device runs proved that real target-side button clicks do enter the JS callback
- callback diagnostics also showed that `callOriginal(...)` executed and returned the original `boolean` result

Example later diagnostic output:

- `diag:securityutil:installed:(Ljava/lang/String;)Z:true`
- `diag:securityutil:enter:Hello`
- `diag:securityutil:leave-original:false`

References:

- [00001230.java](/E:/Learn/Lessons/安卓逆向这档事/013第十三节.是时候学习一下Frida一把梭了(上)/教程demo.apk.cache/sources/30/00001230.java)
- [static-native-check-diag.js](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/Test_Lab/nook-frida-articles/raw-scripts/article-15/static-native-check-diag.js)

## Page mapping note

Manual testing hit page-number confusion, so the real callsite matters more than the visible "第几关" label.

APK decompilation shows:

- `ChallengeEight` owns the `SecurityUtil.check(editText.getText().toString())` callsite
- that page layout includes:
  - one `EditText`
  - one `so_check` button

References:

- [000011fd.java](/E:/Learn/Lessons/安卓逆向这档事/013第十三节.是时候学习一下Frida一把梭了(上)/教程demo.apk.cache/sources/fd/000011fd.java)
- [000011e2.java](/E:/Learn/Lessons/安卓逆向这档事/013第十三节.是时候学习一下Frida一把梭了(上)/教程demo.apk.cache/sources/e2/000011e2.java)

User-observed UI labels during manual testing were:

- one page with:
  - one input box
  - one "验证" button
- another page with:
  - `vip`
  - `会员等级`
  - `钻石数量`
  - one "验证" button

Future debugging should key off the actual class and method callsite, not only the visible page description.

## Conclusion

Current status:

- exact-signature static Java hook install for this target: works
- static native Java callback hit for `SecurityUtil.check(String)`: works on validated device runs

This document remains only as a historical note so the earlier report is not rediscovered and debugged again as if it were still current.

## Remaining work

1. Keep a dedicated regression script for `SecurityUtil.check(String)` static-hook hit behavior
2. Update broader docs that still imply "static install works but real hit does not"
3. Continue focusing on still-open compatibility gaps instead of this already-validated path
