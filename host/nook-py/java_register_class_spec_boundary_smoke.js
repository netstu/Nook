Java.ready(function () {
  send("java-register-class-spec-boundary-binding:" + typeof Java.registerClass);

  var Runnable = Java.use("java.lang.Runnable");
  var ObjectClass = Java.use("java.lang.Object");

  try {
    Java.registerClass({
      name: "nook.smoke.UnsupportedFields",
      implements: [Runnable],
      fields: {
        counter: "int"
      },
      methods: {
        run: function () {}
      }
    });
    send("java-register-class-spec-boundary-fields:accepted");
  } catch (e) {
    send("java-register-class-spec-boundary-fields:" + String(e));
  }

  try {
    Java.registerClass({
      name: "nook.smoke.UnsupportedSuperClass",
      superClass: ObjectClass,
      implements: [Runnable],
      methods: {
        run: function () {}
      }
    });
    send("java-register-class-spec-boundary-super:accepted");
  } catch (e) {
    send("java-register-class-spec-boundary-super:" + String(e));
  }
});
