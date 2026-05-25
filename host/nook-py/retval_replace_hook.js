var targetModule = "libnative-lib.so";
var targetSymbol = "_Z22LoginCiphertextMatchesPKcS0_";

var listener = Interceptor.attach(
  { module: targetModule, symbol: targetSymbol },
  {
    onLeave: function (retval) {
      send({
        type: "send",
        payload:
          "retval-replace-before:" +
          retval +
          ",x0=" +
          this.context.x0 +
          ",u32=" +
          retval.toUInt32(),
      });
      retval.replace(1);
      send({
        type: "send",
        payload:
          "retval-replace-after:" +
          retval +
          ",x0=" +
          this.context.x0 +
          ",i32=" +
          retval.toInt32(),
      });
    },
  }
);

send({
  type: "send",
  payload: "retval-replace-hook:" + JSON.stringify(listener),
});

rpc.exports = {
  unhook: function () {
    return listener.detach();
  },
};
