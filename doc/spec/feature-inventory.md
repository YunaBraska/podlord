# Feature Inventory

Podlord is a resource-first Kubernetes desktop console. The UI favors global scanning, cache-first operations, explicit source selection, and focused resource inspection.

## Implemented

| Area | Behavior |
|---|---|
| Kubeconfig sources | Import home config, custom file, directory scan, pasted YAML, and generated k3d config. |
| Source snapshots | Store app-owned kubeconfig copies by source path and content hash. |
| Resource explorer | Flat resource table with sorting, resizing, reordering, column visibility, filters, saved filters, and quick search. |
| Radar | Deterministic resource island, zoom, pan, selection, hover details, activity markers, and optional water animation. |
| Metrics | CPU/memory from `metrics.k8s.io` where available; namespace-scoped pod metric fallback when cluster-wide metrics are forbidden. |
| Inspector | Overview, YAML, events, links, logs for pods, values for ConfigMaps/Secrets, delete action, and port-forward action where supported. |
| YAML | Syntax highlighting, editing, reset, and server-side apply. |
| Logs | Tail logs for pods through the Kubernetes API. |
| Port forwarding | Native Kubernetes port-forward stream handling without `kubectl` for normal app use. |
| Request control | Cache-first snapshots, request queue, rate-limit backoff, audit log, idle sync controls, and optional hard request cap. |
| Testing | Unit tests, fake Kubernetes HTTP tests, and k3d integration tests with coverage gates. |

## Planned

| Area | Direction |
|---|---|
| Rule alerts | User-editable alert rules replacing hardcoded activity/problem behavior. |
| Watch engine | Watch-first resource updates with list fallback. |
| CRDs | Richer discovery and generic CRD table rendering. |
| Historical metrics | Optional Prometheus and kube-state-metrics sources. |
| Terminal | Context-bound embedded terminal with generated temporary kubeconfig. |
| Diff | Resource, namespace, source, and cluster comparison views. |
| Packaging | Signed macOS builds, Windows installer, Linux `.deb`, `.rpm`, and AppImage. |
