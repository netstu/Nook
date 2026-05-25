Java.ready(function () {
  var Arrays = Java.use("java.util.Arrays");

  var intRow1 = Java.array("int", [1, 2]);
  var intRow2 = Java.array("int", [3, 4]);
  var ints2d = Java.array("int[]", [intRow1, intRow2]);

  var boolRow1 = Java.array("boolean", [true, false]);
  var boolRow2 = Java.array("boolean", [false, true]);
  var bools2d = Java.array("boolean[]", [boolRow1, boolRow2]);

  var byteRow1 = Java.array("byte", [1, 2]);
  var byteRow2 = Java.array("byte", [3, 4]);
  var bytes2d = Java.array("byte[]", [byteRow1, byteRow2]);

  var shortRow1 = Java.array("short", [1, 2]);
  var shortRow2 = Java.array("short", [3, 4]);
  var shorts2d = Java.array("short[]", [shortRow1, shortRow2]);

  var charRow1 = Java.array("char", ["a", "b"]);
  var charRow2 = Java.array("char", ["c"]);
  var chars2d = Java.array("char[]", [charRow1, charRow2]);

  var longRow1 = Java.array("long", [1, 2]);
  var longRow2 = Java.array("long", [3, 4]);
  var longs2d = Java.array("long[]", [longRow1, longRow2]);

  var floatRow1 = Java.array("float", [1.25, 2.5]);
  var floatRow2 = Java.array("float", [3.75]);
  var floats2d = Java.array("float[]", [floatRow1, floatRow2]);

  var doubleRow1 = Java.array("double", [1.25, 2.5]);
  var doubleRow2 = Java.array("double", [3.75]);
  var doubles2d = Java.array("double[]", [doubleRow1, doubleRow2]);

  var stringRow1 = Java.array("java.lang.String", ["a", "b"]);
  var stringRow2 = Java.array("java.lang.String", ["c"]);
  var strings2d = Java.array("java.lang.String[]", [stringRow1, stringRow2]);

  send({
    type: "send",
    payload:
      "java-array-multi-bindings:" +
      (typeof Java.array) + ":" +
      ints2d.$className + ":" +
      strings2d.$className
  });

  send({
    type: "send",
    payload: "java-array-multi-int-result:" + Arrays.deepToString(ints2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-boolean-result:" + Arrays.deepToString(bools2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-byte-result:" + Arrays.deepToString(bytes2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-short-result:" + Arrays.deepToString(shorts2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-char-result:" + Arrays.deepToString(chars2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-long-result:" + Arrays.deepToString(longs2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-float-result:" + Arrays.deepToString(floats2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-double-result:" + Arrays.deepToString(doubles2d)
  });

  send({
    type: "send",
    payload: "java-array-multi-string-result:" + Arrays.deepToString(strings2d)
  });
});
