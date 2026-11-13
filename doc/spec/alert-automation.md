# Alert Automation

Podlord alerts are user-owned rules that use the matcher language already used by resource filters. They evaluate the entire originating session cache, not only current filter matches. Filters must not hide alarms. Radar presentation and alarm-triggered focus remain limited to the filtered resource set. Evaluation and actions must not add direct Kubernetes calls.

## Model

An alert is built from a guided top-to-bottom inspector flow:

| Section | Purpose |
|---|---|
| Name | Human-readable alert name. |
| Description | Short operator note explaining why the rule exists. |
| Matchers | One or more matcher blocks. Criteria inside a block are `AND`; blocks are `OR`. |
| Color | Optional radar block color with `none`, `once`, `no-match`, duration or `new-in-view` hold behavior. |
| Animation | Optional radar animation with the same hold modes. |
| Zoom | Optional radar zoom percentage when the rule fires. |
| Sound | Select one of 20 original bundled cues or silence. Existing choices migrate by purpose. User-file import is deferred beyond the first release. |

Default alerts are built in, enabled by default, and locked against editing. Users can enable or disable built-in alerts in the rules table and add custom alerts. The editor does not offer a duplicate action.

## Matcher Language

Alerts use the existing Podlord matcher behavior:

- strings: `pod`, `"exact"`, `~prefix`, `suffix~`, `/regex/`
- numbers: `5`, `=5`, `>5`, `<5`, `>=5`, `<=5`
- durations: `>5m`, `<1h`, `<=10s`
- stats: `outlier`, `p95`
- scopes: kind, namespace, name, status, issue, node, image, owner, CPU, memory, storage, restarts, age, event reason, event message, activity, problems

Rules evaluate cached rows only. The UI updates immediately when a rule changes; Kubernetes sync cadence remains controlled by the existing cache, request queue, TTL, and request-limit settings.

`once` holds an effect for 1350 ms. Duration holds use the configured seconds.
Neither hold is extended by updates while it is active; a finite hold can outlive
a lost match while its resource still exists. `newInView` is a boolean criterion,
not resource freshness. A `new-in-view` hold starts when a matching resource
enters the visible filtered table, ends on expiry or departure, and can start
again on re-entry. Establishing the initial visible membership is silent. These
view-entry effects do not restrict whole-session rule matching to filtered rows.

The first synchronization establishes a silent baseline: partial loading must not
emit alarms, freshness indicators, focus actions or sounds. After that initial
synchronization, background requests must not clear alarms or highlights from
the still-visible cache. Incoming snapshots update matches normally; starting
or completing a request alone must not replay an action. The editor's explicit
Radar preview remains unavailable while synchronization is in progress.

## Default Alerts

| Alert | Trigger | Action |
|---|---|---|
| Problem color | `problems=true` | status color, radar zoom, warning ping |
| Recent change color | `recentlyChanged=true` | fresh color |
| Active view pulse | `newInView=true` AND `activity=true` | bounded pulse |

The release catalog retains the three shipped C# definitions. This product decision, confirmed on 2026-10-04, replaces the earlier four-rule proposal; there is no additional default restart-spike or CPU-p95 rule. Restart outlier and CPU percentile expressions remain custom-rule capabilities. Built-ins stay enabled and locked by default.

Descriptions explain each action rather than showing a generic built-in marker:
problem resources receive yellow/red, recent changes receive the agreed green
freshness highlight, and active resources receive a brief entry pulse. Reading
an older profile containing exactly `Built-in desktop alert` substitutes only
that description in memory. Enabled state and custom rules remain unchanged;
all other locked fields remain validated. Reading does not rewrite the profile.
An explicit save persists the canonical descriptions through the existing
optimistic, atomic store.

The editor's explicit Radar preview uses the current draft and session cache,
including disabled drafts, without saving, enabling, fetching or playing sound.
It focuses a visible match or the first visible fallback, at least 100% zoom.
Invalid, loading, empty or stale-scope results cannot move the current camera.

