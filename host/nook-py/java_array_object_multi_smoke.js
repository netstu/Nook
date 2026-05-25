Java.ready(function () {
  var Arrays = Java.use("java.util.Arrays");

  var row1 = Java.array("java.lang.Object", ["a", true]);
  var row2 = Java.array("java.lang.Object", [2.5, "b"]);
  var objects2d = Java.array("java.lang.Object[]", [row1, row2]);

  send({
    type: "send",
    payload:
      "java-array-object-multi-bindings:" +
      (typeof Java.array) + ":" +
      objects2d.$className
  });

  send({
    type: "send",
    payload: "java-array-object-multi-result:" + Arrays.deepToString(objects2d)
  });
});
