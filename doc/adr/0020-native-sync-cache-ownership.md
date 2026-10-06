# How Does The Native Workspace Synchronize And Expire Snapshots?

Status: Accepted for the native read workspace, 2026-10-03.

## Decision

The native read controller owns one central synchronization timer, one request gate and one cache-expiry timer. QML surfaces do not own refresh loops. Rendering, hover, scrolling and keyboard focus read snapshots without fetching resources.

The gate serializes reads and preserves the legacy minimum start spacing of 400 ms. A positive request ceiling adds `ceil(60000 / requestsPerMinute)` spacing when larger; zero adds no extra ceiling. Server backoff still applies. Inspector work precedes queued foreground refreshes, which precede background work. Joining an existing read does not send a duplicate request.

Visible-session cadence uses the existing legacy focus, idle and cache-state policy: focused populated views use 20 seconds, 45 seconds or 120 seconds as activity decreases; focused empty views use 12 seconds, or 25 seconds after failure; unfocused populated views use 240 seconds and empty views use 45 seconds. These values originate in `MainWindowViewModel.BackgroundRefreshIntervalFor`, not a new performance target. An explicit action remains immediately eligible subject to the shared gate and server backoff.

Inactive open sessions synchronize only when the user enables their interval. Closed sessions never synchronize. Removing queued obsolete work does not abort already-sent reads. A response is processed for its originating state; it must not restart unwanted hidden follow-up work. Controller destruction disconnects callbacks and aborts owned transports because the application owner is gone.

The two supported native read settings are stored atomically in the explicit private profile: request ceiling and inactive-session interval, both defaulting to zero as in legacy `Domain.Settings`. Boundary validation, an exclusive file lock and comparison against the loaded settings prevent malformed input, concurrent replacement and silent lost updates. Invalid settings disable new reads rather than silently authorizing a fallback configuration. Existing resource snapshots are not persisted by this decision.

[Pod-log ownership](0021-native-pod-log-ownership.md) extends the same settings
store with a five MB retained-history limit and explicit version-one migration;
it does not introduce a second synchronization or request-policy owner.

Collections retain their authoritative timestamps. Lists expire after the existing 24 hours; details after five minutes. Closing or reopening a tab does not renew either timestamp. A session whose lists are within the existing 25-second freshness window opens without another scan. Expiry removes retained values and republishes visible snapshots when their content changes. Unchanged responses do not rebuild the visible table. Indexed resource lookup avoids scanning the full table when opening details.

Background synchronization reuses discovered collection descriptors. Manual refresh rediscovers the API. An independent discovery TTL and reconciliation of changed API membership remain undecided; this increment does not claim complete SYN-012 coverage.

Failed operation paths are not silently retried by central synchronization. Manual refresh authorizes a new attempt for those paths. Authentication remains governed by [the confirmed authentication decision](0019-confirmed-exec-authentication.md), including explicit login confirmation and no automatic browser launch.

Untrusted resource strings render as plain text. The standard shared tooltip receives escaped content, retaining the native tooltip without allowing resource markup to initiate image requests. This follows [Qt's text-format guidance](https://doc.qt.io/qt-6/qml-qtquick-text.html#textFormat-prop).

Lock contention returns `Busy`; inability to create or write the lock returns `WriteFailed`. A directory occupying the lock path is rejected explicitly before calling Qt, because the local Qt implementation reports that condition as `LockFailedError` even though no writer owns a valid lock. This distinction was reproduced through the real public settings store before correction.

## Evidence And Limits

The [test map](../spec/k3d-test-map.md) records real QML settings, inspection, cache aging, presentation safety and local HTTP observations. Scheduler cadence and owner destruction also have public controller-boundary tests; those do not constitute packaged-application shutdown or complete UI refresh evidence.

The current real Kubernetes runner exercises static and confirmed exec-certificate authentication and checks removal of its owning container and actual volumes. It does not establish complete legacy parity or platform-release readiness.

The native controller currently serializes its own requests. A process-wide ceiling across multiple independent controllers or windows is not established. Full settings migration, discovery membership changes, startup resource-cache authority, broader read-failure recovery and performance/release gates remain open. No disk resource cache, draft persistence, synthetic startup data or new transport timeout is introduced.

## Migration And Rollback

The isolated legacy reference remains unchanged and independently runnable. Native settings live only in the explicit native profile and do not replace legacy configuration. Rollback uses the isolated legacy lane; automatic interchange of native read settings and legacy settings is not claimed.
