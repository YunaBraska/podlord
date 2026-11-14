# How does the native inspector own Pod logs?

Status: accepted implementation of the confirmed log contract.

## Decision

Use the existing resource client, request gate, authentication state and network
manager. A second HTTP client or independent retry policy is unnecessary.
Each session and Pod UID owns its cached history and container selection. Each
selection retains its own reading position. A recreated Pod must not inherit the
previous UID's log history.

[ADR 0022](0022-native-resource-metadata-authority.md) owns the shared list/detail
metadata ordering used to identify that Pod. Wall-clock adjustment or a late
pagination page must not restore metadata from the previous UID.

The multi-container selector initially displays `all`. Its selection key is `*`,
which cannot be a Kubernetes container name. A real container named `all` remains
individually selectable as `all (container)`. Each HTTP request specifies one
actual container, never the aggregate selection key.

The single visible log owner schedules automatic cycles at least three seconds
apart, measured from actual dispatch. Cycles do not overlap. Explicit opening,
selection and resume use foreground priority while respecting the shared ceiling,
minimum request spacing, authentication suspension and server backoff. Resource
synchronization is not held hostage by a continuously active log view.

Foreground log reads share the inspector-detail priority above bulk discovery;
merely marking them foreground must not leave them behind an initial foreground
resource batch. Requests reuse the existing API `Accept: application/json`
negotiation, while successful log bodies are parsed as timestamped plain text.
The real API rejected an exclusive `text/plain` Accept header with HTTP 406.

Hiding/minimizing the window, leaving the log view, changing selection or closing
the session removes obsolete unsent reads. Sent reads complete into their original
cache; they cannot repaint a different or hidden view. Authentication failures do
not launch automatic login or retry. Pause freezes the presented snapshot; resume
publishes the matching cache and requests a foreground refresh.

Timestamped UTF-8 responses are merged chronologically, preserving nanosecond
ordering and a visible container origin. Incremental requests include the last
timestamp; duplicate boundary occurrences are reconciled without deleting real
repeated messages. Malformed responses and partial container failures retain
available history and name the failed container. Log text is plain text, not HTML.

The default retained-history limit is five decimal MB per Pod view, shared across
containers. Settings accept positive whole MB values. Lowering the limit trims
immediately; oldest entries go first. Response draining is bounded and does not
abort an already sent request. A paused presentation and the cached history are
separately bounded snapshots; this setting is not a total-process RAM guarantee.
The existing cache expiry owner also expires inactive container history.

The virtualized native list applies incremental changes rather than resetting on
each refresh. A reading anchor and pixel offset survive retained-entry updates.
If the anchor is removed, the view stays out of live-follow mode, shows the oldest
retained entry and distinguishes size eviction from cache expiry. Full entry and
copy actions read the cached entry, not the clipped list label.

Read settings remain atomic in the explicit private profile. Version one loads
with the five MB default; saving writes version two with the new setting. Invalid
input and conflicting concurrent writes do not replace the previous settings.

## Evidence and limits

The [test map](../spec/k3d-test-map.md) records executable public-QML scenarios,
real local Kubernetes runs, failures reproduced before fixes and coverage gates.
The external Kubernetes HTTP boundary alone is simulated in deterministic tests;
owned stores, request queue, authentication, timers, model and QML execute normally.
The real runner rejects remote Docker endpoints and removes only its own container
and actual anonymous volumes. It preserves the pinned test image, without global
pruning or modifying production/shared clusters.

This decision does not declare legacy log parity, total native migration,
accessibility, performance or release readiness. Those require their remaining
scenario and platform evidence. Rollback remains the independently preserved
legacy reference and profile; no production legacy profile is overwritten.
