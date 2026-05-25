send({
  type: "send",
  payload:
    "java-env-diag-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv)
});

send({
  type: "send",
  payload: "java-env-diag-step:getEnv-enter"
});

var env = Java.vm.getEnv();

send({
  type: "send",
  payload:
    "java-env-diag-step:getEnv-ok:" +
    (typeof env) + ":" +
    env.toString()
});

send({
  type: "send",
  payload:
    "java-env-diag-step:handle-ok:" +
    (typeof env.handle) + ":" +
    env.handle.toString()
});

send({
  type: "send",
  payload: "java-env-diag-step:exceptionCheck-enter"
});

var hasException = env.exceptionCheck();

send({
  type: "send",
  payload:
    "java-env-diag-step:exceptionCheck-ok:" +
    String(hasException)
});

send({
  type: "send",
  payload: "java-env-diag-step:findClass-enter"
});

var stringClass = env.findClass("java/lang/String");

send({
  type: "send",
  payload:
    "java-env-diag-step:findClass-ok:" +
    stringClass.toString()
});
