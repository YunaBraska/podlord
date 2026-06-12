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

Trade-offs:

- Multiple clusters currently share one process-wide gate, which is conservative but slower than a per-cluster limiter.
- The current engine still uses list/poll cache warming rather than full watch supervision.
- Streaming log follow is represented by queued tail polling until a native watch/stream supervisor is added.
