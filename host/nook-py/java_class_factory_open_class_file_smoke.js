Java.ready(function () {
  var chosen = null;

  Java.enumerateClassLoaders({
    onMatch: function (loader) {
      if (chosen === null &&
          String(loader.$className).indexOf("PathClassLoader") !== -1) {
        chosen = loader;
      }
    },
    onComplete: function () {}
  });

  send({
    type: "send",
    payload:
      "java-cf-open-class-file-loader:" +
      (chosen !== null) + ":" +
      (chosen !== null ? chosen.$className : "null")
  });

  var factory = Java.ClassFactory.get(chosen);

  send({
    type: "send",
    payload:
      "java-cf-open-class-file-binding:" +
      (typeof factory.openClassFile)
  });

  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();
  var apkPath = app.getPackageCodePath();

  send({
    type: "send",
    payload:
      "java-cf-open-class-file-path:" +
      apkPath
  });

  var dex = factory.openClassFile(apkPath);
  var dexLoader = dex.load();
  var scopedFactory = Java.ClassFactory.get(dexLoader);
  var TextFragment = scopedFactory.use("com.demo.target.TextFragment");
  var instance = TextFragment.$new("()V");

  send({
    type: "send",
    payload:
      "java-cf-open-class-file-result:" +
      dexLoader.$className + ":" +
      TextFragment.$className + ":" +
      String(instance.formatBalance(10.0))
  });
});
