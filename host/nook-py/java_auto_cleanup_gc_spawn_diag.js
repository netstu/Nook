var gcEvents = [];
var retainedOnce = false;
var hitOrder = [];

function safeClassLoaderReady() {
  return typeof Java._isClassLoaderReady === "function"
    ? String(Java._isClassLoaderReady())
    : "missing";
}

function safeAppReady() {
  return typeof Java._isAppReady === "function"
    ? String(Java._isAppReady())
    : "missing";
}

function safeLifecycleReady() {
  return typeof Java._isLifecycleReady === "function"
    ? String(Java._isLifecycleReady())
    : "missing";
}

function emit(payload) {
  send({
    type: "send",
    payload: payload
  });
}

function runChoose(className, tag, onMatch) {
  var count = 0;
  Java.choose(className, {
    onMatch: function (instance) {
      count += 1;
      if (onMatch) {
        onMatch(instance, count);
      }
    },
    onComplete: function () {
      emit("java-auto-cleanup-gc-spawn-choose:" + tag + ":" + className + ":" + count);
    }
  });
  return count;
}

function retainTextFragment(instance, reason) {
  if (retainedOnce) {
    return;
  }
  retainedOnce = true;
  var kept = Java.retain(instance);
  var weakToken = Script.bindWeak(kept, function () {
    gcEvents.push("wrapper-gc");
    emit("java-auto-cleanup-gc-spawn-fired:" + gcEvents.length);
  });
  emit(
    "java-auto-cleanup-gc-spawn-retained:" +
    reason + ":" +
    String(kept.__nookJavaOwnedHandle) + ":" +
    kept.__nookJavaReceiverHandle + ":" +
    String(kept.__nookJavaWeakToken) + ":" +
    String(weakToken)
  );
}

emit(
  "java-auto-cleanup-gc-spawn-bindings:" +
  (typeof Java.retain) + ":" +
  (typeof Java.ready) + ":" +
  (typeof Java.performNow) + ":" +
  (typeof Java.choose) + ":" +
  (typeof Script.bindWeak) + ":" +
  (typeof Script._runGcForTesting)
);

emit(
  "java-auto-cleanup-gc-spawn-script-enter:" +
  safeClassLoaderReady() + ":" +
  safeAppReady() + ":" +
  safeLifecycleReady()
);

Java.performNow(function () {
  emit(
    "java-auto-cleanup-gc-spawn-perform-now:" +
    safeClassLoaderReady() + ":" +
    safeAppReady() + ":" +
    safeLifecycleReady()
  );
});

Java.ready(function () {
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
  var TextFragment = Java.use("com.demo.target.TextFragment");
  var loginInitView = LoginFragment.initView.overload("android.view.View");
  var textInitView = TextFragment.initView.overload("android.view.View");

  emit(
    "java-auto-cleanup-gc-spawn-ready:" +
    safeClassLoaderReady() + ":" +
    safeAppReady() + ":" +
    safeLifecycleReady()
  );

  loginInitView.implementation = function (view) {
    hitOrder.push("login-initView");
    emit("java-auto-cleanup-gc-spawn-hit:LoginFragment.initView:" + hitOrder.length);
    return this.initView.callOriginal(view);
  };

  textInitView.implementation = function (view) {
    hitOrder.push("text-initView");
    emit("java-auto-cleanup-gc-spawn-hit:TextFragment.initView:" + hitOrder.length);
    retainTextFragment(this, "hook");
    return this.initView.callOriginal(view);
  };

  emit(
    "java-auto-cleanup-gc-spawn-installed:" +
    loginInitView.$signature + ":" +
    textInitView.$signature
  );

  runChoose("com.demo.target.LoginFragment", "immediate", null);
  runChoose("com.demo.target.TextFragment", "immediate", function (instance, count) {
    if (count === 1) {
      emit("java-auto-cleanup-gc-spawn-match-existing:TextFragment");
      retainTextFragment(instance, "choose");
    }
  });
});

rpc.exports.getevents = function () {
  return gcEvents.slice();
};

rpc.exports.gethits = function () {
  return hitOrder.slice();
};

rpc.exports.scan = function () {
  var loginCount = 0;
  var textCount = 0;
  Java.performNow(function () {
    runChoose("com.demo.target.LoginFragment", "rpc", function () {
      loginCount += 1;
    });
    runChoose("com.demo.target.TextFragment", "rpc", function (instance) {
      textCount += 1;
      if (!retainedOnce) {
        retainTextFragment(instance, "scan");
      }
    });
  });
  return {
    login: loginCount,
    text: textCount,
    retained: retainedOnce
  };
};

rpc.exports.gc = function () {
  Script._runGcForTesting();
  return gcEvents.slice();
};
