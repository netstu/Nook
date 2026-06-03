Java.perform(function () {
  const TAG = "NookGadgetStartup";
  const Log = Java.use("android.util.Log");
  const LoginFragment = Java.use("com.demo.target.LoginFragment");

  function log(message) {
    Log.i(TAG, String(message));
  }

  try {
    LoginFragment.verifyPasswordNative.implementation = function (password) {
      log("startup-login-hook-enter password=" + password);

      const original = this.verifyPasswordNative.callOriginal(password);
      log("startup-login-hook-leave original=" + original);

      log("startup-login-hook-return forced=true");
      return true;
    };

    log("startup-login-hook-installed");
  } catch (error) {
    log("startup-login-hook-error " + error);
    throw error;
  }
});
