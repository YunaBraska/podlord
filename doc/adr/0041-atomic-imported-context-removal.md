# How Can A Context And Its Sessions Be Removed Atomically?

Status: accepted, 2026-10-06.

## Decision

The user confirms the selected context and the exact affected saved sessions.
One atomic session-catalog commit removes those sessions and records the context
identity as unavailable. Source resolution and listing honor that same catalog.
New sessions cannot revive an excluded context. Changed session membership
invalidates confirmation instead of silently widening its scope.

Catalog version 3 adds sorted, unique context exclusions. Existing version 2
catalogs remain readable and remain version 2 until exclusions are needed.
Malformed or unsupported catalogs fail closed without replacement.

## Why Not Write Both Stores?

Two independent file replacements cannot provide one atomic cascade. A rollback
after the second replacement fails can itself fail or be interrupted. A journal
would introduce recovery behavior into every source and session writer.

The immutable imported kubeconfig remains the credential/content authority for
its other contexts. The catalog is the availability and saved-session authority.
Neither original kubeconfig files nor the shared immutable content are rewritten.
There is no second deletion write or partially committed cascade.

## Lifecycle And Reimport

After a successful commit the owning workspace closes removed session tabs,
log streams, alert state and port-forwards. Existing ordinary dispatched reads
finish under their existing lifecycle rules; resource caches retain their TTL.
Other sessions and contexts are unaffected. An unapplied owned YAML draft must
be saved or explicitly discarded before removal.

Explicitly reimporting the unchanged source restores excluded context
availability through one catalog commit, retaining the existing snapshot import
time. It never restores deleted sessions. Changed source content is still a new
immutable snapshot. This local availability change does not authenticate or
contact Kubernetes.

## Verification And Compatibility

Public workspace/UI tests cover confirmation, cancel, active/background tabs,
empty membership, stale confirmation, writer locks, corrupt stores, restart and
explicit reimport. Older native binaries reject version 3 rather than silently
losing exclusions; rollback requires a separately retained pre-upgrade profile.
The independently isolated C# reference profile is unchanged.
