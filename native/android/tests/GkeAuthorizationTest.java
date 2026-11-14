package berlin.yuna.podlord;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Intent;
import com.google.android.gms.auth.api.identity.AuthorizationClient;
import com.google.android.gms.auth.api.identity.AuthorizationRequest;
import com.google.android.gms.auth.api.identity.AuthorizationResult;
import com.google.android.gms.auth.api.identity.Identity;
import com.google.android.gms.common.GoogleApiAvailability;
import com.google.android.gms.common.api.ApiException;
import com.google.android.gms.tasks.Task;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Queue;

public final class GkeAuthorizationTest {
    private static final String CLOUD = "https://www.googleapis.com/auth/cloud-platform";
    private static final String EMAIL = "https://www.googleapis.com/auth/userinfo.email";
    private static final List<String> SCOPES = Arrays.asList(CLOUD, EMAIL);

    private record Completion(long requestId, int status, String token) {}

    public static void main(final String[] args) {
        staleConsentDoesNotCompleteNewAttempt();
        teardownCancelsRatherThanFallingBack();
        unrelatedActivityResultStillDelegatesToQt();
        exhaustedRequestCodeFailsExplicitly();
        missingPlayServicesRemainsUnavailable();
        missingScopeFailsClosed();
    }

    private static void staleConsentDoesNotCompleteNewAttempt() {
        final Activity activity = new Activity();
        final FakeClient client = new FakeClient();
        client.queue.add(new AuthorizationResult(SCOPES, null, new PendingIntent()));
        client.queue.add(new AuthorizationResult(SCOPES, null, new PendingIntent()));
        Identity.client = client;
        GoogleApiAvailability.getInstance().availability = 0;
        final List<Completion> completions = new ArrayList<>();
        final GkeAuthorization authorization = new GkeAuthorization(activity,
            (id, status, token) -> completions.add(new Completion(id, status, token)));

        check(authorization.start(1), "attempt A did not start");
        final int codeA = activity.requestCodes.get(0);
        check(authorization.cancel(1), "attempt A did not cancel");
        check(authorization.start(2), "attempt B did not start");
        final int codeB = activity.requestCodes.get(1);
        check(codeA != codeB, "consent results share a request code");
        check(authorization.activityResult(codeA, Activity.RESULT_OK, new Intent("old-token")),
            "stale consent was delegated to Qt");
        check(completions.isEmpty(), "stale consent completed attempt B");
        check(client.parsedResults == 0, "stale consent was parsed for attempt B");
        check(authorization.activityResult(codeB, Activity.RESULT_OK, new Intent("new-token")),
            "attempt B consent was not handled");
        check(completions.equals(List.of(new Completion(2, 0, "new-token"))),
            "attempt B did not complete with its own token");
    }

    private static void teardownCancelsRatherThanFallingBack() {
        final Activity activity = new Activity();
        final FakeClient client = new FakeClient();
        client.queue.add(new AuthorizationResult(SCOPES, null, new PendingIntent()));
        Identity.client = client;
        GoogleApiAvailability.getInstance().availability = 0;
        final List<Completion> completions = new ArrayList<>();
        final GkeAuthorization authorization = new GkeAuthorization(activity,
            (id, status, token) -> completions.add(new Completion(id, status, token)));

        check(authorization.start(3), "attempt did not start");
        final int code = activity.requestCodes.get(0);
        check(authorization.activityDestroyed(), "teardown did not complete the attempt");
        check(completions.equals(List.of(new Completion(3, 1, null))),
            "teardown did not report cancellation");
        check(authorization.activityResult(code, Activity.RESULT_OK, new Intent("late-token")),
            "late consent was delegated to Qt");
        check(completions.size() == 1, "late consent completed a disposed attempt");
    }

    private static void unrelatedActivityResultStillDelegatesToQt() {
        final GkeAuthorization authorization = new GkeAuthorization(new Activity(), (id, status, token) -> {});
        check(!authorization.activityResult(17, Activity.RESULT_OK, new Intent("other")),
            "unrelated result was consumed");
    }

    private static void exhaustedRequestCodeFailsExplicitly() {
        final List<Completion> completions = new ArrayList<>();
        final GkeAuthorization authorization = new GkeAuthorization(new Activity(),
            (id, status, token) -> completions.add(new Completion(id, status, token)));
        check(authorization.start(0x01000000L), "exhausted code was not handled");
        check(completions.equals(List.of(new Completion(0x01000000L, 3, null))),
            "exhausted code did not fail explicitly");
    }

    private static void missingPlayServicesRemainsUnavailable() {
        GoogleApiAvailability.getInstance().availability = 1;
        final List<Completion> completions = new ArrayList<>();
        final GkeAuthorization authorization = new GkeAuthorization(new Activity(),
            (id, status, token) -> completions.add(new Completion(id, status, token)));
        check(authorization.start(4), "unavailable authorization was not handled");
        check(completions.equals(List.of(new Completion(4, 2, null))),
            "missing Play Services did not remain unavailable");
    }

    private static void missingScopeFailsClosed() {
        final FakeClient client = new FakeClient();
        client.queue.add(new AuthorizationResult(List.of(CLOUD), "scopeless-token", null));
        Identity.client = client;
        GoogleApiAvailability.getInstance().availability = 0;
        final List<Completion> completions = new ArrayList<>();
        final GkeAuthorization authorization = new GkeAuthorization(new Activity(),
            (id, status, token) -> completions.add(new Completion(id, status, token)));
        check(authorization.start(5), "scope failure was not handled");
        check(completions.equals(List.of(new Completion(5, 3, null))),
            "missing email scope did not fail closed");
    }

    private static void check(final boolean condition, final String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static final class FakeClient implements AuthorizationClient {
        private final Queue<AuthorizationResult> queue = new ArrayDeque<>();
        private int parsedResults;

        @Override
        public Task<AuthorizationResult> authorize(final AuthorizationRequest request) {
            return new Task<>(queue.remove());
        }

        @Override
        public AuthorizationResult getAuthorizationResultFromIntent(final Intent data) throws ApiException {
            parsedResults++;
            return new AuthorizationResult(SCOPES, data.token, null);
        }
    }
}
