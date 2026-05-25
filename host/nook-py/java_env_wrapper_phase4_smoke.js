send({
  type: "send",
  payload:
    "java-env-wrapper-phase4-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv) + ":" +
    (typeof Java.vm.tryGetEnv)
});

Java.ready(function () {
  var env = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
  var instance = TextFragment.$new();

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase4-direct:" +
      (typeof env) + ":" +
      (typeof env.isInstanceOf)
  });

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase4-own:" +
      String(env.isInstanceOf(instance, TextFragment))
  });

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase4-other:" +
      String(env.isInstanceOf(instance, LoginFragment))
  });

  instance.$dispose();
});
