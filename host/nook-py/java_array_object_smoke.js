Java.ready(function () {
  var Arrays = Java.use("java.util.Arrays");
  var objects = Java.array("java.lang.Object", ["a", true, 2.5]);

  send({
    type: "send",
    payload:
      "java-array-object-bindings:" +
      (typeof Java.array) + ":" +
      objects.$className
  });

  send({
    type: "send",
    payload: "java-array-object-result:" + Arrays.toString(objects)
  });
});
