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
