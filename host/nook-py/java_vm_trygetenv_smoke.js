send({
  type: "send",
  payload:
    "java-vm-trygetenv-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.tryGetEnv)
});

var directEnv = Java.vm.tryGetEnv();

send({
  type: "send",
  payload:
    "java-vm-trygetenv-direct:" +
    String(directEnv === null) + ":" +
    (directEnv === null ? "null" : directEnv.toString())
});

Java.vm.perform(function () {
  var insideEnv = Java.vm.tryGetEnv();
  send({
    type: "send",
    payload:
      "java-vm-trygetenv-perform:" +
      String(insideEnv === null) + ":" +
      (insideEnv === null ? "null" : insideEnv.toString())
  });
});
