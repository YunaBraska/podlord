# Which cached metadata describes the current resource?

Status: accepted correction of the existing cache-first contract.

## Context

List responses and explicit inspector reads can observe different instances at
the same Kubernetes path. Always preferring the list hides a newly recreated Pod
returned by the inspector. Always preferring detail data makes an older detail
hide a newer list. Wall-clock timestamps cannot order these observations safely
when time is frozen or corrected. Pagination can finish an older snapshot after
a foreground detail read has already returned newer metadata.

## Decision

The existing session resource cache owns the latest normalized metadata per path.
List snapshots still own collection membership and their existing retention lease;
detail entries retain their separate existing lease. Do not add another cache,
persist observation markers or change TTLs.

Normalize at the existing API boundary and use the already owned monotonic
`QElapsedTimer` to order accepted observations. A paginated collection carries the
first page's observation through continuation requests, rather than becoming newer
merely because its last page arrived later. These markers are process-local, not
server versions or dates; [Qt's reference-clock rules](https://doc.qt.io/qt-6/qelapsedtimer.html#reference-clocks)
prohibit treating them as portable persisted time.

An accepted detail read updates normalized metadata for an already listed path.
Collection publication preserves a later accepted metadata observation. Unchanged
list payloads still advance their observation without forcing a table rebuild.
Detail expiry does not resurrect an older list UID. The collection's existing
retention boundary remains unchanged; failed reads do not renew it.

Resource rows are projections of the cached metadata. Inspector-only `fetchedAt`
changes do not change that projection or rebuild the table. A detail entry for a
different cached UID is not returned as the current resource's detail. Deleted
membership does not manufacture a new table row from an inspector response.

The existing log owner reconciles UID changes from both metadata producers and
cache expiry. Remove obsolete unsent log work; do not cancel sent work. A valid
replacement gets its own history and container selection. If metadata no longer
supports a log owner, clear that owner and the presented log model rather than
keep a hidden refresh loop alive. Cache rendering remains read-only.

## Consequences and evidence

The metadata cache now has an explicit authority role rather than being treated
as a list-only lookup index. Observation markers add bounded per-entry bookkeeping,
not a timer, request path, dependency or persistence format. All mutations remain
on the existing Qt owner thread.

The [test map](../spec/k3d-test-map.md) owns commands and results. Public QML tests
cover detail-after-list, list-after-detail, equal/backward wall time, detail expiry,
already-open logs and a paginated older snapshot completing after a fresh detail.
The detail-after-list case first reproduced incorrect old-container HTTP requests.
These checks do not prove the full malformed-input matrix, legacy parity,
platform/accessibility behavior, performance budgets or release readiness.
