Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("java-class-factory-cast-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-class-factory-cast-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      var TextFragment = cf.use("com.demo.target.TextFragment");
      var initView = TextFragment.initView.overload("android.view.View");

      send("java-class-factory-cast-wrapper:" +
           typeof cf.cast + ":" +
           initView.$signature + ":" +
           String(initView.$isStatic));

      initView.implementation = function (view) {
        var casted = cf.cast(this, TextFragment);
        send("java-class-factory-cast-result:" +
             casted.$className + ":" +
             String(casted.__nookJavaLoaderHandle === chosen.__nookJavaReceiverHandle) + ":" +
             String(casted.formatBalance(10.0)));
        return this.initView.callOriginal(view);
      };

      send("java-class-factory-cast-installed");
    }
  });
});
