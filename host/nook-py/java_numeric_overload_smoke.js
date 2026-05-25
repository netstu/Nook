Java.ready(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const initView = TextFragment.initView.overload("android.view.View");

  send({
    type: "send",
    payload:
      "java-numeric-overload-wrapper:" +
      (typeof initView) + ":" +
      initView.$signature + ":" +
      String(initView.$isStatic) + ":" +
      String(Java._invokeResolverVersion)
  });

  initView.implementation = function (view) {
    send({
      type: "send",
      payload: "java-numeric-overload-enter"
    });

    let viaIntegralNumber;
    let viaFractionalNumber;
    try {
      viaIntegralNumber = this.formatBalance(10.0);
      viaFractionalNumber = this.formatBalance(10.5);
    } catch (error) {
      send({
        type: "send",
        payload: "java-numeric-overload-error:" + String(error)
      });
      return this.initView.callOriginal(view);
    }

    send({
      type: "send",
      payload:
        "java-numeric-overload-result:" +
        String(viaIntegralNumber) + ":" +
        String(viaFractionalNumber)
    });

    return this.initView.callOriginal(view);
  };

  send({
    type: "send",
    payload: "java-numeric-overload-installed"
  });
});
