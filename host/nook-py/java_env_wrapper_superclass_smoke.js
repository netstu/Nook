send({
  type: "send",
  payload:
    "java-env-wrapper-superclass-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv)
});

Java.ready(function () {
  var env = Java.vm.getEnv();
  var StringClass = Java.use("java.lang.String");
  var ObjectClass = Java.use("java.lang.Object");
  var superClass = env.getSuperclass(StringClass);
  var assignable = env.isAssignableFrom(ObjectClass, StringClass);

  send({
    type: "send",
    payload:
      "java-env-wrapper-superclass-shape:" +
      (typeof env.getSuperclass) + ":" +
      (typeof env.isAssignableFrom)
  });

  send({
    type: "send",
    payload:
      "java-env-wrapper-superclass-result:" +
      superClass.$className + ":" +
      String(superClass.__nookJavaReceiverHandle) + ":" +
      String(superClass.__nookJavaLoaderHandle)
  });

  send({
    type: "send",
    payload:
      "java-env-wrapper-superclass-assignable:" +
      String(assignable)
  });
});
