Java.perform(function () {
  function installStaticHook(target) {
    const klass = Java.use(target.className);
    const method = klass[target.methodName];
    const overload = method.overload.apply(method, target.argTypes);

    send({
      type: "send",
      payload:
        "java-static-wrapper:" +
        target.label + ":" +
        (typeof overload) + ":" +
        overload.$signature + ":" +
        String(overload.$isStatic) + ":" +
        (typeof overload.callOriginal)
    });

    overload.implementation = function () {
      const args = Array.prototype.slice.call(arguments);
      send({
        type: "send",
        payload: "java-static-enter:" + target.label + ":" + args.join("|")
      });

      const original = this[target.methodName].callOriginal.apply(
        this[target.methodName],
        args
      );

      send({
        type: "send",
        payload: "java-static-leave-original:" + target.label + ":" + String(original)
      });

      return original;
    };

    send({
      type: "send",
      payload:
        "java-static-installed:" +
        target.label + ":" +
        overload.$signature + ":" +
        String(overload.$isStatic)
    });
  }

  try {
    installStaticHook({
      label: "main-activity",
      className: "com.demo.target.MainActivity",
      methodName: "incrementIntercept",
      argTypes: []
    });
  } catch (e) {
    send({
      type: "send",
      payload: "java-static-fallback:" + String(e)
    });

    installStaticHook({
      label: "android-log-d",
      className: "android.util.Log",
      methodName: "d",
      argTypes: ["java.lang.String", "java.lang.String"]
    });
  }
});
