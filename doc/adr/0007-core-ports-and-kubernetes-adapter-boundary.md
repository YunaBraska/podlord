# 0007 Core Ports And Kubernetes Adapter Boundary

## Status

Accepted

## Context

Podlord has three production assemblies: Core, Kubernetes, and App. Core owns domain and application models, Kubernetes talks to the Kubernetes API, and App owns the Avalonia desktop presentation.

The boundary was mostly clean for package dependencies, but App view-model code consumed `KubernetesResourceService` and adapter-owned port-forward types directly. That made UI code depend on infrastructure implementation details and made future adapter testing harder.

## Decision

Core owns the Kubernetes-facing application ports and transport-neutral DTOs used by those ports:

- resource snapshots and cache warming
- resource details and YAML apply/delete operations
- pod logs
- native port-forward lifecycle
- request telemetry, cache telemetry, and audit entries

`Podlord.Kubernetes` implements those Core ports and remains the only production project that references `KubernetesClient` or `k8s` types. Kubernetes client objects, WebSocket demuxing, kubeconfig auth, retry/backoff, request queueing, cache storage, metrics fallback, audit recording, and secret redaction stay inside the adapter.

`Podlord.App` depends on Core-owned interfaces and models for view-model behavior. The only approved App dependency on `Podlord.Kubernetes` is `KubernetesServiceBootstrap`, the tiny composition root that constructs the concrete adapter for the desktop executable.

Avalonia types stay in `Podlord.App`. Core and Kubernetes must not reference Avalonia packages, controls, brushes, colors, windows, or UI platform types.

Architecture tests enforce these constraints by scanning project files and source files for forbidden references.

## Consequences

- App view-models can be tested against Core ports instead of concrete infrastructure.
- KubernetesClient/k8s types cannot leak into Core or general UI code without failing guardrail tests.
- Avalonia cannot leak into Core or Kubernetes without failing guardrail tests.
- The App assembly still references `Podlord.Kubernetes` for executable composition until a separate host project exists, but implementation access is limited to `KubernetesServiceBootstrap`.
- Existing Kubernetes behavior is preserved: queueing, caching, backoff, audit records, metrics fallback, native port-forwarding, and secret redaction remain adapter-owned.
