package org.qtproject.qt.android;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import org.qtproject.qt.android.bindings.QtApplication;

// Test-only Android lifecycle ingress. The actual Podlord Activity and native app are unchanged.
public final class PodlordStartupApplication extends QtApplication implements Application.ActivityLifecycleCallbacks {
    private boolean recreationRequested;

    @Override public void onCreate() {
        super.onCreate();
        registerActivityLifecycleCallbacks(this);
    }

    @Override public void onActivityPostCreated(final Activity activity, final Bundle state) {
        if (!activity.getIntent().getBooleanExtra("podlord_test_recreate", false)) return;
        final boolean started = QtNative.getStateDetails().isStarted;
        Log.i("PodlordStartupRegression", "created nativeStarted=" + started + " pid=" + Process.myPid());
        if (recreationRequested || started) return;
        recreationRequested = true;
        Log.i("PodlordStartupRegression", "recreate-before-start pid=" + Process.myPid());
        activity.recreate();
    }

    @Override public void onActivityCreated(final Activity activity, final Bundle state) {}
    @Override public void onActivityStarted(final Activity activity) {}
    @Override public void onActivityResumed(final Activity activity) {}
    @Override public void onActivityPaused(final Activity activity) {}
    @Override public void onActivityStopped(final Activity activity) {}
    @Override public void onActivitySaveInstanceState(final Activity activity, final Bundle state) {}
    @Override public void onActivityDestroyed(final Activity activity) {}
}
