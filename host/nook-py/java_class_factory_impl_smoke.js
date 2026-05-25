Java.ready(function () {
  if (typeof Java.deopt === "function") {
    var deopt = Java.deopt();
    send("java-class-factory-impl-deopt:" +
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
        send("java-class-factory-impl-loader:" + loader.$className);
      }
    },
    onComplete: function () {
      if (chosen === null) {
        send("java-class-factory-impl-loader:none");
        return;
      }

      var cf = Java.ClassFactory.get(chosen);
      var LoginFragment = cf.use("com.demo.target.LoginFragment");
      var overload = LoginFragment.verifyPasswordNative.overload("java.lang.String");

      send("java-class-factory-impl-wrapper:" +
           overload.$signature + ":" +
           String(overload.$isStatic));

      overload.implementation = function (password) {
        send("java-class-factory-impl-enter:" + String(password));
        return true;
      };

      send("java-class-factory-impl-installed:" + overload.$signature);
    }
  });
});
