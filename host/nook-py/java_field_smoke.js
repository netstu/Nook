Java.perform(function () {
  const state = {
    deoptSent: false,
    staticSent: false,
    installed: false,
    bootstrapInstalled: false,
    lastError: ""
  };

  function emit(payload) {
    send({
      type: "send",
      payload: payload
    });
  }

  function emitOnceDeopt() {
    if (state.deoptSent || typeof Java.deopt !== "function") {
      return;
    }

    const deoptResult = Java.deopt();
    emit(
      "java-field-deopt:" +
      String(deoptResult.ok) + ":" +
      String(deoptResult.invalidated) + ":" +
      String(deoptResult.reason) + ":" +
      String(deoptResult.runtimeOffset)
    );
    state.deoptSent = true;
  }

  function verifyStaticField() {
    if (state.staticSent) {
      return true;
    }

    const MainActivity = Java.use("com.demo.target.MainActivity");
    const staticField = MainActivity.interceptCount;
    if (typeof staticField !== "object" ||
        staticField.$signature !== "I" ||
        staticField.$isStatic !== true) {
      throw new Error(
        "static field not ready:" +
        typeof staticField + ":" +
        String(staticField.$signature) + ":" +
        String(staticField.$isStatic)
      );
    }

    const staticBefore = staticField.value;
    staticField.value = staticBefore + 10;
    const staticAfter = staticField.value;
    staticField.value = staticBefore;
    const staticRestored = staticField.value;

    emit(
      "java-field-static:" +
      (typeof staticField) + ":" +
      staticField.$signature + ":" +
      String(staticField.$isStatic) + ":" +
      String(staticBefore) + ":" +
      String(staticAfter) + ":" +
      String(staticRestored)
    );
    state.staticSent = true;
    return true;
  }

  function installInstanceHook() {
    if (state.installed) {
      return true;
    }

    const AdWallFragment = Java.use("com.demo.target.AdWallFragment");
    const initView = AdWallFragment.initView.overload("android.view.View");
    const loadAd = AdWallFragment.loadAd.overload("java.lang.String", "java.lang.String");

    initView.implementation = function (view) {
      emit("java-field-adwall-init-enter");
      const result = this.initView.callOriginal(view);
      emit("java-field-adwall-init-leave");
      return result;
    };

    loadAd.implementation = function (adType, position) {
      const field = this.adCount;
      const before = field.value;
      field.value = before + 1;
      const afterWrite = field.value;

      emit(
        "java-field-instance:" +
        field.$signature + ":" +
        String(field.$isStatic) + ":" +
        String(before) + ":" +
        String(afterWrite) + ":" +
        adType + ":" +
        position
      );

      this.loadAd.callOriginal(adType, position);

      emit(
        "java-field-instance-after-original:" +
        String(this.adCount.value)
      );
    };

    emit(
      "java-field-init-installed:" +
      initView.$signature + ":" +
      String(initView.$isStatic)
    );
    emit(
      "java-field-installed:" +
      loadAd.$signature + ":" +
      String(loadAd.$isStatic)
    );
    state.installed = true;
    return true;
  }

  function tryInstall(reason) {
    if (state.installed) {
      return true;
    }

    try {
      emitOnceDeopt();
      verifyStaticField();
      installInstanceHook();
      if (reason) {
        emit("java-field-ready:" + reason);
      }
      return true;
    } catch (error) {
      const message = String(error);
      if (message !== state.lastError) {
        state.lastError = message;
        emit("java-field-wait:" + reason + ":" + message);
      }
      return false;
    }
  }

  const Log = Java.use("android.util.Log");
  const logd = Log.d.overload("java.lang.String", "java.lang.String");
  logd.implementation = function (tag, message) {
    const result = this.d.callOriginal(tag, message);
    if (!state.installed) {
      tryInstall(String(tag) + ":" + String(message));
    }
    return result;
  };
  state.bootstrapInstalled = true;
  emit("java-field-bootstrap-installed");

  tryInstall("initial");
});
