send({
  type: "send",
  payload:
    "java-env-wrapper-phase6-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv) + ":" +
    (typeof Java.vm.tryGetEnv)
});

var env = Java.vm.getEnv();
var jstr = env.newStringUtf("hello");
var cstr = env.getStringUtfChars(jstr);
var released = env.releaseStringUtfChars(jstr, cstr);

send({
  type: "send",
  payload:
    "java-env-wrapper-phase6-direct:" +
    (typeof env) + ":" +
    (typeof env.newStringUtf) + ":" +
    (typeof env.getStringUtfChars) + ":" +
    (typeof env.releaseStringUtfChars)
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase6-new:" +
    jstr.toString() + ":" +
    String(jstr.isNull())
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase6-chars:" +
    cstr.toString() + ":" +
    String(cstr.isNull())
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase6-release:" +
    String(released)
});

send({
  type: "send",
  payload:
    "java-env-wrapper-phase6-exception:" +
    String(env.exceptionCheck())
});
