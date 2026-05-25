send({
  type: "send",
  payload:
    "java-env-wrapper-phase8-bindings:" +
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
      "java-env-wrapper-phase8-direct:" +
      (typeof env) + ":" +
      (typeof env.newWeakGlobalRef) + ":" +
      (typeof env.deleteWeakGlobalRef)
  });

  function run(instance, source) {
    if (fired) {
      return;
    }
    fired = true;

    var weakGlobalRef = env.newWeakGlobalRef(instance);
    var weakGlobalDeleted = env.deleteWeakGlobalRef(weakGlobalRef);

    send({
      type: "send",
      payload:
        "java-env-wrapper-phase8-weak-global:" +
        source + ":" +
        weakGlobalRef.toString() + ":" +
        String(weakGlobalRef.isNull()) + ":" +
        String(weakGlobalDeleted)
    });

    send({
      type: "send",
      payload:
        "java-env-wrapper-phase8-exception:" +
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
        payload: "java-env-wrapper-phase8-no-match"
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
