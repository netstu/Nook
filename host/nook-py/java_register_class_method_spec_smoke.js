Java.ready(function () {
  send("java-register-class-method-spec-binding:" + typeof Java.registerClass);

  var OnClickListener = Java.use("android.view.View$OnClickListener");
  var ActivityThread = Java.use("android.app.ActivityThread");
  var View = Java.use("android.view.View");

  var Listener = Java.registerClass({
    name: "nook.smoke.ClickListenerMethodSpec",
    implements: [OnClickListener],
    methods: {
      onClick: {
        returnType: "void",
        argumentTypes: ["android.view.View"],
        implementation: function (view) {
          var viewText = view === null || typeof view === "undefined"
            ? "null"
            : String(view.$className || view);
          send("java-register-class-method-spec-callback:" + viewText);
        }
      }
    }
  });

  send("java-register-class-method-spec-classlike:" + typeof Listener.$new);

  var listener = Listener.$new();
  send("java-register-class-method-spec-instance:" + String(listener.$className));

  var app = ActivityThread.currentApplication();
  send("java-register-class-method-spec-app:" + String(app !== null));

  var view = View.$new("(Landroid/content/Context;)V", app);
  send("java-register-class-method-spec-view:" + String(view.$className));

  view.setOnClickListener.overload("android.view.View$OnClickListener")(listener);
  send("java-register-class-method-spec-installed");

  var clicked = view.performClick();
  send("java-register-class-method-spec-invoke-done:" + String(clicked));
});
