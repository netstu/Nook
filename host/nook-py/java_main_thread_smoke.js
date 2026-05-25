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
    "java-main-thread-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.performNow) + ":" +
    (typeof Java.isMainThread) + ":" +
    (typeof Java.scheduleOnMainThread) + ":" +
    (typeof Java.ready)
});

send({
  type: "send",
  payload:
    "java-main-thread-script-enter:" +
    safeClassLoaderReady() + ":" +
    safeAppReady()
});

Java.performNow(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();

  send({
    type: "send",
    payload:
      "java-main-thread-immediate:" +
      String(Java.isMainThread()) + ":" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      String(app !== null && app !== undefined)
  });

  Java.scheduleOnMainThread(function () {
    send({
      type: "send",
      payload:
        "java-main-thread-scheduled:" +
        String(Java.isMainThread())
    });
  });

  send({
    type: "send",
    payload: "java-main-thread-after-schedule"
  });
});

Java.ready(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();

  send({
    type: "send",
    payload:
      "java-main-thread-ready-fired:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      String(app !== null && app !== undefined)
  });
});
