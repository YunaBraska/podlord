package berlin.yuna.podlord;

import android.content.Intent;
import android.content.res.Configuration;
import android.view.KeyEvent;
import org.qtproject.qt.android.bindings.QtActivity;

/** Qt activity for native input and lifecycle-bound Android authorization. */
public final class PodlordActivity extends QtActivity {
    private final GkeAuthorization gkeAuthorization = new GkeAuthorization(this);

    /**
     * Routes Home and End through Qt before the platform input editor consumes them.
     * @param event original Android event, including modifiers and repeat state
     * @return whether Qt or the normal Android dispatcher handled the event
     */
    @Override
    public boolean dispatchKeyEvent(final KeyEvent event) {
        final int key = event.getKeyCode();
        if (key == KeyEvent.KEYCODE_MOVE_HOME || key == KeyEvent.KEYCODE_MOVE_END) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && super.onKeyDown(key, event)) return true;
            if (event.getAction() == KeyEvent.ACTION_UP && super.onKeyUp(key, event)) return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private static native void notifyTypographyChanged();

    @Override
    public void onConfigurationChanged(final Configuration configuration) {
        super.onConfigurationChanged(configuration);
        try {
            notifyTypographyChanged();
        } catch (final UnsatisfiedLinkError notStarted) {
            // Qt reads the current configuration when its native library starts.
        }
    }

    /** Starts one user-confirmed GKE authorization attempt. */
    public boolean startGkeAuthorization(final long requestId) {
        return gkeAuthorization.start(requestId);
    }

    /** Cancels the matching GKE authorization attempt without completing another attempt. */
    public boolean cancelGkeAuthorization(final long requestId) {
        return gkeAuthorization.cancel(requestId);
    }

    @Override
    protected void onActivityResult(final int requestCode, final int resultCode, final Intent data) {
        if (!gkeAuthorization.activityResult(requestCode, resultCode, data)) {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    @Override
    protected void onDestroy() {
        gkeAuthorization.activityDestroyed();
        super.onDestroy();
    }
}
