Java.perform(function () {
  if (typeof Java.deopt === "function") {
    const deoptResult = Java.deopt();
    send({
      type: "send",
      payload:
        "java-static-log-deopt:" +
        String(deoptResult.ok) + ":" +
        String(deoptResult.invalidated) + ":" +
        String(deoptResult.reason) + ":" +
        String(deoptResult.runtimeOffset)
    });
  }

  const Log = Java.use("android.util.Log");
  const overload = Log.d.overload("java.lang.String", "java.lang.String");

  send({
    type: "send",
    payload:
      "java-static-log-wrapper:" +
      (typeof overload) + ":" +
      overload.$signature + ":" +
      String(overload.$isStatic) + ":" +
      (typeof overload.callOriginal)
  });

  overload.implementation = function (tag, message) {
    send({
      type: "send",
      payload: "java-static-log-enter:" + tag + ":" + message
    });

    const original = this.d.callOriginal(tag, message);

    send({
      type: "send",
      payload: "java-static-log-leave-original:" + String(original)
    });

    return original;
  };

  send({
    type: "send",
    payload: "java-static-log-installed:" + overload.$signature + ":" + String(overload.$isStatic)
  });
});
