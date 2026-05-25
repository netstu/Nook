var targetModule = "libnative-lib.so";
var targetSymbol = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

if (!globalThis.Module || typeof Module.attachExport !== "function") {
  throw new Error("Module.attachExport is not available");
}

var attachResult = Module.attachExport(targetModule, targetSymbol, {
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
        ",threadId=" +
        this.threadId +
        ",returnAddress=" +
        this.returnAddress +
        ",pc=" +
        this.context.pc +
        ",lr=" +
        this.context.lr +
        ",sp=" +
        this.context.sp,
    });
  },
  onLeave: function (retval) {
    send({
      type: "send",
      payload:
        "leave:" +
        retval +
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
});

send({
  type: "send",
  payload: "native-attach-ok:" + JSON.stringify(attachResult),
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
