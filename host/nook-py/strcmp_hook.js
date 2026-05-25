var targetModule = "libnative-lib.so";
var targetSymbol = "_Z22LoginCiphertextMatchesPKcS0_";
var helperOffset = 0x26518;
var mallocDemoOffset = 0x22a3c;

send({
  type: "send",
  payload:
    "native-pointer-smoke:" +
    String(ptr("0x1000")) +
    "," +
    String(ptr("0x1000").add(4)) +
    "," +
    String(ptr("0x1000").sub(4)) +
    "," +
    String(NULL.isNull()),
});

var targetAddress = Module.findExportByName(targetModule, targetSymbol);
send({
  type: "send",
  payload: "find-export:" + String(targetAddress),
});
var listener = Interceptor.attach(
  { module: targetModule, symbol: targetSymbol },
  {
  snapshot: [
    { index: 0, type: "cstringUtf8" },
    { index: 1, type: "cstringUtf8" },
  ],
  onEnter: function (args) {
    var lhs = args[0].isNull()
      ? "<null>"
      : typeof args[0].$utf8 === "string"
        ? args[0].$utf8
        : "<transient>";
    var rhs = args[1].isNull()
      ? "<null>"
      : typeof args[1].$utf8 === "string"
        ? args[1].$utf8
        : args[1].readUtf8String(256);
    send({
      type: "send",
      payload:
        "login-ciphertext-args:" +
        "lhs_ptr=" +
        args[0] +
        ",lhs=" +
        lhs +
        "," +
        "rhs=" +
        rhs,
    });
  },
  onLeave: function (retval) {
    send({
      type: "send",
      payload: "login-ciphertext-ret:" + retval + ",isNull=" + retval.isNull(),
    });
  },
});

var mallocDemoListener = null;
var mallocDemoAddress = null;
if (targetAddress !== null) {
  var moduleBase = targetAddress.sub(helperOffset);
  mallocDemoAddress = moduleBase.add(mallocDemoOffset);
  try {
    mallocDemoListener = Interceptor.attach(mallocDemoAddress, {
      snapshot: [{ index: 0, type: "cstringUtf8" }],
      onEnter: function (args) {
        var text = args[0].isNull()
          ? "<null>"
          : typeof args[0].$utf8 === "string"
            ? args[0].$utf8
            : args[0].readUtf8String(128);
        send({
          type: "send",
          payload: "login-malloc-demo-arg:" + text + ",ptr=" + args[0],
        });
      },
      onLeave: function (retval) {},
    });
    send({
      type: "send",
      payload:
        "login-malloc-demo-attach-ok:" +
        JSON.stringify({
          address: String(mallocDemoAddress),
          hookId: mallocDemoListener.hookId,
        }),
    });
  } catch (e) {
    send({
      type: "send",
      payload: "login-malloc-demo-attach-failed:" + String(e),
    });
  }
} else {
  send({
    type: "send",
    payload: "login-malloc-demo-skipped: target module not loaded yet",
  });
}

send({
  type: "send",
  payload:
    "login-ciphertext-attach-ok:" +
    JSON.stringify({
      mode: listener.deferred
        ? "interceptor-module-symbol-deferred"
        : "interceptor-module-symbol",
      ok: listener.ok,
      hookId: listener.hookId,
      deferred: listener.deferred,
    }),
});

rpc.exports = {
  ping: function () {
    return {
      module: targetModule,
      symbol: targetSymbol,
      target: targetAddress === null ? null : String(targetAddress),
      hookId: listener && listener.hookId ? listener.hookId : 0,
      mallocDemoTarget: mallocDemoAddress === null ? null : String(mallocDemoAddress),
      mallocDemoHookId:
        mallocDemoListener && mallocDemoListener.hookId
          ? mallocDemoListener.hookId
          : 0,
    };
  },
  unhook: function () {
    var ok = listener.detach();
    if (mallocDemoListener !== null) {
      ok = mallocDemoListener.detach() && ok;
    }
    return ok;
  },
};
