send({
  type: "send",
  payload:
    "java-callable-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java.use) + ":" +
    String(Java._isClassLoaderReady())
});

Java.ready(function () {
  send({
    type: "send",
    payload: "java-callable-ready-enter"
  });

  const Log = Java.use("android.util.Log");
  const debug = Log.d.overload("java.lang.String", "java.lang.String");

  send({
    type: "send",
    payload:
      "java-callable-wrapper:" +
      (typeof debug) + ":" +
      debug.$signature + ":" +
      String(debug.$isStatic) + ":" +
      (typeof debug.callOriginal)
  });

  const result = debug("NookCallable", "hello-from-nook");

  send({
    type: "send",
    payload: "java-callable-result:" + String(result)
  });
});
