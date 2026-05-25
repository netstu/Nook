Java.ready(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const initView = TextFragment.initView.overload("android.view.View");
  let fired = false;

  send({
    type: "send",
    payload:
      "java-choose-bindings:" +
      (typeof Java.choose) + ":" +
      String(Java._invokeResolverVersion)
  });

  initView.implementation = function (view) {
    const result = this.initView.callOriginal(view);
    if (!fired) {
      fired = true;
      Java.choose("com.demo.target.TextFragment", {
        onMatch(instance) {
          send({
            type: "send",
            payload:
              "java-choose-match:" +
              instance.$className + ":" +
              String(instance.formatBalance(10.0))
          });
        },
        onComplete() {
          send({
            type: "send",
            payload: "java-choose-complete"
          });
        }
      });
    }
    return result;
  };

  send({
    type: "send",
    payload: "java-choose-installed"
  });
});
