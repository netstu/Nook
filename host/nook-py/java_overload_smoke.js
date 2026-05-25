Java.perform(function () {
  const LoginFragment = Java.use("com.demo.target.LoginFragment");
  const overload = LoginFragment.verifyPasswordNative.overload("java.lang.String");

  send({
    type: "send",
    payload:
      "java-overload-wrapper:" +
      (typeof overload) + ":" +
      overload.$signature + ":" +
      (typeof overload.callOriginal)
  });

  overload.implementation = function (password) {
    send({
      type: "send",
      payload: "java-overload-enter:" + password
    });

    const original = this.verifyPasswordNative.callOriginal(password);

    send({
      type: "send",
      payload: "java-overload-leave-original:" + original
    });

    return true;
  };

  send({
    type: "send",
    payload: "java-overload-installed:" + overload.$signature
  });
});
