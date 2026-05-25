Java.ready(function () {
  var Arrays = Java.use("java.util.Arrays");

  send({
    type: "send",
    payload: "java-array-mutation-bindings:" + (typeof Java.array)
  });

  var ints = Java.array("int", [1, 2, 3]);
  ints[1] = 9;
  send({
    type: "send",
    payload:
      "java-array-mutation-int:" +
      Arrays.toString(ints)
  });

  var objects = Java.array("java.lang.Object", ["a", true, 2.5]);
  objects[1] = "b";
  send({
    type: "send",
    payload:
      "java-array-mutation-object:" +
      Arrays.toString(objects)
  });

  var row1 = Java.array("int", [1, 2]);
  var row2 = Java.array("int", [3, 4]);
  var rows = Java.array("int[]", [row1, row2]);
  row1[0] = 7;
  send({
    type: "send",
    payload:
      "java-array-mutation-2d:" +
      Arrays.deepToString(rows)
  });
});
