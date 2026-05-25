var boundToken = 0;
var unboundToken = 0;

(function () {
  var target = {};
  var cancelled = {};

  boundToken = Script.bindWeak(target, function () {
    send({
      type: "send",
      payload: "script-bindweak-unload-fired:bound:" + String(boundToken)
    });
  });

  unboundToken = Script.bindWeak(cancelled, function () {
    send({
      type: "send",
      payload: "script-bindweak-unload-fired:unbound:" + String(unboundToken)
    });
  });

  var unbound = Script.unbindWeak(unboundToken);

  send({
    type: "send",
    payload:
      "script-bindweak-unload-installed:" +
      (typeof Script.bindWeak) + ":" +
      (typeof Script.unbindWeak) + ":" +
      String(boundToken > 0) + ":" +
      String(unbound)
  });

  send({
    type: "send",
    payload: "script-bindweak-unload-note:unbindWeak-now-fires-immediately"
  });
})();
