# 0025 Native Session Alarm Evaluation

## Status

Accepted scope: the user-confirmed whole-session alarm contract. Implementation and release evidence remain incomplete.

## What Owns Alarm Evaluation And Persistence?

One native alarm owner evaluates cached snapshots from ResourceClient for every open session, independently of view filters. Immutable evaluation work runs off the UI thread and coalesces newer snapshots. Results carry their originating session/generation; late work cannot populate another session. Radar actions resolve through filtered resource membership, and tab activation does not repeat already observed notifications.

Persist rule definitions and the mute/reduced-motion preferences in a private, versioned `alert-rules.json`, using the existing lock, optimistic conflict and atomic-save conventions. Invalid data must remain recoverable. Do not persist resource snapshots, Secret values, evaluation results or per-resource effect state in this document.

One deadline timer owns bounded effects and time-dependent evaluation. A lazily created Qt Multimedia player owns local preview/playback; mute stops playback and no hidden view owns a refresh loop. Bundled sounds retain their real source/license attribution. Regular expressions have bounded match/depth work and explicit failure feedback.

## Alternatives And Consequences

Evaluating only the visible filter set was rejected because filters could conceal operational alarms. Per-view timers were rejected because they duplicate lifecycle ownership and replay hidden work. A second configuration framework was unnecessary: existing private-profile conventions cover conflict-safe persistence.

The initial native catalog and view-entry semantics do not yet match every legacy capability. See [the alarm specification](../spec/alert-automation.md) and [test map](../spec/k3d-test-map.md) for unverified playback, imported/full-catalog sounds, view-entry semantics and visual/device coverage. This decision does not authorize dropping those features or declaring release parity.

## How is problem-state disagreement prevented?

The release retains the three actual C# built-ins: Problem color, Recent change color and Active view pulse. This confirmed product decision supersedes the four-default proposal in the alarm specification.

ResourceClient publishes one canonical cache projection for problem status. Dashboard, tables and alarm evaluation consume that projection; alarm evaluation no longer recalculates Event health or restart problem thresholds independently. The existing cache-expiry timer owns Warning Event recency and Pod startup-grace boundaries. Its next health deadline is retained per session, and deadline transitions publish cached rows without requesting Kubernetes data.

Canonical cache problem state also owns warning versus severe-failure classification.
Radar consumes that classification for yellow/red highlights; a recent-change
effect uses green. This avoids treating every problem as a severe failure.
Following the user decision on 2026-10-05, a newly triggered focus action targets
the most recently changed matching resource, with severity/path tie-breaks, rather
than the first unchanged match. Existing rule enablement and filtered presentation
remain authoritative; an unchanged refresh never replays the action.

Warning Event health uses the latest available Event timestamp with creation-time fallback, rejects negative age, and expires after 30 minutes. Pods receive a five-minute startup grace for Pending/readiness gaps, but explicit failures remain failures. Completed resources are healthy. Running core Pod restart populations determine the rounded-up IQR threshold with a floor of three; failed Pods cannot dilute that population. Request completion, cancellation and authentication behavior remain unchanged.

## How Do Built-In Descriptions Remain Useful And Compatible?

The three locked definitions describe their actual effect: yellow/red problem
color, the user-confirmed green freshness highlight, and bounded view-entry
pulsing. The older C# description mentioning cyan does not override the accepted
green behavior. Reading an existing native version-1 catalog replaces only the
known generic built-in description in memory. Every other locked field is still
validated, and custom rules and enabled flags are retained. Reading never rewrites
the file; an explicit successful save publishes the canonical descriptions using
the existing lock, conflict check and atomic writer.
