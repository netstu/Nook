send({
    type: "send",
    payload: "begin-readUtf8String-guard-test",
  });

  try {
    var text = ptr("0x1").readUtf8String(16);
    send({
      type: "send",
      payload: "unexpected-success:" + String(text),
    });
  } catch (e) {
    send({
      type: "send",
      payload: "expected-error:" + String(e),
    });
  }

  rpc.exports = {
    ping: function () {
      return "readUtf8String-guard-ready";
    },
  };