# 0003: Keep k3d as Test Infrastructure

## Status

Superseded for the native release on 2026-10-29 by the confirmed standalone
product boundary. K3d remains test infrastructure, not a production integration.
The decision below describes historical records and the preserved C# reference.
Existing records remain readable under [ADR 0017](0017-native-kubeconfig-snapshot-records.md).

## Context

Podlord used to discover local k3d clusters and run `k3d kubeconfig get` in the desktop process. A k3d kubeconfig is an ordinary Kubernetes kubeconfig, so the product had a special integration without gaining a distinct Kubernetes capability.

## Decision

Podlord imports all user-provided kubeconfigs through the same file, directory, home-config, or pasted-text paths. It does not discover, invoke, or otherwise special-case k3d. k3d remains test infrastructure for the real Kubernetes integration suite.

## Consequences

- Production code does not run user-installed Kubernetes tools.
- A kubeconfig exported from k3d works like every other kubeconfig.
- The disposable k3d test suite remains the public-boundary proof for Kubernetes behavior.
