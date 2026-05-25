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

var order = [];

send({
  type: "send",
  payload:
    "java-ready-lifecycle-order-bindings:" +
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
    "java-ready-lifecycle-order-script-enter:" +
    safeClassLoaderReady() + ":" +
    safeAppReady() + ":" +
    safeLifecycleReady()
});

Java.ready(function () {
  order.push("ready");
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
  send({
    type: "send",
    payload:
      "java-ready-lifecycle-order-callback:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      safeLifecycleReady() + ":" +
      LoginFragment.$className + ":" +
      (typeof LoginFragment.verifyPasswordNative)
  });
});

order.push("after-ready");

Java.performNow(function () {
  order.push("perform-now");
  send({
    type: "send",
    payload:
      "java-ready-lifecycle-order-perform-now:" +
      safeClassLoaderReady() + ":" +
      safeAppReady() + ":" +
      safeLifecycleReady()
  });
});

send({
  type: "send",
  payload: "java-ready-lifecycle-order-seq:" + order.join("|")
});
