send({
  type: "send",
  payload:
    "java-enum-loaders-top:" +
    (typeof Java) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java.enumerateClassLoaders) + ":" +
    String(Java._invokeResolverVersion)
});

Java.ready(function () {
  send({
    type: "send",
    payload:
      "java-enum-loaders-bindings:" +
      (typeof Java.enumerateClassLoaders) + ":" +
      (typeof Java.vm) + ":" +
      (typeof Java.ClassFactory) + ":" +
      String(Java._invokeResolverVersion)
  });

  if (typeof Java.enumerateClassLoaders !== "function") {
    send({
      type: "send",
      payload: "java-enum-loaders-missing"
    });
    return;
  }

  var seen = [];
  try {
    Java.enumerateClassLoaders({
      onMatch: function (loader) {
        var tag = loader.$className + ":" + String(loader.__jptr);
        if (seen.indexOf(tag) === -1) {
          seen.push(tag);
          send({
            type: "send",
            payload: "java-enum-loaders-match:" + tag
          });
        }
      },
      onComplete: function () {
        send({
          type: "send",
          payload: "java-enum-loaders-complete:" + String(seen.length)
        });
      }
    });
  } catch (error) {
    send({
      type: "send",
      payload: "java-enum-loaders-error:" + String(error)
    });
  }
});
