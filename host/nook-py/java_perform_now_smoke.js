send({
  type: "send",
  payload:
    "java-perform-now-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.performNow) + ":" +
    (typeof Java.use) + ":" +
    String(Java._isClassLoaderReady())
});

var seen = [];

Java.performNow(function () {
  seen.push("inside");

  var System = Java.use("java.lang.System");
  var ActivityThread = Java.use("android.app.ActivityThread");
  var now = System.currentTimeMillis();
  var app = ActivityThread.currentApplication();

  send({
    type: "send",
    payload:
      "java-perform-now-callback:" +
      seen.join("|") + ":" +
      String(now > 0) + ":" +
      String(app !== null && app !== undefined)
  });
});

seen.push("after");

send({
  type: "send",
  payload: "java-perform-now-order:" + seen.join("|")
});
