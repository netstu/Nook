var targetModule = "libnative-lib.so";
var targetSymbol = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

if (!globalThis.Thread || typeof Thread.backtrace !== "function") {
  throw new Error("Thread.backtrace is not available");
}

if (!globalThis.Backtracer ||
    Backtracer.ACCURATE === undefined ||
    Backtracer.FUZZY === undefined) {
  throw new Error("Backtracer is not available");
}

if (!globalThis.DebugSymbol || typeof DebugSymbol.fromAddress !== "function") {
  throw new Error("DebugSymbol.fromAddress is not available");
}

if (!globalThis.Interceptor || typeof Interceptor.attach !== "function") {
  throw new Error("Interceptor.attach is not available");
}

function summarizeFrames(frames, previewCount, symbolizeCount) {
  return frames.slice(0, previewCount).map(function (address, index) {
    if (index < symbolizeCount) {
      return DebugSymbol.fromAddress(address).toString();
    }
    return String(address);
  });
}

send({
  type: "send",
  payload:
    "thread-backtrace-bindings:" +
    typeof Thread +
    ":" +
    typeof Thread.backtrace +
    ":" +
    String(Backtracer.ACCURATE !== undefined) +
    ":" +
    String(Backtracer.FUZZY !== undefined),
});

var currentFrames = Thread.backtrace(Backtracer.ACCURATE);
var currentPreview = summarizeFrames(currentFrames, 4, 4);
var currentFramesNoArgs = Thread.backtrace();
var currentFuzzyFrames = Thread.backtrace(Backtracer.FUZZY);
var currentFuzzyPreview = summarizeFrames(currentFuzzyFrames, 3, 1);
send({
  type: "send",
  payload:
    "thread-backtrace-shapes:" +
    [
      String(currentFramesNoArgs.length > 0),
      String(currentFramesNoArgs[0].isNull()),
      String(currentFuzzyFrames.length > 0),
      String(currentFuzzyFrames[0].isNull()),
    ].join(":"),
});
send({
  type: "send",
  payload:
    "thread-backtrace-mode-split:" +
    currentFrames.length +
    ":" +
    currentFuzzyFrames.length,
});
send({
  type: "send",
  payload:
    "thread-backtrace-current:" +
    currentFrames.length +
    ":" +
    currentPreview.join(" | "),
});
send({
  type: "send",
  payload:
    "thread-backtrace-current-fuzzy:" +
    currentFuzzyFrames.length +
    ":" +
    currentFuzzyPreview.join(" | "),
});

var attachResult = Interceptor.attach(
  { module: targetModule, symbol: targetSymbol },
  {
    blocking: false,
    onEnter: function (args) {
      var frames = Thread.backtrace(this.context, Backtracer.ACCURATE);
      var fuzzyFrames = Thread.backtrace(this.context, Backtracer.FUZZY);
      var preview = summarizeFrames(frames, 4, 4);
      var fuzzyPreview = summarizeFrames(fuzzyFrames, 3, 1);
      send({
        type: "send",
        payload:
          "thread-backtrace-hook-mode-split:" +
          frames.length +
          ":" +
          fuzzyFrames.length,
      });
      send({
        type: "send",
        payload:
          "thread-backtrace:" +
          frames.length +
          ":" +
          preview.join(" | "),
      });
      send({
        type: "send",
        payload:
          "thread-backtrace-hook-fuzzy:" +
          fuzzyFrames.length +
          ":" +
          fuzzyPreview.join(" | "),
      });
    },
  }
);

send({
  type: "send",
  payload: "thread-backtrace-hook-ok:" + JSON.stringify({
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
      hookId: attachResult && attachResult.hookId ? attachResult.hookId : 0,
      deferred: attachResult ? attachResult.deferred : false,
    };
  },
};
