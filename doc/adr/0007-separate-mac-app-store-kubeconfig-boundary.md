# 0007 Separate Mac App Store kubeconfig boundary

## Status

Accepted

## Context

The direct PodLord distribution can import the home kubeconfig, scan directories, watch source files, and run a kubeconfig `exec` credential command. A Mac App Store build must run in the App Sandbox and cannot rely on arbitrary files or external programs.

## Decision

Build the Store edition with `PodlordAppStore=true`. It imports individual kubeconfig files selected through the system picker or pasted YAML, then uses its app-owned snapshot. It does not automatically read or watch source files, scan directories, accept typed paths, run k3d, check GitHub Releases, or run kubeconfig `exec` credentials. Store imports require embedded token or certificate data.

The direct download keeps the existing import, refresh, and authentication behavior.

## Consequences

One codebase produces two deliberately different distribution channels. The Store build preserves Kubernetes resource operations while making its file and credential boundary explicit. Users whose clusters require an external login command use the notarized direct download.
