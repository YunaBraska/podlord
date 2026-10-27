package berlin.yuna.podlord;

import android.app.Activity;
import android.content.Intent;
import android.content.IntentSender;
import com.google.android.gms.auth.api.identity.AuthorizationClient;
import com.google.android.gms.auth.api.identity.AuthorizationRequest;
import com.google.android.gms.auth.api.identity.AuthorizationResult;
import com.google.android.gms.auth.api.identity.Identity;
import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;
import com.google.android.gms.common.api.ApiException;
import com.google.android.gms.common.api.CommonStatusCodes;
import com.google.android.gms.common.api.Scope;
import java.util.Arrays;
import java.util.List;

final class GkeAuthorization {
    private static final int REQUEST_CODE_PREFIX = 0x47000000;
    private static final int REQUEST_CODE_MASK = 0xff000000;
    private static final long MAX_REQUEST_ID = 0x00ffffffL;
    private static final int SUCCESS = 0;
    private static final int CANCELLED = 1;
    private static final int UNAVAILABLE = 2;
    private static final int FAILURE = 3;
    private static final String CLOUD_SCOPE = "https://www.googleapis.com/auth/cloud-platform";
    private static final String EMAIL_SCOPE = "https://www.googleapis.com/auth/userinfo.email";

    private final Activity activity;
    private final Completion completion;
    private Attempt active;

    GkeAuthorization(final Activity activity) {
        this(activity, GkeAuthorization::nativeComplete);
    }

    GkeAuthorization(final Activity activity, final Completion completion) {
        this.activity = activity;
        this.completion = completion;
    }

    boolean start(final long requestId) {
        if (active != null) {
            return false;
        }
        if (requestId <= 0 || requestId > MAX_REQUEST_ID) {
            completion.complete(requestId, FAILURE, null);
            return true;
        }
        final int availability = GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(activity);
        if (availability != ConnectionResult.SUCCESS) {
            completion.complete(requestId, UNAVAILABLE, null);
            return true;
        }
        final Attempt attempt = new Attempt(requestId, REQUEST_CODE_PREFIX | (int) requestId,
            Identity.getAuthorizationClient(activity));
        active = attempt;
        final AuthorizationRequest request = AuthorizationRequest.builder()
            .setRequestedScopes(Arrays.asList(new Scope(CLOUD_SCOPE), new Scope(EMAIL_SCOPE)))
            .build();
        attempt.client.authorize(request)
            .addOnSuccessListener(result -> authorized(attempt, result))
            .addOnFailureListener(failure -> failed(attempt, failure));
        return true;
    }

    boolean cancel(final long requestId) {
        if (active == null || active.requestId != requestId) {
            return false;
        }
        active = null;
        return true;
    }

    boolean activityResult(final int requestCode, final int resultCode, final Intent data) {
        if ((requestCode & REQUEST_CODE_MASK) != REQUEST_CODE_PREFIX) {
            return false;
        }
        if (active == null || active.requestCode != requestCode) {
            return true;
        }
        final Attempt attempt = active;
        if (resultCode != Activity.RESULT_OK || data == null) {
            complete(attempt, CANCELLED, null);
            return true;
        }
        try {
            authorized(attempt, attempt.client.getAuthorizationResultFromIntent(data));
        } catch (ApiException failure) {
            failed(attempt, failure);
        }
        return true;
    }

    boolean activityDestroyed() {
        if (active == null) {
            return false;
        }
        complete(active, CANCELLED, null);
        return true;
    }

    private void authorized(final Attempt attempt, final AuthorizationResult result) {
        if (active != attempt) {
            return;
        }
        if (result.hasResolution()) {
            try {
                activity.startIntentSenderForResult(result.getPendingIntent().getIntentSender(), attempt.requestCode, null, 0, 0, 0);
            } catch (IntentSender.SendIntentException failure) {
                complete(attempt, FAILURE, null);
            }
            return;
        }
        final List<String> scopes = result.getGrantedScopes();
        if (scopes == null || !scopes.contains(CLOUD_SCOPE) || !scopes.contains(EMAIL_SCOPE)) {
            complete(attempt, FAILURE, null);
            return;
        }
        complete(attempt, SUCCESS, result.getAccessToken());
    }

    private void failed(final Attempt attempt, final Exception failure) {
        if (active != attempt) {
            return;
        }
        final int status = failure instanceof ApiException ? ((ApiException) failure).getStatusCode() : CommonStatusCodes.ERROR;
        complete(attempt, status == CommonStatusCodes.CANCELED ? CANCELLED
            : status == CommonStatusCodes.API_NOT_CONNECTED ? UNAVAILABLE : FAILURE, null);
    }

    private void complete(final Attempt attempt, final int status, final String accessToken) {
        if (active != attempt) {
            return;
        }
        active = null;
        completion.complete(attempt.requestId, status, accessToken);
    }

    interface Completion {
        void complete(long requestId, int status, String accessToken);
    }

    private static native void nativeComplete(long requestId, int status, String accessToken);

    private static final class Attempt {
        private final long requestId;
        private final int requestCode;
        private final AuthorizationClient client;

        private Attempt(final long requestId, final int requestCode, final AuthorizationClient client) {
            this.requestId = requestId;
            this.requestCode = requestCode;
            this.client = client;
        }
    }
}
