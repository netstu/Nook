Java.ready(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload:
      "java-retain-bindings:" +
      (typeof Java.retain) + ":" +
      String(Java._invokeResolverVersion)
  });

  initView.implementation = function (view) {
    const kept = Java.retain(this);
    const casted = Java.cast(kept, TextFragment);
    send({
      type: "send",
      payload:
        "java-retain-result:" +
        kept.$className + ":" +
        String(kept !== this) + ":" +
        String(casted.formatBalance(10.0))
    });
    return this.initView.callOriginal(view);
  };

  send({
    type: "send",
    payload: "java-retain-installed"
  });
});
