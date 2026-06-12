# Podlord Product Brief

Podlord is a standalone desktop Kubernetes control center built with C#/.NET and Avalonia UI. It replaces namespace-first Kubernetes GUIs with a flat, real-time, multi-context, IDE-like workspace.

Tagline:

> Podlord: Rule your clusters before they rule you.

## Product Principle

Podlord must never hide the battlefield behind a namespace tree.

The user should always know:

- where they are
- what is alive
- what changed
- what is stale
- which context each action controls
- how to inspect one thing without losing the bigger picture

## UX Pillars

1. Flat by default: all namespaces are visible until filtered.
2. Context clarity: every view, tab, log stream, editor, and action shows cluster, context, namespace scope, and freshness.
3. IDE-style multitasking: tabs, splits, bottom panels, pinned resources, logs, and workspace restore.
4. Standalone session management: Podlord imports kubeconfigs into an app-owned model and never depends on the user's shell context.
5. Watch-first state: list-watch, object store, freshness metadata, batching, reconnects, and event history.
6. Tactical map: a serious 2D pixel-art operational map, not a toy dashboard.

## MVP Milestones

1. App shell and config
   - Avalonia native app shell
   - C# core command structure
   - Settings persistence
   - Import kubeconfig
   - Show contexts
   - Create Podlord sessions
   - Switch active session
   - Store session metadata

2. Kubernetes connection
   - Create Kubernetes client from app-managed config
   - List namespaces
   - List pods and deployments across all namespaces
   - Show flat resource explorer
   - Add search and filters

3. Watch engine
   - Watch supervisor
   - Object store
   - Real-time UI updates
   - Freshness states
   - Reconnect and stale handling

4. Resource focus
   - Persistent resource tabs
   - YAML, metadata, conditions, events, owner chain, freshness

5. Logs and actions
   - Pod logs
   - Follow logs
   - Bottom panel
   - Context-bound actions
   - Native port forwarding

6. Tactical map
   - Nodes as bases
   - Pods as units
   - Deployments as factories
   - Services as relay structures
   - Pan, zoom, inspect, health visuals

7. Guardrails and polish
   - Production warnings
   - Dangerous command detection
   - Workspace restore
   - Saved filters
   - Error, loading, and empty states

## Operation Guardrails

The C# core and Kubernetes service layer own all sensitive operations:

- Context binding
- Kubeconfig isolation
- Apply confirmation requirements
- Secret redaction
- RBAC interpretation

Secrets are metadata-only by default. Raw secret values, kubeconfig tokens, and certificates are not logged or persisted unless a later explicit feature makes that safe.

## Architecture Modules

- `Podlord.Core`: domain, errors, time, kubeconfig import/store/export, sessions, settings, filters, health
- `Podlord.Kubernetes`: Kubernetes API adapter, discovery, future watch supervisor, object store, health, events, relationships, logs, port forwarding
- `Podlord.App`: Avalonia app shell, explorer, radar, inspector, logs, source/session management
- future `Podlord.Terminal`: PTY sessions, shell profiles, generated kubeconfig env, command safety
- future `Podlord.Workspace`: tabs, splits, pins, layouts

## Current Boundaries

The current app uses cache-first list/detail/log requests. Watch supervision and embedded terminal support are planned separately so the existing request queue and source model stay explicit.

Animated radar alerts belong in the rule system, where users can enable, disable, and customize them.
