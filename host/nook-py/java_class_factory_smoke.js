Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("java-class-factory-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-class-factory-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      var TextFragment = cf.use("com.demo.target.TextFragment");
      var overload = TextFragment.formatBalance.overload("double");
      send("java-class-factory-use:" +
           TextFragment.$className + ":" +
           overload.$signature + ":" +
           String(overload.$isStatic));
    }
  });
});
