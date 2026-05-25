send({
  type: "send",
  payload:
    "java-vm-getenv-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv)
});

var env = Java.vm.getEnv();

send({
  type: "send",
  payload:
    "java-vm-getenv-direct:" +
    String(env.isNull()) + ":" +
    env.toString()
});

Java.vm.perform(function () {
  var insideEnv = Java.vm.getEnv();
  send({
    type: "send",
    payload:
      "java-vm-getenv-perform:" +
      String(insideEnv.isNull()) + ":" +
      insideEnv.toString()
  });
});
