var pinCount = 0;

send({
  type: "send",
  payload:
    "script-pin-bindings:" +
    (typeof Script.pin) + ":" +
    (typeof Script.unpin)
});

rpc.exports.pin = function () {
  Script.pin();
  pinCount += 1;
  send({
    type: "send",
    payload: "script-pin-count:" + String(pinCount)
  });
  return pinCount;
};

rpc.exports.unpin = function () {
  Script.unpin();
  pinCount -= 1;
  send({
    type: "send",
    payload: "script-pin-count:" + String(pinCount)
  });
  return pinCount;
};

rpc.exports.getcount = function () {
  return pinCount;
};
