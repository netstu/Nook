function safeClassLoaderReady() {
  return typeof Java._isClassLoaderReady === "function"
    ? String(Java._isClassLoaderReady())
    : "missing";
}

function safeAppReady() {
  return typeof Java._isAppReady === "function"
    ? String(Java._isAppReady())
    : "missing";
}

function safeLifecycleReady() {
  return typeof Java._isLifecycleReady === "function"
    ? String(Java._isLifecycleReady())
    : "missing";
}

send({
  type: "send",
  payload:
    "java-ready-lifecycle-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java.performNow) + ":" +
    (typeof Java._isClassLoaderReady) + ":" +
    (typeof Java._isAppReady) + ":" +
    (typeof Java._isLifecycleReady)
});

send({
  type: "send",
  payload:
    "java-ready-lifecycle-script-enter:" +
    safeClassLoaderReady() + ":" +
    safeAppReady() + ":" +
    safeLifecycleReady()
});

Java.performNow(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();

  send({
    type: "send",
    payload:
      "java-ready-lifecycle-perform-now:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      safeLifecycleReady() + ":" +
      String(app !== null && app !== undefined)
  });
});

Java.ready(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();
  var loader = app !== null && app !== undefined ? app.getClassLoader() : null;
  var LoginFragment = Java.use("com.demo.target.LoginFragment");

  send({
    type: "send",
    payload:
      "java-ready-lifecycle-fired:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      safeLifecycleReady() + ":" +
      String(app !== null && app !== undefined) + ":" +
      String(loader !== null && loader !== undefined) + ":" +
      LoginFragment.$className + ":" +
      (typeof LoginFragment.verifyPasswordNative)
  });
});
