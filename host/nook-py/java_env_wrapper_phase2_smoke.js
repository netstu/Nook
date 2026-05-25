send({
  type: "send",
  payload:
    "java-env-wrapper-phase2-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv) + ":" +
    (typeof Java.vm.tryGetEnv)
});

var env = Java.vm.getEnv();
var pending = env.exceptionOccurred();

send({
  type: "send",
  payload:
    "java-env-wrapper-phase2-direct:" +
    (typeof env) + ":" +
    (typeof env.handle) + ":" +
    env.toString() + ":" +
    env.handle.toString()
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase2-exception-occurred:" +
    pending.toString() + ":" +
    String(pending.isNull())
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase2-exception-clear:" +
    String(env.exceptionClear())
});

var tryEnv = Java.vm.tryGetEnv();

send({
  type: "send",
  payload:
    "java-env-wrapper-phase2-try:" +
    String(tryEnv === null) + ":" +
    (tryEnv === null ? "null" : tryEnv.toString())
});

Java.ready(function () {
  var readyEnv = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var instance = TextFragment.$new();
  var clazz = readyEnv.getObjectClass(instance);

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase2-get-object-class:" +
      (typeof readyEnv.getObjectClass) + ":" +
      instance.$className + ":" +
      clazz.toString()
  });
});
