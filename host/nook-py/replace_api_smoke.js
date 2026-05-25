(function () {
  const moduleName = "libnative-lib.so";
  const symbolName =
    "Java_com_demo_target_LoginFragment_verifyPasswordNative";

  const target = Module.getExportByName(moduleName, symbolName);
  let hitCount = 0;
  let reverted = false;

  const replacement = new NativeCallback(function (env, thiz, password) {
    hitCount += 1;
    send({
      type: "send",
      payload:
        "replace-hit:" +
        String(hitCount) +
        ":env=" +
        String(env) +
        ":thiz=" +
        String(thiz) +
        ":password=" +
        String(password),
    });
    return 1;
  }, "uint32", ["pointer", "pointer", "pointer"]);

  Interceptor.replace(target, replacement);
  send({
    type: "send",
    payload: "replace-installed:" + String(target),
  });

  rpc.exports.status = function () {
    return {
      target: String(target),
      hitCount: hitCount,
      reverted: reverted,
    };
  };

  rpc.exports.revert = function () {
    if (reverted) {
      return false;
    }
    Interceptor.revert(target);
    reverted = true;
    return true;
  };
})();
