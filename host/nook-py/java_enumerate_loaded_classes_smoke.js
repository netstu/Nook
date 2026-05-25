Java.ready(function () {
  send({
    type: "send",
    payload:
      "java-enum-classes-bindings:" +
      (typeof Java.enumerateLoadedClasses) + ":" +
      String(Java._invokeResolverVersion)
  });

  Java.enumerateLoadedClasses({
    onMatch(name) {
      if (name.indexOf("com.demo.target.") === 0) {
        send({
          type: "send",
          payload: "java-enum-classes-match:" + name
        });
      }
    },
    onComplete() {
      send({
        type: "send",
        payload: "java-enum-classes-complete"
      });
    }
  });
});
