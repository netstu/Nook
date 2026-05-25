Java.ready(function () {
  var TextUtils = Java.use("android.text.TextUtils");
  var parts = Java.array("java.lang.String", ["a", "b"]);

  send({
    type: "send",
    payload:
      "java-array-ref-cov-bindings:" +
      (typeof Java.array) + ":" +
      TextUtils.concat.overload("java.lang.CharSequence[]").$signature
  });

  var result = TextUtils.concat(parts);
  send({
    type: "send",
    payload:
      "java-array-ref-cov-result:" +
      result.toString() + ":" +
      parts.$className
  });
});
