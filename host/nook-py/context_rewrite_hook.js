var targetModule = "libnative-lib.so";
var targetSymbol = "_Z22LoginCiphertextMatchesPKcS0_";

var listener = Interceptor.attach(
  { module: targetModule, symbol: targetSymbol },
  {
    snapshot: [
      { index: 0, type: "cstringUtf8" },
      { index: 1, type: "cstringUtf8" },
    ],
    onEnter: function (args) {
      var lhs = typeof args[0].$utf8 === "string" ? args[0].$utf8 : "<transient>";
      var rhs = typeof args[1].$utf8 === "string" ? args[1].$utf8 : "<transient>";
      this.context.x0 = args[1];
      send({
        type: "send",
        payload:
          "context-rewrite-enter:" +
          "lhs=" +
          lhs +
          ",rhs=" +
          rhs +
          ",newX0=" +
          this.context.x0,
      });
    },
    onLeave: function (retval) {
      send({
        type: "send",
        payload:
          "context-rewrite-leave:" +
          "retval=" +
          retval +
          ",x0=" +
          this.context.x0,
      });
    },
  }
);

send({
  type: "send",
  payload: "context-rewrite-hook:" + JSON.stringify(listener),
});

rpc.exports = {
  unhook: function () {
    return listener.detach();
  },
};
