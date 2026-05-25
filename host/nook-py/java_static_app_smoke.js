Java.perform(function () {
  if (typeof Java.deopt === "function") {
    const deoptResult = Java.deopt();
    send({
      type: "send",
      payload:
        "java-static-app-deopt:" +
        String(deoptResult.ok) + ":" +
        String(deoptResult.invalidated) + ":" +
        String(deoptResult.reason) + ":" +
        String(deoptResult.runtimeOffset)
    });
  }

  const MainActivity = Java.use("com.demo.target.MainActivity");
  const overload = MainActivity.incrementIntercept.overload();

  send({
    type: "send",
    payload:
      "java-static-app-wrapper:" +
      (typeof overload) + ":" +
      overload.$signature + ":" +
      String(overload.$isStatic) + ":" +
      (typeof overload.callOriginal)
  });

  overload.implementation = function () {
    send({
      type: "send",
      payload: "java-static-app-enter"
    });

    const original = this.incrementIntercept.callOriginal();

    send({
      type: "send",
      payload: "java-static-app-leave-original:" + String(original)
    });

    return original;
  };

  send({
    type: "send",
    payload: "java-static-app-installed:" + overload.$signature + ":" + String(overload.$isStatic)
  });
});
