send({
  type: "send",
  payload:
    "java-env-wrapper-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv) + ":" +
    (typeof Java.vm.tryGetEnv)
});

var env = Java.vm.getEnv();

send({
  type: "send",
  payload:
    "java-env-wrapper-direct:" +
    (typeof env) + ":" +
    (typeof env.handle) + ":" +
    env.toString() + ":" +
    env.handle.toString()
});

send({
  type: "send",
  payload:
    "java-env-wrapper-exception:" +
    String(env.exceptionCheck())
});

var stringClass = env.findClass("java/lang/String");

send({
  type: "send",
  payload:
    "java-env-wrapper-findclass:" +
    stringClass.toString()
});

var tryEnv = Java.vm.tryGetEnv();

send({
  type: "send",
  payload:
    "java-env-wrapper-try:" +
    String(tryEnv === null) + ":" +
    (tryEnv === null ? "null" : tryEnv.toString())
});
