var targetModule = "libnative-lib.so";
var targetSymbol = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

if (!globalThis.Module || typeof Module.attachExport !== "function") {
  throw new Error("Module.attachExport is not available");
}

var attachResult = Module.attachExport(targetModule, targetSymbol, {
  snapshot: [{ index: 2, type: "jstringUtf8" }],
  onEnter: function (args) {
    var decoded = "<missing-jni-snapshot>";
    if (args[2] && typeof args[2].$jniUtf8 === "string") {
      decoded = args[2].$jniUtf8;
    }

    send({
      type: "send",
      payload:
        "jni-enter:env=" +
        args[0] +
        ",thiz=" +
        args[1] +
        ",password=" +
        args[2] +
        ",decoded=" +
        decoded,
    });
  },
  onLeave: function (retval) {
    send({ type: "send", payload: "jni-leave:" + retval });
  },
});

send({
  type: "send",
  payload: "jni-hook-ok:" + JSON.stringify(attachResult),
});

send({
  type: "send",
  payload:
    "jni-hook-note: decoded text comes from hook-thread JNI snapshot metadata args[2].$jniUtf8",
});

rpc.exports = {
  ping: function () {
    return {
      module: targetModule,
      symbol: targetSymbol,
      hookId: attachResult && attachResult.hookId ? attachResult.hookId : 0,
    };
  },
};
