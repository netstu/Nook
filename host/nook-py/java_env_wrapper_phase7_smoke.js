send({
  type: "send",
  payload:
    "java-env-wrapper-phase7-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.vm) + ":" +
    (typeof Java.vm.getEnv) + ":" +
    (typeof Java.choose)
});

Java.ready(function () {
  var env = Java.vm.getEnv();
  var fired = false;
  var classNames = [
    "com.demo.target.TextFragment",
    "com.demo.target.LoginFragment",
    "com.demo.target.AdWallFragment"
  ];

  send({
    type: "send",
    payload:
      "java-env-wrapper-phase7-direct:" +
      (typeof env) + ":" +
      (typeof env.newGlobalRef) + ":" +
      (typeof env.deleteGlobalRef)
  });

  function run(instance, source) {
    if (fired) {
      return;
    }
    fired = true;

    var globalRef = env.newGlobalRef(instance);
    var globalDeleted = env.deleteGlobalRef(globalRef);

    send({
      type: "send",
      payload:
        "java-env-wrapper-phase7-global:" +
        source + ":" +
        globalRef.toString() + ":" +
        String(globalRef.isNull()) + ":" +
        String(globalDeleted)
    });

    send({
      type: "send",
      payload:
        "java-env-wrapper-phase7-exception:" +
        String(env.exceptionCheck())
    });
  }

  function chooseNext(index) {
    if (fired) {
      return;
    }
    if (index >= classNames.length) {
      send({
        type: "send",
        payload: "java-env-wrapper-phase7-no-match"
      });
      return;
    }

    var className = classNames[index];
    Java.choose(className, {
      onMatch: function (instance) {
        run(instance, "choose:" + className);
      },
      onComplete: function () {
        if (!fired) {
          chooseNext(index + 1);
        }
      }
    });
  }

  chooseNext(0);
});
