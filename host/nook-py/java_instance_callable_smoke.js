Java.ready(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload:
      "java-instance-callable-wrapper:" +
      (typeof initView) + ":" +
      initView.$signature + ":" +
      String(initView.$isStatic) + ":" +
      (typeof initView.callOriginal)
  });

  initView.implementation = function (view) {
    const viaDouble = this.formatBalance(10.5);
    const viaString = this.formatBalance("10.00");

    send({
      type: "send",
      payload:
        "java-instance-callable-result:" +
        String(viaDouble) + ":" +
        String(viaString)
    });

    return this.initView.callOriginal(view);
  };

  send({
    type: "send",
    payload: "java-instance-callable-installed"
  });
});
