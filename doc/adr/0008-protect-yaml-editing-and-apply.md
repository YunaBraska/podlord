# 0008 Protect YAML Editing And Apply

## Status

Accepted on 2026-10-01. Implementation conformance remains to be reviewed.

## Context

The inspector displays cached data immediately and refreshes it asynchronously. Cached YAML is useful for inspection but may no longer reflect the resource that an edit would modify. A later response can also arrive while an operator is editing, and other actors can change the resource before the operator applies the draft.

These cases require distinct treatment: immediate cached presentation, an editing baseline obtained from a fresh request, a preserved local draft, and an explicit conflict when the server version has changed.

## Decision

Use the confirmed inspector contract in [the operational specification](../spec/podlord-operational-spec.md#confirmed-inspector-requirements), particularly INS-007 through INS-028, as the source of behavioral requirements.

Keep the user's editing baseline and draft distinct from refreshed server data. Refresh completion is not authority to replace the draft. Applying a draft must protect against server changes after the editing baseline, rather than assuming the initial fresh read remains current indefinitely.

Make the intended change and its target reviewable before dispatch. An explicit confirmation in the preview establishes the user's authorization for that change; cancelling the preview preserves the draft without sending it.

Treat a lost response after dispatch as an uncertain outcome, not permission to resend. Read-back supplies evidence of the observed resource state, not an exactly-once receipt. Retain the original editing baseline and require the existing confirmation and conflict safeguards for a renewed apply.

The same confirmation and uncertain-outcome principles apply to supported resource deletions, as confirmed on 2026-10-01 in ACT-001 through ACT-010 of the [operational specification](../spec/podlord-operational-spec.md#confirmed-resource-deletion-requirements). Deletion confirmation binds authorization to identified targets; uncertainty does not authorize retransmission or escalation to force-delete. This extension does not add new resource action types.

## Alternatives And Trade-offs

- Allowing edits immediately from cached YAML reduces the initial wait but exposes users to an outdated baseline. It was rejected in favor of fresh-read gating.
- Replacing editor content on refresh would keep the displayed server content current but could erase work. Preserving the draft requires a separate representation of newer server data.
- Applying against an earlier baseline without conflict protection risks overwriting concurrent changes. Conflict reporting may require the operator to compare and reconcile changes before applying again.
- Blind write retries can obscure a successful first request or overwrite intervening changes. Read-back adds a request and may leave uncertainty unresolved, but avoids an unapproved second write. Kubernetes update semantics and conflict handling are described in the [API concepts](https://kubernetes.io/docs/reference/using-api/api-concepts/#updates-to-existing-resources) and [Server-Side Apply documentation](https://kubernetes.io/docs/reference/using-api/server-side-apply/).

## Consequences

Cached inspection remains available during loading and failures. Editing waits for successful fresh YAML loading. Draft preservation, discard confirmation, and concurrency protection must be exercised at the inspector and server-operation boundaries in the later implementation review.

## Native Draft Ownership

The existing workspace owns a local editing baseline and draft, distinct from
the shared resource cache. A completed, accepted GET for the current session and
resource permits editing; a cached timestamp or unchanged document notification
does not. Starting another explicit detail read removes that permission until a
successful completion. An existing draft survives subsequent read failures,
cache expiry and version changes instead of being replaced or silently rebased.

Starting editing requires the received resource UID and opaque resource version.
The original accepted document is retained with the editing baseline, including
its actual Secret values. Editable presentation starts with the same masked YAML
as inspection; the original document is not exposed through a reveal of another
field or through bulk YAML copy. Copy-visible-YAML is unavailable during draft
editing. Redacted text is not authority for a future apply payload.

A single pending leave intent captures the requested resource path or session/
context identity before opening the discard dialog. Stay and Escape preserve
the draft and do not perform that action. Discard clears the local draft and
continues the captured action, not a subsequently reordered table row. Clean
editing ends before asynchronous tab/context transitions begin, preventing a
new dirty draft from appearing after closure has already started. The native
window close event uses the same guard; confirmed closure is a separate signal.
The dialog initially focuses Stay with a visible native keyboard focus cue.

Draft edits notify their own text/state properties rather than publishing the
whole workspace on every keystroke. There is no draft disk persistence, second
resource cache, independent refresh loop or additional dependency.

These ownership rules implement the local editing/discard boundary only. Apply,
diff/target preview, server conflict protection, reconciliation and uncertain
write read-back remain unimplemented. The UI explicitly says apply is unavailable.
The [test map](../spec/k3d-test-map.md) records executed QML and real-cluster
workflows, test reliability gaps and outstanding conformance gates.

This decision does not select a transport mechanism or a merge algorithm and does not claim these safeguards are already implemented.

## How does native Apply enforce the accepted contract?

The native transport uses a minimal JSON Patch, starting with `test` operations
for the captured UID and opaque `resourceVersion`. Only changed fields follow;
objects are compared by field and arrays are atomic values. Kubernetes checks the
patch against the current object, rather than allowing a full-document overwrite
that might silently replace concurrent fields. This follows the conditional-update
model in [Kubernetes API concepts](https://kubernetes.io/docs/reference/using-api/api-concepts/#updates-to-existing-resources)
and the atomic failure behavior of [JSON Patch](https://www.rfc-editor.org/rfc/rfc6902).

The confirmed target and draft are frozen before local preparation. Preparation
runs outside the UI thread with immutable inputs; stale results never open a
confirmation in a different scope or after a pending navigation. A native modal
shows the captured cluster, session, namespace, kind/name, UID, version and masked
field diff. Cancel is the initial visible keyboard focus. Only explicit Apply
queues a write. The existing request owner provides spacing, limits, authentication
suspension, TLS, redirect refusal and priority; there is no second HTTP client,
retry loop or resource cache. An unsent write can be canceled. A dispatched write
and its observational read-back finish in their original scope, including after
its view closes. Application shutdown remains the existing lifecycle exception.

A valid acknowledgement is followed by a fresh GET before editing is offered
again. Conflicts, malformed acknowledgements and uncertain transport/server
failures preserve the draft. Observing the requested field changes is reported as
an observation, not exactly-once proof. No write is automatically retried.
Authentication and authorization failures are explicit; a login is never initiated
by this recovery path. Response bodies are not used as diagnostics.

Reconciliation is a separate, deliberate three-way comparison. Unchanged local
fields retain the current server values. Equal changes agree. Overlapping changes
require an explicit mine/server choice; conflicting arrays stay atomic. A changed
UID cannot be reconciled onto the replacement object. Accepting the comparison
updates the draft baseline and creates a new preview, not a write. Secret values
and embedded last-applied Secret configuration remain masked in the comparison
and diff. Untouched placeholders restore the original bytes; a new placeholder
without an original value is rejected. Reconciled hidden local values stay attached
to the draft, not a disk cache. Completed operations release their unused payloads.

The user-approved document/patch limit defaults to **3 MiB** and is independently
configurable as a positive whole number of MiB in Settings. This is a local
pre-send bound, not a request timeout or permission to abort a running request.
The default matches the upstream API server's
[configured request-body default](https://github.com/kubernetes/kubernetes/blob/v1.35.5/staging/src/k8s.io/apiserver/pkg/server/config.go).
Alias graphs are checked without expanding cycles; conversion bounds expanded
JSON before constructing an oversized alias result. YAML tags and numbers must
be JSON-compatible. Private request settings move to schema 3; schemas 1 and 2
retain their previous values and acquire the default, using the existing atomic
compare-and-save owner.

Values explicitly entered by the user may remain visible in the local YAML draft.
Secret values in loaded YAML and all comparisons and previews remain masked. A fresh
read after a successful change masks the values again. Untouched values retain
their original bytes. Write-only `stringData` is normalized into UTF-8/base64
`data` once, before preparing the bounded patch, matching the
[Kubernetes Secret contract](https://kubernetes.io/docs/reference/kubernetes-api/core/secret-v1/).
Read-back compares this canonical intent, not a write-only field the server never
returns. Executable evidence and remaining gates belong in
[the test map](../spec/k3d-test-map.md), not this decision record.

## How Does Native Resource Deletion Bind User Authorization?

Native single-resource deletion uses the existing serialized Kubernetes request
owner, not a second transport or an external `kubectl` process. A modal freezes
the selected session, cluster endpoint, scope, API resource identity and UID.
Cancel receives initial keyboard focus. No name typing is required. A changed
selection cancels an unsent authorization instead of silently retargeting it.
The capability comes from the accepted discovery `delete` verb and the listed
UID; discovery is not a substitute for server-side RBAC.

The DELETE body is `v1/DeleteOptions` with `preconditions.uid`. This protects a
replacement created under the same name, using the upstream
[Preconditions contract](https://github.com/kubernetes/apimachinery/blob/v0.35.5/pkg/apis/meta/v1/types.go).
No zero grace period, propagation override, finalizer removal or force escalation
is added. Ordinary [Kubernetes finalization](https://kubernetes.io/docs/concepts/overview/working-with-objects/finalizers/)
remains authoritative. A valid acknowledgement means accepted, not necessarily
removed. A fresh read distinguishes absence, continued presence, termination and
a different UID without asserting exactly-once execution.

Transport loss, malformed acknowledgements and server failures trigger an
observational GET of the originating target only. Authentication failures suspend
normal requests and never initiate login automatically. Rejections remain visible;
response bodies are not copied into diagnostics. Writes never retransmit
automatically. Navigation cancels queued deletions, while dispatched deletions and
their read-back finish in the original scope. Application shutdown retains the
existing transport lifecycle exception.

An unresolved observation belongs to the originating resource-client session,
not the last open dialog. Switching resources cannot erase the requirement to
read current server state before another deletion. No operational side effect or
uncertainty record is persisted to disk. A renewed deletion still requires a new
confirmation, including the freshly observed UID. Only observed absence removes
older retained generations for that exact path in the originating session.
The read's monotonic admission time prevents erasing a newer cache observation;
unrelated paths survive. Removing only the last confirmed UID would resurrect an
older same-name generation from the collection cache after a replacement is
deliberately confirmed and deleted.

This increment provides one-target deletion only. Multi-selection deletion remains
unimplemented and must not be inferred from ACT-002. Executed public UI, failure,
real Kubernetes and cleanup evidence belongs in the existing test map.
