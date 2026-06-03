# Nook Java.overload Real Device Validation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add one stable real-device validation path proving `Java.use(...).method.overload(...)` can distinguish two real Java overloads in `TargetDemoApp`.

**Architecture:** Reuse `TextFragment` as the trigger surface. Add a second `formatBalance(...)` overload in the demo app, make the existing `double` overload call into the new `String` overload, then add a host smoke script that installs both exact-signature hooks and logs each path independently.

**Tech Stack:** Java (Android demo app), QuickJS-based Nook runtime, Python host smoke scripts, existing `nook-cli attach --wait --usb` workflow

---

### Task 1: Add a real overloaded Java target in `TargetDemoApp`

**Files:**
- Modify: `E:\Learn\my_program\all_my_hook\TargetAppDemo\TargetDemoApp\src\main\java\com\demo\target\TextFragment.java`

**Step 1: Keep the existing double overload as the page entrypoint**

Ensure `updateUserInfo()` still calls:

```java
tvBalance.setText(formatBalance(BALANCE));
```

**Step 2: Add a second overload**

Add:

```java
public String formatBalance(String amountText) {
    return "楼 " + amountText;
}
```

**Step 3: Route the existing overload through the new one**

Change:

```java
public String formatBalance(double amount) {
    return formatBalance(String.format("%.2f", amount));
}
```

**Step 4: Verify the page trigger path stays simple**

No new button, no new fragment field, no extra JNI. Entering the `TextFragment` page should be enough to trigger both overloads.

### Task 2: Add a host smoke script for both overloads

**Files:**
- Create: `host/nook-py/java_overload_textfragment_smoke.js`

**Step 1: Bind both overload wrappers**

Use:

```javascript
const TextFragment = Java.use("com.demo.target.TextFragment");
const byDouble = TextFragment.formatBalance.overload("double");
const byString = TextFragment.formatBalance.overload("java.lang.String");
```

**Step 2: Emit wrapper metadata**

Send messages proving exact signatures:

```javascript
send({ type: "send", payload: "text-overload-wrapper-double:" + byDouble.$signature });
send({ type: "send", payload: "text-overload-wrapper-string:" + byString.$signature });
```

Expected signatures:

- `(D)Ljava/lang/String;`
- `(Ljava/lang/String;)Ljava/lang/String;`

**Step 3: Install both implementations**

Double overload:

```javascript
byDouble.implementation = function (amount) {
  send({ type: "send", payload: "text-overload-double-enter:" + amount });
  const original = this.formatBalance.callOriginal(amount);
  send({ type: "send", payload: "text-overload-double-leave:" + original });
  return original;
};
```

String overload:

```javascript
byString.implementation = function (amountText) {
  send({ type: "send", payload: "text-overload-string-enter:" + amountText });
  const original = this.formatBalance.callOriginal(amountText);
  send({ type: "send", payload: "text-overload-string-leave:" + original });
  return original;
};
```

**Step 4: Emit install-complete marker**

```javascript
send({ type: "send", payload: "text-overload-installed" });
```

### Task 3: Update verification docs

**Files:**
- Modify: `host/nook-py/README.md`
- Modify: `docs/code_review.md`

**Step 1: Add the new smoke command**

Document:

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_overload_textfragment_smoke.js --wait --usb
```

**Step 2: Add the user action**

Document that the tester must switch to the `TextFragment` tab/page.

**Step 3: Add expected output**

Initial:

- `text-overload-wrapper-double:(D)Ljava/lang/String;`
- `text-overload-wrapper-string:(Ljava/lang/String;)Ljava/lang/String;`
- `text-overload-installed`

After opening `TextFragment`:

- `text-overload-double-enter:10`
- `text-overload-string-enter:10.00`
- `text-overload-string-leave:楼 10.00`
- `text-overload-double-leave:楼 10.00`

### Task 4: Real-device validation handoff

**Files:**
- None

**Step 1: Rebuild and deploy**

Rebuild the demo app with the new `TextFragment` overloads, then reinstall / redeploy it by the normal user workflow.

**Step 2: Run server and attach**

Run:

```powershell
adb shell "su -c 'pkill -f /data/local/tmp/nook/nook-server 2>/dev/null || true'"
adb shell am force-stop com.demo.target
adb shell am start -n com.demo.target/.MainActivity
adb shell "su -c 'LD_LIBRARY_PATH=/data/local/tmp/nook /system/bin/linker64 /data/local/tmp/nook/nook-server'"
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_overload_textfragment_smoke.js --wait --usb
```

**Step 3: Trigger both overloads**

Switch to the `TextFragment` page.

**Step 4: Verify exact behavior**

Expected:

- both overload wrappers install
- both overload callbacks fire
- `callOriginal(...)` on each overload returns the correct original result
- app remains stable
