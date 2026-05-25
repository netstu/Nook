package nook.java;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;

public final class NookJsInvocationHandler implements InvocationHandler {
    private final long callbackId;

    public NookJsInvocationHandler(long callbackId) {
        this.callbackId = callbackId;
    }

    private static native Object nativeInvoke(long callbackId,
                                              Object proxy,
                                              Method method,
                                              Object[] args);

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
        return nativeInvoke(callbackId, proxy, method, args);
    }
}
