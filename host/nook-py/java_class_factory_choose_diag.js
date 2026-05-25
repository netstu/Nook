Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          typeof loader.$className === "string" &&
          loader.$className.indexOf("PathClassLoader") !== -1) {
        chosen = loader;
        send("cf-choose-diag-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("cf-choose-diag-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      send("cf-choose-diag-type:" +
           typeof cf.choose + ":" +
           typeof cf.use);
    }
  });
});
