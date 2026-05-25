Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("java-class-factory-factory-new-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-class-factory-factory-new-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      send("java-class-factory-factory-new-binding:" + typeof cf.$new);

      var instanceResolved = cf.$new("com.demo.target.TextFragment");
      send("java-class-factory-factory-new-result:" +
           instanceResolved.$className + ":" +
           String(instanceResolved.__nookJavaLoaderHandle === chosen.__nookJavaReceiverHandle) + ":" +
           String(instanceResolved.formatBalance(10.0)));

      var instanceExact = cf.$new("com.demo.target.TextFragment", "()V");
      send("java-class-factory-factory-new-result-exact:" +
           instanceExact.$className + ":" +
           String(instanceExact.__nookJavaLoaderHandle === chosen.__nookJavaReceiverHandle) + ":" +
           String(instanceExact.formatBalance(10.5)));
    }
  });
});
