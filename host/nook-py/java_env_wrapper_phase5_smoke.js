send({
  type: "send",
  payload:
    "java-env-wrapper-phase5-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv) + ":" +
    (typeof Java.vm.tryGetEnv)
});

var env = Java.vm.getEnv();
var jstr = env.newStringUtf("hello");

send({
  type: "send",
  payload:
    "java-env-wrapper-phase5-direct:" +
    (typeof env) + ":" +
    (typeof env.newStringUtf)
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase5-new:" +
    jstr.toString() + ":" +
    String(jstr.isNull())
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase5-exception:" +
    String(env.exceptionCheck())
});
