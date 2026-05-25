send({
  type: "send",
  payload:
    "java-env-wrapper-ref-type-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv)
});

Java.ready(function () {
  var env = Java.vm.getEnv();
  var ObjectClass = Java.use("java.lang.Object");
  var obj = ObjectClass.$new();
  var kept = Java.retain(obj);

  send({
    type: "send",
    payload:
      "java-env-wrapper-ref-type-shape:" +
      (typeof env.getObjectRefType)
  });

  send({
    type: "send",
    payload:
      "java-env-wrapper-ref-type-result:" +
      String(env.getObjectRefType(obj)) + ":" +
      String(env.getObjectRefType(kept))
  });
});
