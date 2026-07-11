# Podlord Operational Spec

## Purpose

Podlord is a desktop Kubernetes operations console for people who need to understand cluster state quickly without trusting shell context, stale views, or namespace-first navigation.

It turns Kubernetes from scattered YAML and terminal state into one cache-backed, multi-session control surface.

## Problems Solved

- Wrong-context risk: every view, tab, radar, action, and port forward is bound to an explicit source/session.
- Slow navigation: resources are shown across the selected scope first, then filtered down.
- Stale UI state: Kubernetes data is cached, refreshed in the background, and marked by sync state.
- Tool dependency: core resource loading, metrics, YAML, secrets, and port-forwarding use Kubernetes APIs instead of requiring `kubectl`.
- Operational noise: filters, alerts, radar colors, sounds, and animations surface important changes without alerting on every normal update.

## Core Capabilities

- Import kubeconfigs from files, folders, pasted YAML, default locations, and generated k3d sources.
- Store app-owned kubeconfig snapshots with content hashes to avoid duplicates.
- Detect changed kubeconfig files and update their stored snapshot.
- Open sessions in tabs or detached windows while sharing the same cache.
- Keep each session open only once across all tabs and windows.
- List Kubernetes resources in a flat, sortable, filterable table.
- Filter by kind, namespace, status, name, image, node, owner, age, restarts, CPU, memory, storage, problems, and activity.
- Inspect resources with overview data, YAML, events, links, logs, values, and actions.
- Reveal ConfigMap and Secret key/value data with secret values hidden by default.
- Edit and apply YAML through Kubernetes API paths.
- Start native Kubernetes port-forwards for supported running Pods and Services.
- Show live API request diagnostics, audit rows, cache telemetry, and process memory diagnostics.
- Render a deterministic radar map of resources and relationships.
- Apply rule-based alerts to radar, graph, and resource rows.
- Play local bundled alert sounds without opening a browser.

## Operational Model

Podlord is cache-first.

The UI reads local snapshots. Background sync fills and refreshes the cache through a rate-limited request queue. User-focused actions can request fresher data, but still respect throttling and backoff.

Each source/session has independent request telemetry and selection state. Switching sessions should be instant when cache exists, and visible when data is still loading.

## Safety Model

- Never silently acts on a different session than the visible one.
- Never depends on the user's current shell kube context.
- Does not mutate original kubeconfig files during import.
- Stores kubeconfig copies as app-managed snapshots.
- Hides Secret values by default.
- Uses explicit user actions for destructive operations.
- Keeps diagnostics useful without dumping sensitive kubeconfig content.

## Audit Trail: What Happens And Why

| Action | What Podlord Does | Why |
|---|---|---|
| Import kubeconfig | Validates, snapshots, hashes, deduplicates, creates sessions | Stable app-owned sources without changing user files |
| Source file changes | Updates the matching snapshot instead of creating duplicates | Keep sources current without list pollution |
| Select session | Restores cached rows, filters, radar state, and open tab state | Fast context switching without losing work |
| Refresh data | Queues Kubernetes API calls with pacing, TTL, and backoff | Avoid API spam while keeping data current |
| Display resources | Reads from cache and applies local filters immediately | UI stays responsive during sync |
| Open resource | Shows cached detail first, then requests fresh detail if needed | Immediate feedback with better accuracy shortly after |
| Open logs | Fetches logs only for log-capable resources and active log views | Avoid hidden background work |
| Port forward | Uses native Kubernetes port-forward stream handling | Cross-platform behavior without `kubectl` dependency |
| Trigger alert | Matches resource state, applies color/animation/sound/zoom rules | Make important changes visible without manual scanning |
| Switch tabs | Preserves alert state per session and avoids replaying old alerts | Context switches should not create false activity |
| Show diagnostics | Records request status, timing, queue state, cache size, and memory | Explain what the app is doing when users need proof |

## Non-Goals

- Not a namespace tree browser.
- Not a shell wrapper.
- Not a Grafana clone.
- Not a Helm or GitOps manager yet.
- Not a multi-user control plane.

## Product Principle

Podlord should make the operator feel:

- I know which cluster I am touching.
- I know what changed.
- I know what is unhealthy.
- I know what data is cached or loading.
- I can act faster without becoming more dangerous.
