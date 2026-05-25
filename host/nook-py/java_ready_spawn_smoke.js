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

send({
  type: "send",
  payload:
    "java-ready-spawn-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java.performNow) + ":" +
    (typeof Java._isClassLoaderReady) + ":" +
    (typeof Java._isAppReady)
});

send({
  type: "send",
  payload:
    "java-ready-spawn-script-enter:" +
    safeClassLoaderReady() + ":" +
    safeAppReady()
});

Java.performNow(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();

  send({
    type: "send",
    payload:
      "java-ready-spawn-perform-now:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
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
      "java-ready-spawn-fired:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      String(app !== null && app !== undefined) + ":" +
      String(loader !== null && loader !== undefined) + ":" +
      LoginFragment.$className + ":" +
      (typeof LoginFragment.verifyPasswordNative)
  });
});
