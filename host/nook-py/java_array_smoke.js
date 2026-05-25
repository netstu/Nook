Java.ready(function () {
  var Arrays = Java.use("java.util.Arrays");
  var ints = Java.array("int", [1, 2, 3]);
  var strings = Java.array("java.lang.String", ["a", "b"]);
  var bools = Java.array("boolean", [true, false, true]);
  var bytes = Java.array("byte", [1, 2, 3]);
  var shorts = Java.array("short", [1, 2, 3]);
  var chars = Java.array("char", ["a", "b"]);
  var longs = Java.array("long", [1, 2, 3]);
  var floats = Java.array("float", [1.25, 2.5]);
  var doubles = Java.array("double", [1.25, 2.5]);
  var toStringInts = Arrays.toString.overload("int[]");

  send({
    type: "send",
    payload:
      "java-array-bindings:" +
      (typeof Java.array) + ":" +
      ints.$className + ":" +
      toStringInts.$signature
  });

  send({
    type: "send",
    payload: "java-array-int-result:" + toStringInts(ints)
  });

  send({
    type: "send",
    payload: "java-array-string-result:" + Arrays.toString(strings)
  });

  send({
    type: "send",
    payload: "java-array-boolean-result:" + Arrays.toString(bools)
  });

  send({
    type: "send",
    payload: "java-array-byte-result:" + Arrays.toString(bytes)
  });

  send({
    type: "send",
    payload: "java-array-short-result:" + Arrays.toString(shorts)
  });

  send({
    type: "send",
    payload: "java-array-char-result:" + Arrays.toString(chars)
  });

  send({
    type: "send",
    payload: "java-array-long-result:" + Arrays.toString(longs)
  });

  send({
    type: "send",
    payload: "java-array-float-result:" + Arrays.toString(floats)
  });

  send({
    type: "send",
    payload: "java-array-double-result:" + Arrays.toString(doubles)
  });
});
