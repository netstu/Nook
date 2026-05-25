Java.perform(function () {
  send({
    type: "send",
    payload:
      "java-debug-bindings:" +
      (typeof Java.deopt) + ":" +
      (typeof Java._setForcedInterpretOnly) + ":" +
      (typeof Java._artRouterDebug)
  });

  const deoptResult = Java.deopt();
  send({
    type: "send",
    payload:
      "java-deopt:" +
      String(deoptResult.ok) + ":" +
      String(deoptResult.invalidated) + ":" +
      String(deoptResult.reason) + ":" +
      String(deoptResult.symbolsAvailable) + ":" +
      String(deoptResult.runtimeAvailable) + ":" +
      String(deoptResult.scanStart) + "-" + String(deoptResult.scanEnd) + ":" +
      String(deoptResult.candidatesSeen) + ":" +
      String(deoptResult.readableCandidates) + ":" +
      String(deoptResult.runtimeOffset)
  });

  const forceOn = Java._setForcedInterpretOnly(true);
  send({
    type: "send",
    payload:
      "java-force-on:" +
      String(forceOn.ok) + ":" +
      String(forceOn.enabled) + ":" +
      String(forceOn.changed)
  });

  const routerDebug = Java._artRouterDebug();
  send({
    type: "send",
    payload:
      "java-art-router:" +
      routerDebug.lastX0 + ":" +
      String(routerDebug.missCount)
  });

  const forceOff = Java._setForcedInterpretOnly(false);
  send({
    type: "send",
    payload:
      "java-force-off:" +
      String(forceOff.ok) + ":" +
      String(forceOff.enabled) + ":" +
      String(forceOff.changed)
  });
});
