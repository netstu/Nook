Java.perform(function () {
  const LoginFragment = Java.use("com.demo.target.LoginFragment");

  send({
    type: "send",
    payload:
      "java-bindings:" +
      (typeof Java) + ":" +
      (typeof Java.perform) + ":" +
      (typeof Java.use)
  });

  send({
    type: "send",
    payload:
      "java-wrapper:" +
      (typeof LoginFragment) + ":" +
      (typeof LoginFragment.verifyPasswordNative) + ":" +
      (typeof LoginFragment.verifyPasswordNative.callOriginal)
  });

  LoginFragment.verifyPasswordNative.implementation = function (password) {
    send({
      type: "send",
      payload: "java-hook-enter:" + password
    });

    const original = this.verifyPasswordNative.callOriginal(password);

    send({
      type: "send",
      payload: "java-hook-leave-original:" + original
    });

    return true;
  };

  send({
    type: "send",
    payload: "java-implementation-installed"
  });
});
