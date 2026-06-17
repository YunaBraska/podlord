# Roadmap

Podlord is built around a flat, cache-first Kubernetes workspace. The current focus is making the desktop app reliable, readable, and safe before adding larger orchestration features.

## Next: Rule-Based Alerts

The next major feature is a user-customizable alert system.

Goal: replace hardcoded activity/problem behavior with transparent rules that users can inspect, enable, disable, duplicate, and tune.

### Planned Capabilities

- Rule builder over resource fields, filters, status, events, metrics, namespace, cluster, labels, owners, and age.
- Built-in example rules matching today’s default behavior:
  - crash loop
  - image pull failure
  - pending too long
  - terminating too long
  - warning event burst
  - restart spike
  - node not ready
  - stale API data
- Per-rule severity: info, warning, error, critical.
- Per-rule output:
  - table/radar highlight
  - radar blink animation
  - custom radar marker style
  - optional sound
  - optional desktop notification
- Per-rule scope:
  - all sources
  - selected sources
  - namespaces
  - resource kinds
  - saved filters
- Rule presets that can be imported/exported as YAML or JSON.
- A default ruleset that can be reset after user edits.

### User Experience

Rules should be simple enough for non-experts:

```text
When Kind is Pod
and Status contains CrashLoopBackOff
then show warning animation
and play warning sound
```

Power users should still be able to express advanced cases:

```text
kind = Pod
namespace in payments,billing
restarts > 5
age < 1h
```

### Migration Plan

1. Add rule data model and persistence.
2. Convert existing activity/problem classification into built-in rules.
3. Keep current Problems and Activity switches as UI presets backed by rules.
4. Add rule editor.
5. Add radar animation and sound actions.
6. Add import/export.

## Workspace Views

### Network View

Add a Network workspace alongside the Graph view. Map Services, Endpoints, Ingresses, NetworkPolicies, Gateway/HTTPRoute, and pod-to-pod traffic into a topology that surfaces:

- Service → backing pods fan-out.
- Ingress/Gateway → Service → pods routing chains.
- Cross-namespace edges and NetworkPolicy restrictions.
- Live throughput when metrics sources expose it.

## Custom Automations

Promote the current built-in radar reactions (auto-zoom to next problem, deterministic colors, blink on alerts) into a user-configurable automation engine.

Surface: a rules editor where each rule binds a *trigger* to one or more *actions*.

### Triggers

- Resource status changes (Created, Updated, Deleted, Restart, CrashLoopBackOff, etc.).
- Metric thresholds crossed (CPU, memory, storage usage or limit-ratio).
- Event reasons or counts within a window.
- Filter membership (resource enters or leaves a saved filter).
- Custom field expressions (same DSL as saved filters).

### Actions

- Radar reactions:
  - color override
  - blink / pulse animation
  - zoom or pan radar to the resource
  - mark resource with custom glyph
- Inspector or table reactions:
  - highlight row
  - auto-focus the resource
- Sound reactions: play a chosen sample on trigger.
- Desktop notifications.
- Status line banner.
- Optional debounce, cooldown, and per-source scoping.

### Persistence

- Automations live in app settings; import/export as YAML/JSON shareable presets.
- A default automation pack ships with the same behaviors users see today.
- Reset-to-defaults at any time.

This subsumes the [Rule-Based Alerts](#next-rule-based-alerts) work — automations are the unified primitive.

## Sound Gamification

Make the cluster feel like a game using royalty-free sounds and music.

Scope:

- Bundle a default sound pack from CC0 / royalty-free sources (e.g., kenney.nl, freesound.org with CC0 tags) covering: alert, success, restart, deletion, deploy, ambient hum.
- Add a sound manager UI where each automation event can pick a sample.
- Allow users to download additional packs from a curated, free-only catalog.
- Optional ambient music loops gated by activity (calm when healthy, tense on incidents).
- Mute toggles per category and a global mute.
- All packs verified for redistribution rights before shipping.

## Near-Term Reliability

- Continue reducing unnecessary UI redraws.
- Add more UI regression tests around table layout, inspector sizing, and radar selection.
- Add screenshot regression coverage when the test infrastructure can run it reliably.
- Improve source manager deduplication and deletion workflows.

## Kubernetes Engine

- Add watch-based resource updates for supported resource kinds.
- Keep list/poll fallback for clusters where watches are not available or are forbidden.
- Add better CRD discovery and generic CRD tables.
- Add more metrics sources beyond `metrics.k8s.io`:
  - Prometheus
  - kube-state-metrics
  - cAdvisor/kubelet where available

## Packaging

- Add signed and notarized macOS releases.
- Add Windows installer packaging.
- Add Linux `.deb`, `.rpm`, and AppImage when the portable archives are stable.
