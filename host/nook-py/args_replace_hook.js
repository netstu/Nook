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
      send({
        type: "send",
        payload:
          "args-replace-before:" +
          "lhs=" +
          lhs +
          ",rhs=" +
          rhs +
          ",arg0=" +
          args[0],
      });
      args[0].replace(args[1]);
      send({
        type: "send",
        payload: "args-replace-after:arg0=" + args[0] + ",rhs=" + args[1],
      });
    },
    onLeave: function (retval) {
      send({
        type: "send",
        payload: "args-replace-leave:" + retval + ",u32=" + retval.toUInt32(),
      });
    },
  }
);

send({
  type: "send",
  payload: "args-replace-hook:" + JSON.stringify(listener),
});

rpc.exports = {
  unhook: function () {
    return listener.detach();
  },
};
