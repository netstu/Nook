Java.ready(function () {
  send("java-register-class-binding:" + typeof Java.registerClass);

  var OnClickListener = Java.use("android.view.View$OnClickListener");
  var ActivityThread = Java.use("android.app.ActivityThread");
  var View = Java.use("android.view.View");
  send("java-register-class-interface:" + OnClickListener.$className);

  var Listener = Java.registerClass({
    name: "nook.smoke.ClickListener",
    implements: [OnClickListener],
    methods: {
      onClick: function (view) {
        var viewText = view === null || typeof view === "undefined"
          ? "null"
          : String(view.$className || view);
        send("java-register-class-callback:" + viewText);
      }
    }
  });

  send("java-register-class-classlike:" + typeof Listener.$new);

  var listener = Listener.$new();
  send("java-register-class-instance:" + String(listener.$className));

  var app = ActivityThread.currentApplication();
  send("java-register-class-app:" + String(app !== null));

  var view = View.$new("(Landroid/content/Context;)V", app);
  send("java-register-class-view:" + String(view.$className));

  view.setOnClickListener.overload("android.view.View$OnClickListener")(listener);
  send("java-register-class-installed");

  var clicked = view.performClick();
  send("java-register-class-invoke-done:" + String(clicked));
});
