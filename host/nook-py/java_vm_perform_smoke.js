send({
  type: "send",
  payload:
    "java-vm-perform-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.perform)
});

var seen = [];

Java.vm.perform(function () {
  seen.push("inside");

  var System = Java.use("java.lang.System");
  var ActivityThread = Java.use("android.app.ActivityThread");
  var now = System.currentTimeMillis();
  var app = ActivityThread.currentApplication();

  send({
    type: "send",
    payload:
      "java-vm-perform-callback:" +
      seen.join("|") + ":" +
      String(now > 0) + ":" +
      String(app !== null && app !== undefined)
  });
});

seen.push("after");

send({
  type: "send",
  payload: "java-vm-perform-order:" + seen.join("|")
});
