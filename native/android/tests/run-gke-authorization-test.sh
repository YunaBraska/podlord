#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

write() {
    mkdir -p "$tmp/src/$(dirname "$1")"
    cat > "$tmp/src/$1"
}

write android/app/Activity.java <<'EOF'
package android.app;
import android.content.Intent;
import android.content.IntentSender;
import java.util.ArrayList;
import java.util.List;
public class Activity {
    public static final int RESULT_OK = -1;
    public final List<Integer> requestCodes = new ArrayList<>();
    public void startIntentSenderForResult(IntentSender sender, int requestCode, Intent data,
        int flagsMask, int flagsValues, int extraFlags) throws IntentSender.SendIntentException {
        requestCodes.add(requestCode);
    }
}
EOF
write android/app/PendingIntent.java <<'EOF'
package android.app;
import android.content.IntentSender;
public final class PendingIntent {
    public IntentSender getIntentSender() { return new IntentSender(); }
}
EOF
write android/content/Intent.java <<'EOF'
package android.content;
public final class Intent {
    public final String token;
    public Intent(String token) { this.token = token; }
}
EOF
write android/content/IntentSender.java <<'EOF'
package android.content;
public final class IntentSender {
    public static final class SendIntentException extends Exception {}
}
EOF
write com/google/android/gms/auth/api/identity/AuthorizationClient.java <<'EOF'
package com.google.android.gms.auth.api.identity;
import android.content.Intent;
import com.google.android.gms.common.api.ApiException;
import com.google.android.gms.tasks.Task;
public interface AuthorizationClient {
    Task<AuthorizationResult> authorize(AuthorizationRequest request);
    AuthorizationResult getAuthorizationResultFromIntent(Intent data) throws ApiException;
}
EOF
write com/google/android/gms/auth/api/identity/AuthorizationRequest.java <<'EOF'
package com.google.android.gms.auth.api.identity;
import com.google.android.gms.common.api.Scope;
import java.util.List;
public final class AuthorizationRequest {
    public static Builder builder() { return new Builder(); }
    public static final class Builder {
        public Builder setRequestedScopes(List<Scope> scopes) { return this; }
        public AuthorizationRequest build() { return new AuthorizationRequest(); }
    }
}
EOF
write com/google/android/gms/auth/api/identity/AuthorizationResult.java <<'EOF'
package com.google.android.gms.auth.api.identity;
import android.app.PendingIntent;
import java.util.List;
public final class AuthorizationResult {
    private final List<String> scopes;
    private final String token;
    private final PendingIntent pendingIntent;
    public AuthorizationResult(List<String> scopes, String token, PendingIntent pendingIntent) {
        this.scopes = scopes;
        this.token = token;
        this.pendingIntent = pendingIntent;
    }
    public boolean hasResolution() { return pendingIntent != null; }
    public PendingIntent getPendingIntent() { return pendingIntent; }
    public List<String> getGrantedScopes() { return scopes; }
    public String getAccessToken() { return token; }
}
EOF
write com/google/android/gms/auth/api/identity/Identity.java <<'EOF'
package com.google.android.gms.auth.api.identity;
import android.app.Activity;
public final class Identity {
    public static AuthorizationClient client;
    public static AuthorizationClient getAuthorizationClient(Activity activity) { return client; }
}
EOF
write com/google/android/gms/common/ConnectionResult.java <<'EOF'
package com.google.android.gms.common;
public final class ConnectionResult { public static final int SUCCESS = 0; }
EOF
write com/google/android/gms/common/GoogleApiAvailability.java <<'EOF'
package com.google.android.gms.common;
import android.app.Activity;
public final class GoogleApiAvailability {
    private static final GoogleApiAvailability INSTANCE = new GoogleApiAvailability();
    public int availability;
    public static GoogleApiAvailability getInstance() { return INSTANCE; }
    public int isGooglePlayServicesAvailable(Activity activity) { return availability; }
}
EOF
write com/google/android/gms/common/api/ApiException.java <<'EOF'
package com.google.android.gms.common.api;
public class ApiException extends Exception {
    private final int statusCode;
    public ApiException(int statusCode) { this.statusCode = statusCode; }
    public int getStatusCode() { return statusCode; }
}
EOF
write com/google/android/gms/common/api/CommonStatusCodes.java <<'EOF'
package com.google.android.gms.common.api;
public final class CommonStatusCodes {
    public static final int ERROR = 1;
    public static final int CANCELED = 2;
    public static final int API_NOT_CONNECTED = 3;
}
EOF
write com/google/android/gms/common/api/Scope.java <<'EOF'
package com.google.android.gms.common.api;
public final class Scope {
    public Scope(String scope) {}
}
EOF
write com/google/android/gms/tasks/Task.java <<'EOF'
package com.google.android.gms.tasks;
import java.util.function.Consumer;
public final class Task<T> {
    private final T result;
    public Task(T result) { this.result = result; }
    public Task<T> addOnSuccessListener(Consumer<? super T> listener) { listener.accept(result); return this; }
    public Task<T> addOnFailureListener(Consumer<? super Exception> listener) { return this; }
}
EOF

find "$tmp/src" -name '*.java' -print > "$tmp/sources"
printf '%s\n' "$here/../src/berlin/yuna/podlord/GkeAuthorization.java" "$here/GkeAuthorizationTest.java" >> "$tmp/sources"
mkdir -p "$tmp/classes"
javac -d "$tmp/classes" @"$tmp/sources"
java -cp "$tmp/classes" berlin.yuna.podlord.GkeAuthorizationTest
