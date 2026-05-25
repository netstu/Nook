Java.ready(function () {
  if (typeof Java.deopt === "function") {
    var deopt = Java.deopt();
    send("java-class-factory-choose-deopt:" +
         String(deopt.ok) + ":" +
         String(deopt.invalidated) + ":" +
         String(deopt.reason));
  }

  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("java-class-factory-choose-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-class-factory-choose-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      var TextFragment = cf.use("com.demo.target.TextFragment");
      var initView = TextFragment.initView.overload("android.view.View");
      var matched = false;
      var running = false;

      send("java-class-factory-choose-wrapper:" +
           initView.$signature + ":" +
           String(initView.$isStatic));

      function runChoose(tag) {
        if (matched || running) {
          return;
        }
        running = true;

        var localMatches = 0;
        cf.choose("com.demo.target.TextFragment", {
          onMatch: function (instance) {
            localMatches += 1;
            matched = true;
            send("java-class-factory-choose-match:" +
                 tag + ":" +
                 instance.$className + ":" +
                 String(instance.formatBalance(10.0)));
          },
          onComplete: function () {
            if (localMatches > 0) {
              send("java-class-factory-choose-complete");
            } else {
              send("java-class-factory-choose-no-match:" + tag);
            }
            running = false;
          }
        });
      }

      initView.implementation = function (view) {
        var result = this.initView.callOriginal(view);
        runChoose("hook");
        return result;
      };

      send("java-class-factory-choose-installed");
      runChoose("immediate");
    }
  });
});
