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
    "java-auto-cleanup-bindings:" +
    (typeof Java.retain) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java.performNow) + ":" +
    String(Java._invokeResolverVersion)
});

send({
  type: "send",
  payload:
    "java-auto-cleanup-script-enter:" +
    safeClassLoaderReady() + ":" +
    safeAppReady()
});

Java.performNow(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();

  send({
    type: "send",
    payload:
      "java-auto-cleanup-perform-now:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      String(app !== null && app !== undefined)
  });
});

Java.ready(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();
  var loader = app !== null && app !== undefined ? app.getClassLoader() : null;
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var initView = TextFragment.initView.overload("android.view.View");
  var retainedOnce = false;

  send({
    type: "send",
    payload:
      "java-auto-cleanup-ready-fired:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      String(app !== null && app !== undefined) + ":" +
      String(loader !== null && loader !== undefined)
  });

  initView.implementation = function (view) {
    if (!retainedOnce) {
      retainedOnce = true;
      var kept = Java.retain(this);
      send({
        type: "send",
        payload:
          "java-auto-cleanup-retained:" +
          String(kept.__nookJavaOwnedHandle) + ":" +
          kept.__nookJavaReceiverHandle + ":" +
          String(kept.__nookJavaWeakToken)
      });
    }

    return this.initView.callOriginal(view);
  };

  send({
    type: "send",
    payload: "java-auto-cleanup-installed"
  });
});
