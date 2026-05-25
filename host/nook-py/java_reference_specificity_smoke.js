send({
  type: "send",
  payload:
    "java-ref-specificity-top:" +
    (typeof Java) + ":" +
    (typeof Java.ready) + ":" +
    (typeof Java.enumerateClassLoaders) + ":" +
    String(Java._invokeResolverVersion)
});

Java.ready(function () {
  var chosen = null;

  send({
    type: "send",
    payload:
      "java-ref-specificity-ready:" +
      (typeof Java.enumerateClassLoaders) + ":" +
      (typeof Java.ClassFactory) + ":" +
      (typeof Java.ClassFactory.get)
  });

  if (typeof Java.enumerateClassLoaders !== "function") {
    send({
      type: "send",
      payload: "java-ref-specificity-loader-enum-missing"
    });
    return;
  }

  try {
    Java.enumerateClassLoaders({
      onMatch: function (loader) {
        if (chosen === null &&
            typeof loader.$className === "string" &&
            loader.$className.indexOf("PathClassLoader") !== -1) {
          chosen = loader;
        }
      },
      onComplete: function () {
        if (chosen === null) {
          send({
            type: "send",
            payload: "java-ref-specificity-loader:none"
          });
          return;
        }

        var cf = Java.ClassFactory.get(chosen);
        var StringBuilder = cf.use("java.lang.StringBuilder");
        var CharBuffer = cf.use("java.nio.CharBuffer");
        var probeBuilder = null;

        send({
          type: "send",
          payload:
            "java-ref-specificity-shape:" +
            loaderName(chosen) + ":" +
            (typeof Java.ClassFactory) + ":" +
            (typeof Java.ClassFactory.get) + ":" +
            (typeof cf.use) + ":" +
            (typeof cf.$new) + ":" +
            (typeof StringBuilder.append) + ":" +
            (typeof StringBuilder.append.overload)
        });

        try {
          probeBuilder = cf.$new("java.lang.StringBuilder");
        } catch (error) {
          send({
            type: "send",
            payload: "java-ref-specificity-new-error:" + String(error)
          });
          return;
        }

        var appendString;
        var appendCharSequence;
        var appendObject;
        var wrapCharSequence;
        try {
          appendString = StringBuilder.append.overload("java.lang.String");
          appendCharSequence = StringBuilder.append.overload("java.lang.CharSequence");
          appendObject = StringBuilder.append.overload("java.lang.Object");
          wrapCharSequence = CharBuffer.wrap.overload("java.lang.CharSequence");
        } catch (error) {
          if (probeBuilder !== null) {
            probeBuilder.$dispose();
          }
          send({
            type: "send",
            payload: "java-ref-specificity-unsupported:" + String(error)
          });
          return;
        }

        send({
          type: "send",
          payload:
            "java-ref-specificity-bindings:" +
            appendString.$signature + ":" +
            appendCharSequence.$signature + ":" +
            appendObject.$signature + ":" +
            wrapCharSequence.$signature
        });

        var activeCase = null;
        var activeReceiver = null;
        var hits = [];

        function loaderName(loader) {
          return typeof loader.$className === "string" ? loader.$className : "unknown";
        }

        function releaseActiveReceiver() {
          if (activeReceiver !== null) {
            activeReceiver.$dispose();
            activeReceiver = null;
          }
        }

        function armCase(name, receiver) {
          releaseActiveReceiver();
          activeCase = name;
          activeReceiver = Java.retain(receiver);
          hits = [];
        }

        function disarmCase() {
          activeCase = null;
          releaseActiveReceiver();
        }

        function shouldTrace(receiver) {
          if (activeCase === null || activeReceiver === null) {
            return false;
          }
          try {
            return receiver.equals.overload("java.lang.Object")(activeReceiver);
          } catch (error) {
            send({
              type: "send",
              payload: "java-ref-specificity-filter-error:" + String(error)
            });
            return false;
          }
        }

        function recordHit(name, value) {
          var text = value === null || typeof value === "undefined"
            ? "<null>"
            : String(value);
          hits.push(name);
          send({
            type: "send",
            payload:
              "java-ref-specificity-hit:" +
              activeCase + ":" +
              name + ":" +
              text
          });
        }

        appendString.implementation = function (value) {
          if (shouldTrace(this)) {
            recordHit("string", value);
          }
          return this.append.callOriginal(value);
        };

        appendCharSequence.implementation = function (value) {
          if (shouldTrace(this)) {
            recordHit("char-sequence", value);
          }
          return this.append.callOriginal(value);
        };

        appendObject.implementation = function (value) {
          if (shouldTrace(this)) {
            recordHit("object", value);
          }
          return this.append.callOriginal(value);
        };

        send({
          type: "send",
          payload: "java-ref-specificity-installed"
        });

        var charSequenceReceiver = cf.$new("java.lang.StringBuilder");
        armCase("char-sequence", charSequenceReceiver);
        var wrappedCharSequence = wrapCharSequence("xy");
        var charSequenceResult =
          String(charSequenceReceiver.append(wrappedCharSequence).toString());
        send({
          type: "send",
          payload:
            "java-ref-specificity-result:char-sequence:" +
            hits.join("|") + ":" +
            charSequenceResult
        });
        disarmCase();

        var nullReceiver = cf.$new("java.lang.StringBuilder");
        armCase("null", nullReceiver);
        var nullResult = String(nullReceiver.append(null).toString());
        send({
          type: "send",
          payload:
            "java-ref-specificity-result:null:" +
            hits.join("|") + ":" +
            nullResult
        });
        disarmCase();

        probeBuilder.$dispose();
        charSequenceReceiver.$dispose();
        nullReceiver.$dispose();
      }
    });
  } catch (error) {
    send({
      type: "send",
      payload: "java-ref-specificity-loader-enum-error:" + String(error)
    });
  }
});
