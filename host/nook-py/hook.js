send({ type: "send", payload: "hello-from-cli" });

recv(function (message) {
  var payload =
      message && Object.prototype.hasOwnProperty.call(message, "payload")
          ? String(message.payload)
          : JSON.stringify(message);
  send({ type: "send", payload: "echo:" + payload });
});

rpc.exports = {
  ping: function (value) {
    return {
      reply: "pong",
      input: String(value),
    };
  },
};
