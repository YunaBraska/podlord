# 0004 Content-addressed kubeconfig snapshots

## Status

Accepted

## Context

Podlord imports kubeconfig files from user-controlled paths such as `$HOME/.kube/config`.
Those files can change outside Podlord while existing sessions still depend on the previously imported content.
Keying imported contexts only by path causes changed files to overwrite previous app-owned kubeconfig copies.

## Decision

Podlord stores each imported kubeconfig snapshot by source path and content hash.
Re-importing the same path with identical content updates the existing snapshot metadata instead of duplicating it.
Re-importing the same path after its content changes creates a new imported context snapshot and app-owned kubeconfig copy.

The source list is displayed with the most recently imported snapshots first.

## Consequences

Existing sessions remain bound to the kubeconfig content they were created from.
Automatic source refresh can discover changed kubeconfig files without mutating older snapshots.
Users may see multiple entries for the same filesystem path when that file changed over time, which is intentional and auditable.
