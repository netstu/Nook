Java.ready(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload:
      "java-cast-bindings:" +
      (typeof Java.cast) + ":" +
      String(Java._invokeResolverVersion)
  });

  initView.implementation = function (view) {
    const casted = Java.cast(this, TextFragment);
    send({
      type: "send",
      payload:
        "java-cast-result:" +
        casted.$className + ":" +
        String(casted !== this) + ":" +
        String(casted.formatBalance(10.0))
    });
    return this.initView.callOriginal(view);
  };

  send({
    type: "send",
    payload: "java-cast-installed"
  });
});
