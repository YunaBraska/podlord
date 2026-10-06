# 0002 - Cache-First Rate-Limited Kubernetes Requests

## Status

Accepted

## Context

Podlord must not spam Kubernetes APIs. The UI should render from a local cache, while background work fills that cache at a controlled pace. User-visible actions such as focusing one resource or tailing logs may request fresher data, but those requests must still respect Kubernetes backoff and API pressure.

Early Avalonia builds could trigger `429 TooManyRequests` against k3d because broad scans and detail/log requests were not coordinated across service instances.

## Decision

Use a cache-first resource service with a prioritized request queue and a process-wide Kubernetes request gate.

- UI reads cached snapshots first.
- Background refresh warms selected remote scopes.
- Detail and log requests use cached data immediately when available, then refresh through the queue.
- Foreground, user-visible, and background requests share one serialized gate.
- `Retry-After` from Kubernetes is honored before the next request instead of immediately surfacing a noisy UI error.
- Real k3d tests run without test-level API stampedes.

## Consequences

Positive:

- The UI stays responsive because filtering, sorting, radar, graph, and focus rendering use local snapshots.
- Kubernetes receives bounded request pressure even when filters, source refreshes, details, and logs are active.
- The behavior is proven against a disposable k3d cluster with pods, deployments, events, secrets, RBAC, logs, and rate-limit-sensitive broad scans.

## Confirmed Scheduling And Lifecycle Extension

Confirmed on 2026-10-02; implementation conformance for this extension remains unreviewed. SYN-001 through SYN-013 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-cache-and-request-scheduling-requirements) own the extended behavior.

Keep central session synchronization separate from surface lifecycle. Prioritize explicit foreground reads over waiting background work without bypassing the request gate or interrupting dispatched work. Share equivalent reads only within their original session and authorization context; a shared read remains relevant while any consumer still needs it.

Remove wholly obsolete queued reads, but process already dispatched results in their original context. Visibility changes are not grounds for cancelling an in-flight request or restarting hidden refresh work. Stable Settings snapshots and separate discovery/live freshness intervals avoid unnecessary UI rebuilding and repeated metadata fetches.

This extension does not select new numeric TTLs, change write retry authorization, or extend cache retention after errors. Its planned public-boundary checks are recorded in the owning specification rather than claimed as executed evidence here.

## Confirmed Error Recovery Extension

Confirmed on 2026-10-02; implementation conformance remains unreviewed. ERR-001 through ERR-014 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-error-and-recovery-requirements) own failure classification, recovery feedback, and authentication retry authorization.

Authentication is not ordinary transient network recovery. Retrying an expired token or another authentication failure can re-execute a credential provider and launch an interactive login browser. Suspend automatic authentication attempts for the affected credential context and require user confirmation for each recovery attempt. A timer, another consumer, or a view change is not new authorization. Unrelated working sessions remain usable.

2026-10-06: The native resolver preserves the reference's read-only cached auth-provider token capability. It accepts `config.access-token`, then `config.id-token`, only when no explicit token/token-file value is selected. These values use the existing bearer-token validation and request owner; they do not authorize provider execution, browser launch or refresh-token exchange. Missing cached tokens fail explicitly with external-refresh/reimport guidance. Combining legacy provider and exec configuration is rejected rather than selecting an implicit executable. Reimport creates a new snapshot; existing sessions keep their original inline provider credentials and configuration binding. Referenced token files remain external files, not frozen copies. Unsupported impersonation includes `as-uid`, which must not silently disappear.

Keep repeated failures in persistent status instead of generating repeated notifications. A requested retry or opened login browser is not evidence of recovery; clear the affected failure only after a successful check. Read recovery does not authorize replaying writes.

## Confirmed Startup And Authentication Presentation Extension

Confirmed on 2026-10-02; implementation conformance remains unreviewed. STR-001 through STR-010 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-startup-and-workspace-restoration-requirements) own workspace and settings restoration.

Immediate cache presentation is a general invariant, including startup, cluster outages, and active login. Reuse the existing health/energy/loading indicator to communicate authentication state and available progress rather than introduce a second competing status surface. Show a clearly visible, session-specific reauthentication action when renewed login is supported; keep its execution confirmation-gated. Unknown progress is not a basis for invented percentages.

Workspace restoration preserves navigation and configuration, not authorization for operational side effects: it must not automatically launch interactive login, recreate port-forwards, or apply YAML. This extension does not introduce new disk caches or draft persistence.

Trade-offs:

- Multiple clusters currently share one process-wide gate, which is conservative but slower than a per-cluster limiter.
- The current engine still uses list/poll cache warming rather than full watch supervision.
- Streaming log follow is represented by queued tail polling until a native watch/stream supervisor is added.
