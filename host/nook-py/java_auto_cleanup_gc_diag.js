var gcEvents = [];
var retainedOnce = false;

function safeClassLoaderReady() {
  return typeof Java._isClassLoaderReady === "function"
    ? String(Java._isClassLoaderReady())
    : "missing";
}

function safeAppReady() {
  return typeof Java._isAppReady === "function"
    ? String(Java._isAppReady())
    : "missing";
}

send({
  type: "send",
  payload:
    "java-auto-cleanup-gc-bindings:" +
    (typeof Java.retain) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java.performNow) + ":" +
    (typeof Script.bindWeak) + ":" +
    (typeof Script._runGcForTesting)
});

Java.ready(function () {
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload:
      "java-auto-cleanup-gc-ready:" +
      safeClassLoaderReady() + ":" +
      safeAppReady()
  });

  initView.implementation = function (view) {
    if (!retainedOnce) {
      retainedOnce = true;
      var kept = Java.retain(this);
      var weakToken = Script.bindWeak(kept, function () {
        gcEvents.push("wrapper-gc");
        send({
          type: "send",
          payload: "java-auto-cleanup-gc-fired:" + gcEvents.length
        });
      });
      send({
        type: "send",
        payload:
          "java-auto-cleanup-gc-retained:" +
          String(kept.__nookJavaOwnedHandle) + ":" +
          kept.__nookJavaReceiverHandle + ":" +
          String(kept.__nookJavaWeakToken) + ":" +
          String(weakToken)
      });
    }

    return this.initView.callOriginal(view);
  };

  send({
    type: "send",
    payload: "java-auto-cleanup-gc-installed"
  });
});

rpc.exports.getevents = function () {
  return gcEvents.slice();
};

rpc.exports.gc = function () {
  Script._runGcForTesting();
  return gcEvents.slice();
};
