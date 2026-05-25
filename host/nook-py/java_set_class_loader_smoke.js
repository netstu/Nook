Java.ready(function () {
  var chosen = null;
  var chooseDone = false;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("java-set-class-loader-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-set-class-loader-loader:none");
        return;
      }

      Java.setClassLoader(chosen);
      send("java-set-class-loader-binding:" + typeof Java.setClassLoader);

      var MainActivity = Java.use("com.demo.target.MainActivity");
      var incrementIntercept = MainActivity.incrementIntercept.overload();
      send("java-set-class-loader-use:" +
           incrementIntercept.$signature + ":" +
           String(incrementIntercept.$isStatic));

      var TextFragment = Java.use("com.demo.target.TextFragment");
      var initView = TextFragment.initView.overload("android.view.View");
      send("java-set-class-loader-wrapper:" +
           initView.$signature + ":" +
           String(initView.$isStatic) + ":" +
           String(initView.__nookJavaLoaderHandle === chosen.__nookJavaReceiverHandle));

      function runChoose(tag) {
        if (chooseDone) {
          return;
        }
        Java.choose("com.demo.target.TextFragment", {
          onMatch: function (instance) {
            chooseDone = true;
            send("java-set-class-loader-choose:" +
                 tag + ":" +
                 instance.$className + ":" +
                 String(instance.__nookJavaLoaderHandle === chosen.__nookJavaReceiverHandle));
          },
          onComplete: function () {}
        });
      }

      initView.implementation = function (view) {
        var kept = Java.retain(this);
        send("java-set-class-loader-retain:" +
             kept.$className + ":" +
             String(kept.__nookJavaLoaderHandle === chosen.__nookJavaReceiverHandle) + ":" +
             String(kept.formatBalance(10.0)));
        runChoose("hook");
        return this.initView.callOriginal(view);
      };

      send("java-set-class-loader-installed");
      runChoose("immediate");
    }
  });
});
