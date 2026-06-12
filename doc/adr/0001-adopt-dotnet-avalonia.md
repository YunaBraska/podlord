# 0001 - Adopt .NET And Avalonia

## Status

Accepted

## Context

Podlord needs a rich native desktop interface: sortable tables, split panels, source/session management, focus tabs, future terminal surfaces, and cross-platform packaging. The project also needs a testable core boundary so UI code cannot bypass context binding, kubeconfig isolation, secret redaction, or command safety.

## Decision

Use C# on .NET 10 with Avalonia UI.

The solution is split into:

- `Podlord.Core`: domain records, settings, kubeconfig import, session store, safety classification
- `Podlord.Kubernetes`: direct Kubernetes REST adapter for resource lists, detail views, event summaries, and pod log tails
- `Podlord.App`: Avalonia desktop shell and tactical command-center UI
- `tests/Podlord.Core.Tests`: behavior tests for import, store, sessions, safety, and bootstrap
- `tests/Podlord.Kubernetes.Tests`: k3d-backed Kubernetes integration tests for list/detail/log/error/RBAC behavior

The UI can request operations, but core services own the operational decision boundary.

## Consequences

Positive:

- One .NET solution builds the app, core, Kubernetes adapter, and tests.
- Avalonia gives stronger native desktop UI primitives for the product shape.
- Cross-runtime publishing is available through .NET runtime identifiers.
- Disposable k3d integration tests validate the highest useful public boundary against real Kubernetes behavior.

Trade-offs:

- Mobile/browser targets are outside the current desktop scope.
- The direct REST adapter must grow watch support, richer auth execution, and streaming logs.
- Formal UI automation and screenshot regression tests still need to be added.