## Sound Policy

The editor shows rule enablement in the rules table. Save and Delete belong to the editor. Auto-focus and sound reveal configuration only when enabled. The editor does not duplicate global mute or reduced-motion controls.

The release bundles 20 original short cues and 20 independently composed instrumental music tracks. No third-party samples, voices, copied melodies or downloaded audio are used. Each cue has a display name and purpose. Removed sound attribution and source-browser controls disappear. Qt, FFmpeg and other dependency notices remain.

Existing sound IDs resolve to appropriate original cues without changing rule identity, matcher, activation or mute preferences. Reading never rewrites configuration. The next explicit successful save persists the canonical sound choice through the existing conflict-safe atomic writer. Unknown IDs remain errors.

Music uses one application-owned player shared across tabs and windows. Play is explicit. It never resumes after application restart. It continues when minimized or another app is active. A single icon-only Play/Pause action lives in the main navigation. It uses that navigation's overflow menu on narrow screens. There is no track chooser, music menu or separate in-app music mute control. Tracks shuffle in complete 20-piece cycles, with no immediate repeat across cycle boundaries. Pause retains the current track and position. Native media focus/background lifetime remains active when advancing to the next piece. Playback interruption or output removal pauses music without automatic restart. Mobile wrappers own only OS audio focus, media controls and background lifetime. No download or external runtime is required.

The original score/renderer sources are maintained with the assets. The 20 pieces must differ in composition, rhythm, arrangement and instrumentation, not merely pitch, speed or distortion. Complete silent decoding, cue migration, controls, restart behavior, native player lifecycle and mobile background playback need executable evidence. Imported user sounds remain deferred.

## What Has The Native Implementation Proven?

Native rules persist in the private profile's `alert-rules.json`. A nonblocking file lock, compare-and-save conflict check, private permissions and atomic replacement protect user edits. Invalid, unsupported or conflicting documents are retained rather than silently replaced. Built-in definitions are locked except for enabled state; duplication creates a custom rule.

Evaluation consumes immutable session snapshots off the UI thread. Changes are coalesced, results remain associated with their originating session, and closing a session discards its pending presentation. Switching tabs does not replay known focus actions. One deadline timer owns expiry of finite effects and time-sensitive matches. Reduced motion suppresses animated radar effects; master mute suppresses playback, including preview. Regex matching has explicit resource limits and an observable evaluation error.

Public QML tests exercise CRUD, invalid input, locking, restart persistence, whole-session evaluation despite filters, grouped AND/OR criteria, numeric ranges, bounded effects, tab non-replay and regex exhaustion. A separate local Kubernetes UI run exercises a real CrashLoop Pod and filtered-out alarm matching. Exact scenarios and remaining coverage belong to [the test map](k3d-test-map.md).

The previous 117-entry C# audio-catalog comparison is superseded by the user-approved original pack. It remains useful only as a legacy-ID inventory, not a requirement to ship third-party audio or credits. New pack qualification is recorded in the test map.

Public lifecycle regressions now cover once/duration/view-entry effects, hidden
and repeated membership, rejected paths and session closure. Imported sounds,
complete camera/action semantics, audible physical-device comparison and full
theme/device/accessibility comparison remain open. These are release gaps, not
accepted removals; lifecycle and component tests do not establish whole-product
parity. The preserved C# sound catalog contains bundled sounds only: importing
user sounds is a specification capability, not an already shipped reference
implementation to copy.

On 2026-11-10 the user replaced the planned music chooser with one Play/Pause toggle and automatic shuffle. No manual track-selection UI ships. Each full cycle visits all 20 pieces once, and the boundary never repeats the previous piece. Music never auto-starts after launching the app.

When the radar button is hidden, the same Play/Pause action follows the existing responsive navigation rule: show an icon in the header when it fits, otherwise use the ordinary navigation overflow. There is no dedicated music menu.
