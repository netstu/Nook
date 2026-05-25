send({
  type: "send",
  payload:
    "java-env-wrapper-phase3-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv) + ":" +
    (typeof Java.vm.tryGetEnv)
});

Java.ready(function () {
  var env = Java.vm.getEnv();
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var instance = TextFragment.$new();
  var kept = Java.retain(instance);
  var other = TextFragment.$new();

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase3-direct:" +
      (typeof env) + ":" +
      (typeof env.isSameObject)
  });

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase3-same:" +
      String(env.isSameObject(instance, kept))
  });

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase3-different:" +
      String(env.isSameObject(instance, other))
  });

  kept.$dispose();
  instance.$dispose();
  other.$dispose();
});
