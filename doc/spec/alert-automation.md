# Alert Automation

Podlord alerts are user-owned rules that use the matcher language already used by resource filters. They evaluate the entire originating session cache, not only current filter matches. Filters must not hide alarms. Radar presentation and alarm-triggered focus remain limited to the filtered resource set. Evaluation and actions must not add direct Kubernetes calls.

## Model

An alert is built from a guided top-to-bottom inspector flow:

| Section | Purpose |
|---|---|
| Name | Human-readable alert name. |
| Description | Short operator note explaining why the rule exists. |
| Matchers | One or more matcher blocks. Criteria inside a block are `AND`; blocks are `OR`. |
| Color | Optional radar block color with `none`, `no-match`, or duration hold behavior. |
| Animation | Optional radar animation with `none`, `no-match`, or duration hold behavior. |
| Zoom | Optional radar zoom percentage when the rule fires. |
| Sound | Select a bundled or imported sound with visible author, license, and source link. |

Default alerts are built in, enabled by default, and locked against editing. Users can enable, disable, duplicate, or add custom alerts. Duplicating a built-in alert creates an editable custom copy.

## Matcher Language

Alerts use the existing Podlord matcher behavior:

- strings: `pod`, `"exact"`, `~prefix`, `suffix~`, `/regex/`
- numbers: `5`, `=5`, `>5`, `<5`, `>=5`, `<=5`
- durations: `>5m`, `<1h`, `<=10s`
- stats: `outlier`, `p95`
- scopes: kind, namespace, name, status, issue, node, image, owner, CPU, memory, storage, restarts, age, event reason, event message, activity, problems

Rules evaluate cached rows only. The UI updates immediately when a rule changes; Kubernetes sync cadence remains controlled by the existing cache, request queue, TTL, and request-limit settings.

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

Bundled sounds must be original, generated, or clearly royalty-free. Every sound choice must show:

- display name
- purpose
- author
- license
- source URL

No copyrighted game samples, faction voices, melodies, logos, or copied assets are allowed. Imported user sounds should keep their metadata beside the imported asset.

The built-in pack currently uses Kenney CC0 assets from UI Audio, Interface Sounds, Sci-fi Sounds, Music Jingles, Digital Audio, Impact Sounds and RPG Audio. The app stores local OGG copies and keeps purpose, source, author, and license visible in the alert editor.

## What Has The Native Implementation Proven?

Native rules persist in the private profile's `alert-rules.json`. A nonblocking file lock, compare-and-save conflict check, private permissions and atomic replacement protect user edits. Invalid, unsupported or conflicting documents are retained rather than silently replaced. Built-in definitions are locked except for enabled state; duplication creates a custom rule.

Evaluation consumes immutable session snapshots off the UI thread. Changes are coalesced, results remain associated with their originating session, and closing a session discards its pending presentation. Switching tabs does not replay known focus actions. One deadline timer owns expiry of finite effects and time-sensitive matches. Reduced motion suppresses animated radar effects; master mute suppresses playback, including preview. Regex matching has explicit resource limits and an observable evaluation error.

Public QML tests exercise CRUD, invalid input, locking, restart persistence, whole-session evaluation despite filters, grouped AND/OR criteria, numeric ranges, bounded effects, tab non-replay and regex exhaustion. A separate local Kubernetes UI run exercises a real CrashLoop Pod and filtered-out alarm matching. Exact scenarios and remaining coverage belong to [the test map](k3d-test-map.md).

The native catalog now preserves all 117 reference entries and embeds all 116 audio files. Its searchable chooser retains selection when results disappear; every ID has a public persistence check, and each actual embedded asset has native Cocoa/Darwin decoding and repeated-playback checks. The chosen sound exposes its source through an explicit external-browser action. Source-opening failure must remain visible, preserve the draft and allow only explicit retry. Searching, previewing and opening a source must neither save an alert implicitly nor contact Kubernetes. About credits group assets by author/license/source instead of creating one control per file.

Native parity is still incomplete: imported sounds, complete legacy view-entry/camera/once semantics, audible physical-device comparison, and full theme/device/accessibility comparison remain open. These are release gaps, not accepted removals; source-catalog and component tests do not establish whole-product parity.
