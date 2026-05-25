Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("java-class-factory-retain-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-class-factory-retain-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      var TextFragment = cf.use("com.demo.target.TextFragment");
      var initView = TextFragment.initView.overload("android.view.View");

      send("java-class-factory-retain-wrapper:" +
           typeof cf.retain + ":" +
           initView.$signature + ":" +
           String(initView.$isStatic));

      initView.implementation = function (view) {
        var kept = cf.retain(this);
        send("java-class-factory-retain-result:" +
             kept.$className + ":" +
             String(kept !== this) + ":" +
             String(kept.__nookJavaLoaderHandle === chosen.__nookJavaReceiverHandle) + ":" +
             String(kept.formatBalance(10.0)));
        return this.initView.callOriginal(view);
      };

      send("java-class-factory-retain-installed");
    }
  });
});
