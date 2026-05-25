send({
  type: "send",
  payload:
    "java-ready-bindings:" +
    (typeof Java) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java._updateClassLoader) + ":" +
    (typeof Java._isClassLoaderReady)
});

Java.ready(function () {
  const LoginFragment = Java.use("com.demo.target.LoginFragment");

  send({
    type: "send",
    payload:
      "java-ready-fired:" +
      Java._isClassLoaderReady() + ":" +
      LoginFragment.$className + ":" +
      (typeof LoginFragment.verifyPasswordNative)
  });
});
