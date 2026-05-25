var targetModule = "libnative-lib.so";
var targetSymbol = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

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

if (!globalThis.Module || typeof Module.findExportByName !== "function") {
  throw new Error("Module.findExportByName is not available");
}

if (!globalThis.Interceptor || typeof Interceptor.attach !== "function") {
  throw new Error("Interceptor.attach is not available");
}

var targetAddress = Module.findExportByName(targetModule, targetSymbol);
send({
  type: "send",
  payload: "find-export:" + String(targetAddress),
});

var callbacks = {
  onEnter: function (args) {
    this.enterSeen = true;
    send({
      type: "send",
      payload:
        "enter:env=" +
        args[0] +
        ",thiz=" +
        args[1] +
        ",password=" +
        args[2] +
        ",password+4=" +
        args[2].add(4) +
        ",threadId=" +
        this.threadId +
        ",returnAddress=" +
        this.returnAddress +
        ",pc=" +
        this.context.pc +
        ",lr=" +
        this.context.lr +
        ",sp=" +
        this.context.sp +
        ",x0=" +
        this.context.x0,
    });
  },
  onLeave: function (retval) {
    send({
      type: "send",
      payload:
        "leave:" +
        retval +
        ",isNull=" +
        retval.isNull() +
        ",enterSeen=" +
        this.enterSeen +
        ",threadId=" +
        this.threadId +
        ",pc=" +
        this.context.pc +
        ",lr=" +
        this.context.lr,
    });
  },
};

var attachResult = Interceptor.attach(
  { module: targetModule, symbol: targetSymbol },
  callbacks
);
var attachMode = attachResult.deferred
  ? "interceptor-module-symbol-deferred"
  : "interceptor-module-symbol";

send({
  type: "send",
  payload: "interceptor-attach-ok:" + JSON.stringify({
    mode: attachMode,
    ok: attachResult.ok,
    hookId: attachResult.hookId,
    deferred: attachResult.deferred,
  }),
});

rpc.exports = {
  ping: function () {
    return {
      module: targetModule,
      symbol: targetSymbol,
      target: targetAddress === null ? null : String(targetAddress),
      mode: attachMode,
      hookId: attachResult && attachResult.hookId ? attachResult.hookId : 0,
      deferred: attachResult ? attachResult.deferred : false,
    };
  },
  unhook: function () {
    if (!attachResult || typeof attachResult.detach !== "function") {
      return false;
    }
    return attachResult.detach();
  },
};
