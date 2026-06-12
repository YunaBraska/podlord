# 0003: K3d Generated Kubeconfig Sources

## Status

Accepted

## Context

Podlord supports multiple kubeconfig sources: real files, pasted text, and generated sources. K3d clusters expose kubeconfigs through `k3d kubeconfig get <cluster>`, not through a stable user-owned source file that Podlord should watch or mutate.

## Decision

K3d imports are stored as generated virtual sources using the `podlord-generated://` scheme. Podlord imports the kubeconfig text, normalizes local `0.0.0.0` endpoints to `127.0.0.1`, stores an app-owned copy, and creates sessions from that copy.

Generated sources are not file-watched and are not refreshed by `RefreshImportedKubeconfigs`. Re-importing k3d is an explicit user action.

## Consequences

- K3d support does not touch `$HOME/.kube/config`.
- Generated k3d sessions behave like other app-owned kubeconfig sessions.
- Source refresh stays predictable: real files are watched, virtual/generated sources are not.
- Later k3d lifecycle controls can update the generated source model without changing file source behavior.
