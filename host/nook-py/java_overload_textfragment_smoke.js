Java.perform(function () {
  const TextFragment = Java.use("com.demo.target.TextFragment");
  const byDouble = TextFragment.formatBalance.overload("double");
  const byString = TextFragment.formatBalance.overload("java.lang.String");

  send({
    type: "send",
    payload: "text-overload-wrapper-double:" + byDouble.$signature
  });
  send({
    type: "send",
    payload: "text-overload-wrapper-string:" + byString.$signature
  });

  byDouble.implementation = function (amount) {
    send({
      type: "send",
      payload: "text-overload-double-enter:" + amount
    });

    const original = this.formatBalance.callOriginal(amount);

    send({
      type: "send",
      payload: "text-overload-double-leave:" + original
    });

    return original;
  };

  byString.implementation = function (amountText) {
    send({
      type: "send",
      payload: "text-overload-string-enter:" + amountText
    });

    const original = this.formatBalance.callOriginal(amountText);

    send({
      type: "send",
      payload: "text-overload-string-leave:" + original
    });

    return original;
  };

  send({
    type: "send",
    payload: "text-overload-installed"
  });
});
