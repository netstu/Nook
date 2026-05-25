Java.ready(function () {
  var Arrays = Java.use("java.util.Arrays");

  try {
    Arrays.toString(Java.array("int", [1, "x"]));
    send({
      type: "send",
      payload: "java-array-error-int:unexpected-success"
    });
  } catch (e) {
    send({
      type: "send",
      payload: "java-array-error-int:" + e.message
    });
  }

  try {
    var badRow = Java.array("int", [1, "x"]);
    Arrays.deepToString(Java.array("int[]", [badRow]));
    send({
      type: "send",
      payload: "java-array-error-int2d:unexpected-success"
    });
  } catch (e) {
    send({
      type: "send",
      payload: "java-array-error-int2d:" + e.message
    });
  }
});
