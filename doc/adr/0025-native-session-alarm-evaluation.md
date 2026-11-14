# 0025 Native Session Alarm Evaluation

## Status

Accepted scope: the user-confirmed whole-session alarm contract. Implementation and release evidence remain incomplete.

## What Owns Alarm Evaluation And Persistence?

One native alarm owner evaluates cached snapshots from ResourceClient for every open session, independently of view filters. Immutable evaluation work runs off the UI thread and coalesces newer snapshots. Results carry their originating session/generation; late work cannot populate another session. Radar actions resolve through filtered resource membership, and tab activation does not repeat already observed notifications.

Persist rule definitions and the mute/reduced-motion preferences in a private, versioned `alert-rules.json`, using the existing lock, optimistic conflict and atomic-save conventions. Invalid data must remain recoverable. Do not persist resource snapshots, Secret values, evaluation results or per-resource effect state in this document.

One deadline timer owns bounded effects and time-dependent evaluation. A lazily created Qt Multimedia player owns local preview/playback; mute stops playback and no hidden view owns a refresh loop. The original native audio pack replaces third-party clips. Dependency license notices remain. Regular expressions have bounded match/depth work and explicit failure feedback.

Pure visibility changes reuse the existing rule-match identities while their
resource snapshot, catalog and time conditions remain valid. Two in-memory time
bounds describe that validity. Resource, catalog and deadline work invalidate it
at the existing queue owner. Backward clock movement or an elapsed condition
deadline requires fresh evaluation. Age rules remain uncached because their
numeric population changes continuously. Reuse remains off the UI thread and
passes through the same generation-checked effect projection, preserving
view-entry holds, whole-session counts and notification deduplication. No second
resource dataset or persisted evaluation cache is introduced.

Numeric population statistics are built only for enabled percentile/outlier
criteria. Stateless effects traverse their matching rows, while bounded holds
still account for unmatched resources until expiry. Functional test runs honor
`PODLORD_DISABLE_AUDIO` at the player boundary without suppressing visual effects
or notification signals. Actual playback remains a separate device check.

The audio checks separate codec coverage from device lifecycle coverage. Every
embedded asset decodes completely to valid PCM with no audio output object.
Three muted device scenarios exercise mono repeat, stereo repeat and a source
switch on one player. Each first requires a separate zero-volume PCM sink to
open and process audio on the selected output, which remains pinned during the
player scenario. Missing or changed outputs fail. This proves separate-sink
progress, not observation of QMediaPlayer's private sink or human audibility.
Device failures still fail, and the existing watchdogs remain unchanged. This
replaces 116 serialized device checks rather than adding a second full-device
matrix. It reduces audio-device disruption. Confirmed on 2026-11-02.

The current playback boundary accepts catalog IDs, not media paths or URLs.
The catalog resolves approved asset IDs only to embedded `qrc:` audio.
Rule decoding rejects unknown IDs and extra fields; preview also resolves an ID
through that catalog before creating a player. Attribution source URLs are not
playback URLs. This matters when reviewing FFmpeg version-based vulnerability
matches: untrusted Kubernetes data or rule JSON cannot choose decoder input in
the current implementation. It is not blanket decoder clearance. The specified
future user-sound import needs its own format/size/retention decision and renewed
decoder review before accepting untrusted media. That capability remains open.

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
green behavior. Following CTX-006, confirmed on 2026-11-14, only the current native
version-2 catalog is accepted. The obsolete version-1 reader and generic-description
normalization are removed. Locked definitions remain validated, custom rules and
enabled flags are retained, and unsupported files remain unchanged. The existing
lock, conflict check and atomic writer protect explicit saves.

## How Are Original Music And Legacy Cue Choices Owned?

Confirmed on 2026-11-10: 20 original cues and 20 independently composed instrumental pieces replace the third-party audio catalog. One shared runtime owns music across windows and tabs. It has no persisted playback intent, automatic startup or network source. The user explicitly requested background playback. Android uses a private media-playback foreground service. iOS uses its native audio session and background audio capability. Qt Multimedia remains the decoder/player. OS interruption and output removal pause playback.

Known legacy sound IDs map by purpose during rule decoding. This does not change enablement or save on read. The existing explicit atomic writer publishes canonical IDs. Removed attribution UI and its obsolete tests are deleted rather than retained as empty compatibility controls. The frozen C# reference may retain its own assets, but the C++ package does not embed them.

On 2026-11-10 the user replaced the planned music chooser with one Play/Pause toggle and automatic shuffle. No manual track-selection UI ships. Each full cycle visits all 20 pieces once, and the boundary never repeats the previous piece. Music never auto-starts after launching the app.

## Why Can Android Music Receive Commands In The Background?

Android enables Qt's documented `android.app.background_running` metadata so the native event loop can advance tracks and receive Pause while the Activity is suspended. Cluster request ownership still observes application focus and stops passive foreground work. Rendering remains tied to exposed Qt Quick windows. Service and audio-focus Binder calls do not pass through Qt's Activity dispatch queue, which defers callbacks while the Activity is paused. The service owns its Android-main-thread media callbacks. No Qt fork is added. See [Qt manifest configuration](https://doc.qt.io/qt-6/android-manifest-file-configuration.html) and [Android media-playback service types](https://developer.android.com/develop/background-work/services/fgs/service-types#media).

Saving a migrated catalog writes the new cue IDs. An older native binary does not know those IDs and may reject the alert catalog while retaining it. The compatibility guarantee is old-catalog reading in the updated app, not forward compatibility in older binaries. No additional configuration copy or automatic rollback rewrite is introduced.
