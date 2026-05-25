Java.perform(function () {
    const AdWallFragment = Java.use("com.demo.target.AdWallFragment");

    const initView = AdWallFragment.initView.overload("android.view.View");
    const loadAd = AdWallFragment.loadAd.overload("java.lang.String", "java.lang.String");

    initView.implementation = function (view) {
      console.log("[initView] " + this.$className);
      return initView.call(this, view);
    };

    loadAd.implementation = function (adType, position) {
      const before = this.adCount.value;
      console.log(
        "[loadAd enter] adType=" + adType +
        " position=" + position +
        " adCount(before)=" + before
      );

      const ret = loadAd.call(this, adType, position);

      const after = this.adCount.value;
      console.log("[loadAd leave] adCount(after)=" + after);
      return ret;
    };

    console.log("[*] hooks installed");
  });