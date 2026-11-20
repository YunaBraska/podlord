# How Does The Native Workspace Synchronize And Expire Snapshots?

Status: accepted, 2026-10-03. Updated 2026-11-20.

## Decision

One read controller owns the synchronization, request gate, cache expiry and retry timing. UI input and rendering read cached snapshots. Up to four reads can overlap, with at least 400 ms between starts. A user request limit can increase that spacing. Writes remain exclusive. Inspector reads precede background work. Server backoff applies even to manual refresh.

The visible session's timer runs after 20, 45 or 120 seconds as activity falls. Empty focused sessions retry after 12 seconds, or 25 seconds following a failure. Unfocused populated sessions wait 240 seconds, shortened to 30 seconds during retryable failures. Hidden sessions sync only when the user enables their interval. Closed sessions do not sync. One session can move between views without copying its cache or starting another scheduler.

The timer checks which discovered collections are due. A changed collection is eligible after 25 seconds. Each identical accepted list extends the next interval, up to 180 seconds for Pods, Nodes, Events and live metrics, or 900 seconds for other kinds. A change resets that collection's interval. A newly observed resource problem prompts a read of collections containing directly linked resources. Requests for one collection are coalesced. Manual refresh rediscovers the API.

Transport failures, HTTP 429 and server errors are retried on the next session cycle without restarting. The controller remembers the failed read's kind and collection so it can retry that path without rediscovering every kind. It clears a resource failure if a later accepted list shows that resource was removed. Authentication and rejected writes are never retried this way. A newly failing read produces one audible cue per short burst when alerts are enabled.

If a cluster-wide namespaced list returns HTTP 403, the controller tries the kubeconfig context's namespace, or `default`. A successful fallback remembers the denied global route for 15 minutes. A later foreground refresh can retry it. The route belongs to that session and credential.

Collections keep their last successful timestamp and expire after the configured cache period, 24 hours by default. Details expire after five minutes. Reopening a tab does not renew either timestamp. Unchanged results do not rebuild the resource table. Diagnostics retain the last 256 request outcomes in memory and show calls started during the past minute.

## Evidence And Limits

Public controller and UI tests cover overlapping reads, request spacing, focus and inactivity, adaptive intervals, transient recovery, related-resource refresh, audit results and the audible failure signal. The clean Linux and cross-platform package checks remain release gates.

Collection refresh still uses Kubernetes list requests. A watch stream and automatic discovery of newly added API kinds are not implemented. Background synchronization never retries a denied write or launches an authentication flow.

## Migration And Rollback

Existing native profiles migrate to the current settings version. Resource snapshots and request audits are not persisted. A rollback must use a binary that accepts the profile version or restore a compatible profile backup.
