package berlin.yuna.podlord;

import android.content.Intent;
import android.os.Bundle;
import org.qtproject.qt.android.bindings.QtActivity;

/** Qt activity that owns Android authorization attempts for their complete lifecycle. */
public final class PodlordActivity extends QtActivity {
    private final GkeAuthorization gkeAuthorization = new GkeAuthorization(this);

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
