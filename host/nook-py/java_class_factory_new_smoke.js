Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("java-class-factory-new-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-class-factory-new-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      var TextFragment = cf.use("com.demo.target.TextFragment");
      send("java-class-factory-new-wrapper:" +
           typeof TextFragment.$new + ":" +
           TextFragment.$className);

      var instanceResolved = TextFragment.$new();
      send("java-class-factory-new-result:" +
           instanceResolved.$className + ":" +
           String(instanceResolved.formatBalance(10.0)));

      var instanceExact = TextFragment.$new("()V");
      send("java-class-factory-new-result-exact:" +
           instanceExact.$className + ":" +
           String(instanceExact.formatBalance(10.5)));
    }
  });
});
