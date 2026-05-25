Java.ready(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var app = ActivityThread.currentApplication();
  send("java-open-class-file-app:" + String(app !== null && app !== undefined));

  var apkPath = app.getPackageCodePath();
  send("java-open-class-file-path:" + apkPath);

  var dex = Java.openClassFile(apkPath);
  send("java-open-class-file-binding:" + typeof dex.load);

  var loader = dex.load();
  send("java-open-class-file-loader:" + loader.$className);

  var TextFragment = Java.use("com.demo.target.TextFragment");
  send("java-open-class-file-wrapper:" +
       TextFragment.$className + ":" +
       String(TextFragment.__nookJavaLoaderHandle === loader.__nookJavaReceiverHandle));

  var instance = TextFragment.$new("()V");
  send("java-open-class-file-result:" +
       instance.$className + ":" +
       String(instance.__nookJavaLoaderHandle === loader.__nookJavaReceiverHandle) + ":" +
       String(instance.formatBalance(10.0)));
});
