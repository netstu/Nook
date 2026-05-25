Java.ready(function () {
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload:
      "java-dispose-bindings:" +
      (typeof Java.retain) + ":" +
      String(Java._invokeResolverVersion)
  });

  initView.implementation = function (view) {
    var kept = Java.retain(this);

    send({
      type: "send",
      payload:
        "java-dispose-before:" +
        (typeof kept.$dispose) + ":" +
        String(kept.__nookJavaOwnedHandle) + ":" +
        kept.__nookJavaReceiverHandle
    });

    kept.$dispose();
    kept.$dispose();

    send({
      type: "send",
      payload:
        "java-dispose-after:" +
        String(kept.__nookJavaOwnedHandle) + ":" +
        kept.__nookJavaReceiverHandle + ":" +
        kept.__jptr
    });

    return this.initView.callOriginal(view);
  };

  send({
    type: "send",
    payload: "java-dispose-installed"
  });
});
