# Roadmap

Podlord is built around a flat, cache-first Kubernetes workspace. The confirmed implementation direction is a C++/Qt Quick rewrite for desktop and mobile, with continuous comparison against the existing application. The [operational specification](spec/podlord-operational-spec.md) owns behavior; [ADR 0015](adr/0015-cpp-rewrite-for-performance-and-device-coverage.md) owns rewrite rationale; the [existing test map](spec/k3d-test-map.md) owns comparison scenarios and evidence.

## C++ Rewrite Sequence

The interview is complete. The steps below are confirmed sequencing, not claims that implementation or verification has run. Each implemented increment must carry real behavior and its tests; unfinished capabilities must remain explicitly unavailable rather than simulated.

| Step | Outcome and requirement references | Dependencies | Exit evidence | Status |
| --- | --- | --- | --- | --- |
| 1 | Preserve the reproducible old baseline and reconcile shipped capabilities, accepted scope, and documentation; RWT-009, RWT-012, RWT-014. | Current application, existing docs and test map. | Recorded baseline/artifact identity and build/run instructions; every inventoried function has a requirement/disposition and visible gap. | Offline macOS arm64 Release rebuild passed after adding two captured runtime packages (50 total). Separately identified comparison bundle launched with an empty profile; settings, sources, diagnostics, and zero-request state observed. [Capability inventory](spec/legacy-capability-inventory.md) records evidence and the initial launch-isolation failure. Auth/forward isolation, installed-artifact correspondence, runtime routes, and complete mapping remain pending. |
| 2 | Establish independently runnable old/new lanes and reusable public-boundary comparison; RWT-010, RWT-011, RWT-013. | Step 1; chosen Qt modules/toolchain and applicable distribution obligations. | Both apps launch separately, local state and ports are isolated, and equivalent read/mutation scenarios cannot contaminate each other. | Populated local-cluster desktop comparison executed with separate profiles; 54 side-by-side pairs retained. Complete mutation/auth/forward isolation and equivalent-scenario coverage remain pending. |
| 3 | Deliver the C++/Qt Quick session-to-resource workflow with real Kubernetes/authentication/cache flow; SES, SYN, ERR, TBL, FLT, STR, RWT-006. | Step 2; required baseline scenarios and resolved dependent scope details. | Usable end-to-end slice, public regression tests, auth and cleanup behavior, and repeated cached-interaction measurements. | Native flow and real Kubernetes checks exist. Full table/filter/restoration coverage and actual UI performance remain pending. |
| 4 | Complete required inspector, YAML/apply/delete, logs, forwards, metrics, radar/alerts, Settings, and accessibility behavior; INS, SEC, ACT, LOG, MET, ACC plus inventoried functions. | Step 3; each area's comparison scenarios. | Each required capability has old/new results, spec conformance evidence, and explicit approved differences rather than silent omissions. | In progress. Protected Apply, logs and appearance have scoped evidence; the populated comparison exposed missing surfaces. RWT-016/017 prohibit treating this partial state as parity. |
| 5 | Establish supported-target releases, settings/session migration and rollback, performance and size acceptance; STR, PER, RWT-004, RWT-008, RWT-015. | Required-function parity; actual target/toolchain availability. | Real install/run evidence per claimed target, migration and rollback checks, release-size measurements, performance profiles, and coverage gates. | Planned. |
| 6 | Retire the old production runtime while retaining the isolated comparison reference; RWT-007, RWT-009, RWT-015. | Step 5; no unresolved required-function or migration gaps. | New release has no .NET/browser dependency, removed production code is not left as dead scaffolding, and the recorded old reference remains reproducibly usable. | Blocked by prior exit evidence. |

Direct dual-application comparison uses a host actually supported by the old app. Mobile and other new targets require the same applicable specification scenarios, not a fabricated old mobile implementation. Reuse existing tests and ordinary build/run tooling where possible; do not create a custom comparison framework merely to wrap commands.

The recorded legacy reference and source-evidence limits are owned by the [capability inventory](spec/legacy-capability-inventory.md). Capturing sources and installed artifacts does not establish that they correspond or that the saved launch/build entrypoints have been exercised. The old global Docker image-pruning script is not an acceptable shared-comparison cleanup boundary.

Use real local test stacks for owned services. Fakes may be used only at external boundaries where needed. All task-owned ephemeral resources must be cleaned on success, failure, interruption, and partial setup; unrelated containers/images remain untouched. Record missing test availability as a gap rather than a pass.

Deferred items below are separate proposals, not automatic scope of the rewrite or evidence that a feature is shipped. Existing production behavior must first be inventoried before anything is deferred or removed. No further broad C# refactoring is part of this sequence.

## Deferred Audio Polish

- Record CC0 voice cues for `voice/radar-activated.ogg`, `voice/under-attack.ogg`, `voice/load-complete.ogg` (free TTS such as Piper, Coqui-TTS, macOS `say`).
- Source CC0 calm ambient or industrial loops for `music/calm/`. Energetic loops already shipped.

Empty roles play silent today and do not block the first OSS release.

## Workspace Views

### Network View

A proposed Network workspace could map Services, Endpoints, Ingresses, NetworkPolicies, Gateway/HTTPRoute, and pod-to-pod traffic into a topology that surfaces:

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

Automations build on top of the existing alert rule engine — they are the unified primitive for everything that reacts to cluster state.

## Sound Gamification

Make the cluster feel like a late-90s / early-2000s RTS using CC0 / royalty-free sounds and music. We mimic *style only* — no commercial game assets (Blizzard, EA/Westwood, etc.) ever ship in the repo.

Asset framework already in place at `src/Podlord.App/Assets/Audio/` with:

- Directory tree: `ui/`, `alerts/`, `events/`, `voice/`, `music/calm/`, `music/energetic/`.
- `CREDITS.md` table for every file (source URL, author, license).
- `MANIFEST.json` mapping semantic roles (`ui.click`, `alert.incident`, `event.radar_activated`, etc.) to one or more files. Multiple files per role get shuffled.
- `manifest.schema.json` describing the manifest format.
- `scripts/audio/README.md` sourcing guide.

### Planned Roles

| Role | Trigger |
| ---- | ------- |
| `ui.click` | Button / tab click |
| `ui.tab_switch` | Inspector tab switch |
| `ui.segment_ping` | Health bar segment changes state |
| `ui.hover` | Subtle hover beep on rare elements |
| `alert.incident` | New CRITICAL appears |
| `alert.warning` | New WARNING appears |
| `alert.recovery` | Critical → healthy |
| `alert.under_attack` | Many criticals at once |
| `event.startup` | App launch |
| `event.load_complete` | Initial resource load done |
| `event.radar_activated` | Screensaver → live radar |
| `event.session_switch` | Source switched |
| `voice.radar_activated` | Voice cue "Radar activated" |
| `voice.under_attack` | Voice cue "We are under attack" |
| `voice.load_complete` | Voice cue "Cluster online" |
| `music.calm` | Healthy cluster ambient loops, shuffled |
| `music.energetic` | Incident-active loops, shuffled |

### Default Catalog Sources

All listed in `Assets/Audio/CREDITS.md`. Confirmed-free catalogs:

- kenney.nl (CC0)
- freesound.org filtered to CC0
- pixabay.com (Pixabay license)
- opengameart.org filtered to CC0
- incompetech.com (CC BY, attribution required)
- fesliyanstudios.com (own free-commercial license)

Voice cues generated locally via Piper TTS (MIT) or espeak-ng (retro RTS HUD timbre). Output is not GPL-encumbered.

### Engine Work

- Avalonia audio playback layer (LibVLCSharp or OpenAL via Silk.NET).
- Manifest loader + runtime role dispatcher.
- Settings UI:
  - Per-category volume slider (UI / alert / voice / music).
  - Per-role enable/disable + sound picker.
  - Master mute.
  - Playlist preview.
- Music engine:
  - Cross-fade between calm and energetic when health state changes.
  - Shuffled playlist with no-immediate-repeat.
- Asset packs as ZIP bundles users can drop into a config directory; auto-merged into the manifest.
- In-app credits screen surfaces CC BY attributions.

### Forbidden Sources

Do NOT add files extracted from Blizzard, EA/Westwood, or any commercial game. Style only.

## Derived Issue Reasoning

Add a generic diagnosis layer so Podlord explains likely root cause instead of stopping at `Failed`, `NotReady`, or `Unknown`.

UI contract:

- Keep the raw Kubernetes status field.
- Add a derived reason on the next line in the inspector message/overview area.
- Show evidence lines when the user opens the inspector.
- Prefer "likely" and "evidence says" over fake certainty.

Cheap checks first:

- resource `status.reason` and `status.message`
- conditions reason/message
- container waiting/terminated reasons
- recent related Events
- owner health (`Deployment`, `ReplicaSet`, `StatefulSet`, `DaemonSet`, `Job`)
- related Node health and taints
- desired vs ready/available counts
- age, restart count, and startup grace window

Planned generic classifiers:

- stale failed pod after node shutdown
- pod evicted by node pressure
- pod stuck pulling image
- pod failing readiness/liveness/startup probe
- pod crash loop / repeated exit
- pod unschedulable
- node unreachable / kubelet stopped posting status
- stale node still registered after replacement
- controller below desired replicas
- controller rollout stuck
- job failed / backoff exhausted
- cronjob missing recent successful run
- pvc pending / waiting for provisioner
- volume attach or mount failure
- service without ready endpoints
- ingress / gateway / route missing backend or listener acceptance
- secret/config reference missing
- RBAC-forbidden visibility masquerading as emptiness

Resource coverage plan:

- `Pod`: termination reason, waiting reason, probe failure, image pull, eviction, node shutdown, owner healthy but stale failed pod.
- `Node`: `Ready` condition, heartbeat age, taints, stale registration, only-daemonset residue.
- `Deployment` / `ReplicaSet` / `StatefulSet` / `DaemonSet`: desired vs ready, progressing deadline, stuck rollout, stale failed children, unavailable replicas.
- `Job` / `CronJob`: failed pods, backoff limit reached, missed schedule, no recent success.
- `PVC` / `PV`: pending bind, lost volume, resize pending, attach/mount errors from Events.
- `Service`: selector matches nothing, endpoints empty, port mismatch when detectable.
- `Ingress` / `Gateway` / `HTTPRoute`: not accepted, no address, backend missing, listener/parent unresolved.
- `ConfigMap` / `Secret`: missing referenced keys or objects where reference graph is known.
- `Event`: collapse noisy raw events into a short problem summary instead of repeating log spam verbatim.

Delivery shape:

- start with cheap, local, cache-backed inference
- attach confidence and evidence to every derived reason
- never require provider-specific logic for first-pass diagnosis
- allow later provider plug-ins, but keep the default engine Kubernetes-generic

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

## Native Session Runtime Progress

The first independent C++ runtime slice implements persistent session lifecycle and configuration copies through a real CLI, not the replacement GUI. [ADR 0016](adr/0016-native-session-persistence-boundary.md) records its boundary; [the native test map](spec/k3d-test-map.md#native-session-runtime-verification-2026-10-02) owns executed checks, coverage, benchmark results, and remaining gaps. Next: kubeconfig/context ownership and the read-only Kubernetes data flow, followed by Qt Quick integration. The migration as a whole remains in progress; no legacy code is retired on the strength of this component result.

Session-core correction: the first native catalog diverged from SES-005-019 despite green component tests. Six failing contract regressions led to schema 2 with per-sequence suffixes and actual usage timestamps, a separate 30-day-ranked selection entrypoint, and unchanged catalog/tab order. The current 135-test result and remaining product-boundary gaps are recorded in the test map. This is not Qt Quick parity or migration completion.

Native file import/listing is now executable independently of .NET. Owned source snapshots retain original bytes and source identity, deduplicate repeated imports, preserve old active-session bindings, and isolate damaged records. This closes the file-import metadata/persistence slice only. Effective credentials and relative paths, source watchers and remaining import channels, Qt Quick integration, Kubernetes flows, migration/rollback and platform evidence remain required. Storage rationale is recorded in ADR 0017; executable results remain in the test map.

The source/session CLI workflow now also proves repeat-change idempotency, UTC/DST-stable ranking, partial source-record failure isolation, directory-access revocation and shared response-delivery behavior. Final results are in the test map. The next required vertical increment is native connection resolution and cache-backed Qt Quick integration, with the remaining authentication/source channels and local Kubernetes E2E gates still explicit.

## Native Read Workspace Progress

The independent Qt Quick development application now exposes the real file-import, session-open/close, Kubernetes discovery/list/filter/sort and metadata-inspector workflow. Static credentials and TLS are resolved from owned source snapshots; unsupported interactive/provider/proxy/impersonation configurations are explicit failures, not alternate identities. [ADR 0018](adr/0018-native-read-workspace.md) records ownership and remaining boundaries.

The latest native suite has 260 passing cases, including actual QML mouse/keyboard interactions and application-argument checks. Real local Kubernetes TLS/client-certificate execution is separately tracked in the test map. The 90% branch gate is currently failing after adding the new runtime; passing scenario counts are not release readiness. No legacy capability-ledger row is declared fully migrated.

Next: close public-workflow and connection/failure coverage gaps, then central synchronization/discovery freshness/request settings and startup cache lifecycle reconciliation. Startup restoration does not authorize a new resource disk cache or persisted credentials. Remaining authentication/source channels, complete inspector/YAML/actions/logs/forwarding, table layouts, filter/radar/alerts/settings, actual dual-lane parity, migration/rollback, performance and platform/distribution evidence remain required. This replaces the earlier statement that no native GUI or Kubernetes connection exists; it does not retire the preserved legacy lane.

## Native Authentication Progress

The native workspace now supports confirmed exec credential processes, versioned token/certificate results, shared-tab login ownership, expiry and HTTP 401 suspension, cancellation and process cleanup. [ADR 0019](adr/0019-confirmed-exec-authentication.md) records its authority boundary and explicit remaining compatibility gaps. Restored sessions never silently invoke login. A local real Kubernetes workflow now also checks confirmed exec client-certificate authentication through QML, not just the static credential path.

That authentication increment passed 287/287 deterministic cases with 96.00% production lines and 80.18% branches. It did not satisfy the branch gate or complete a legacy capability.

## Native Synchronization Progress

The native read workspace now owns central visible/inactive-session synchronization, inspector-prioritized reads, minimum request spacing and a persistent configurable request ceiling. Private settings use validation, locking and compare-before-save. Cache expiry removes retained data and updates visible snapshots; unchanged responses do not republish the table. Plain-text rendering and escaped shared tooltips prevent resource markup from loading remote images. [ADR 0020](adr/0020-native-sync-cache-ownership.md) defines the ownership and limits.

The latest deterministic run passes 449/449 cases with two test jobs, 96.73% production lines and 80.18% branches. An earlier four-job run passed, but a later four-job run and a focused serial run had unexplained intermittent failures; these remain a test-reliability gap, not a proved concurrency constraint. Native Pod logs retain their shared queue, chronological bounded history, default multi-container selection and per-UID ownership. [ADR 0022](adr/0022-native-resource-metadata-authority.md) records metadata authority; [ADR 0023](adr/0023-native-inspector-document-boundary.md) records cached YAML/values and individual Secret reveal/copy. [ADR 0008](adr/0008-protect-yaml-editing-and-apply.md) now also records protected native draft ownership: a successful current-scope GET permits editing, refresh preserves the draft and reports version changes, and resource/session/tab/window transitions use a target-bound discard guard. Thirty additional public-QML cases cover this local editing boundary. Current-build real Kubernetes draft/refresh/Stay/Discard, YAML/values/Secrets, confirmed-exec credentials and multi-container logs passed with scoped container/volume cleanup and visually inspected evidence. Apply, diff/confirmation, conflict recovery and uncertain-write handling are not implemented. The 90% branch gate still fails. The [test map](spec/k3d-test-map.md) owns exact commands and gaps. Discovery reconciliation, remaining settings/auth/source channels, multi-window request budgets, operational actions, legacy parity, migration/rollback and actual UI/performance/platform/release gates remain open. The full migration remains active; no legacy capability is declared fully migrated.

## Native Protected Apply Progress

The native application now has an end-to-end YAML preparation, captured-target
diff, explicit confirmation, atomic UID/version guarded JSON Patch, fresh read-back
and deliberate three-way reconciliation workflow. The existing transport remains
the sole request owner. Uncertain writes preserve the draft and never auto-retry;
original Secret bytes survive unrelated changes. Local YAML work is off the UI
thread, and overlapping choices use a virtualized native list. The configurable
pre-send document/patch limit defaults to 3 MiB. [ADR 0008](adr/0008-protect-yaml-editing-and-apply.md)
owns the decision; [the test map](spec/k3d-test-map.md#what-does-native-protected-apply-currently-prove)
owns actual executed outcomes and open gates. This supersedes the earlier statement
that native Apply, diff, confirmation and conflict recovery are unimplemented.

User-entered Secret values may remain visible in the local draft; loaded values,
comparisons and previews remain masked. Write-only Secret input is normalized
once to canonical data before guarded patching and read-back.
Complete operational actions/deletion, the remaining inspector surfaces,
port-forwards, imports/source management, table configuration, filter/radar/alerts,
settings, measurements, legacy capability-by-capability comparison, migration and
rollback, accessibility/performance and platform/package evidence remain required.
The preserved legacy reference remains available. Neither this functional package
nor a green component suite completes the C++ migration or establishes release readiness.

The protected Apply real-cluster lane now passes: native ConfigMap writes and
concurrent-change reconciliation, new user-entered Secret values with independent
verification and remasking, and byte-identical preservation of original Secret
data after test-value removal. Owned containers and volumes are cleaned up. The
accepted draft-visibility rule is SEC-009; the independent 3 MiB local limit is
INS-029. This closes those specific workflow evidence gaps, not the remaining
branch, UI/accessibility, legacy, portability or release gates.

## Which independently evidenced functions follow the shared native shell?

The reference-shaped Resources/Events/Ports/Settings shell now uses one persistent
Radar/filter sidebar, a narrow-window drawer and five cache-derived header cards.
The Ports page implements real task search, endpoint copying, inspection and
stopping. See [ADR 0033](adr/0033-native-reference-shell-and-cache-summary.md) and
[the test map](spec/k3d-test-map.md) for results and known comparison limitations.
This is an implementation increment, not retirement of the reference.

The 2026-10-05 sidebar increment implements exclusive Problems/Activity modes
and native named presets, including per-session filters and restart restoration.
Collection loading establishes a silent alert baseline and a bottom-up health
work indicator; independent reads overlap in four globally rate-limited slots.
[ADR 0034](adr/0034-silent-sync-baseline-and-bounded-reads.md) and the
[test map](spec/k3d-test-map.md) distinguish executed evidence from remaining gaps.

The capability inventory remains the function-by-function authority. Complete the
following increments in order; each closes its declared controls, failure paths,
public tests and paired presentation evidence before the next UI redesign. A
passing component suite is not a completed view. Keep the current working branch
and the runnable C# reference; do not start another parallel migration.

| Order | Complete increment | Acceptance / current disposition |
| --- | --- | --- |
| 1 | Empty-state branding, global search navigation, approved intensity removal | Restore the actual logo; wrap Previous/Next/Enter/Shift+Enter over cached Resources/Events without requests; retain old profiles and all palettes/variants. Implemented on 2026-10-08; scoped evidence belongs in the test map, not an all-view parity claim. |
| 2 | Settings navigation and Alerts | Reduce always-visible chrome. Compare every reference rule column and editor field, criteria groups, live preview, default-rule locks, sound catalog/import/audition, enabled/mute/motion, validation and persistence. Complete empty/populated/edit/error/keyboard/narrow states without dropping actions. |
| 3 | Sources | Reference-shaped readable source/context inventory, import channels, aliases, refresh/recovery, source/session configuration and confirmed removal. Preserve original files, canonical identities, snapshots, permissions and sessions; pair populated/narrow/error screens. |
| 4 | About | Product branding/version, reference project/support links, update/download ownership and offline licenses/attributions. Do not offer C# update assets to the C++ app. Verify real destinations and native presentation; no invented release data. |
| 5 | Diagnostics | Real process runtime measurements and timestamps with explicit unavailable states, cache/request telemetry and reference table tools. Refresh only the visible section at bounded cadence or explicitly; no hidden timer and no cluster request from diagnostics rendering. |
| 6 | Remaining Settings sections | Appearance, Graphics, Sync, Privacy and Workspace: each reference control, help, validation, unsaved inputs, atomic persistence and failure feedback. Theme intensity is the only newly approved removal. Close translations and keyboard/contrast/narrow presentation, not just selectors. |
| 7 | Filters and all table surfaces | Complete row limit and reference preset/field behavior; preserve actual-measurement semantics. Resources, Events, Ports, Alerts, diagnostics and inspector tables require consistent sorting, columns, copy, hover, match navigation, scroll retention and responsive targets. |
| 8 | Inspector and embedded terminal | Compare every page and eligible action, cache/freshness/edit/Secret boundaries, logs/container selection, guidance/quick fixes and forwards. Prove interactive PTY/ANSI input, resize, exit, reconnect/error and teardown through the real container workflow; pair rendered terminal screens. |
| 9 | Radar, sessions, tabs and windows | Same reference projection/pose for deterministic topology evidence, water/background, filtered selection, issue/fresh highlights after initial load, stable health baseline, camera/navigation. Complete detach/move/close/restart with one session placement and correct forward ownership. |
| 10 | Release evidence | Full required function/view/theme comparison matrix, actual mobile/platform targets, accessibility and packaged startup. Run the final complete suite, unique behavior accounting, coverage and repeatable public-boundary CPU/memory/latency gates; report failures honestly. Private unsigned distribution remains allowed; signing/notarization is deferred by the user. |

On 2026-10-08, row 2 gained typed boolean/color/hold controls and compact Settings
navigation; row 4 gained reference branding and the missing Liberapay link;
row 5 gained actual OS runtime counters, observation time and the shared
runtime/audit table toolbox. Those are completed implementation increments,
not completed rows: imported alert sounds/remaining action semantics, Sources,
native updates, cache-byte telemetry, translations and full image/device evidence
remain open. Executed regressions, the full-run failure/recovery and the exact
private package are recorded in the existing test map.

For each row, use the existing capability inventory and test map to record:
reference action, native public entrypoint, implementation status, exact executed
scenario, original paired image and remaining gap. Update a row when evidence
changes. Test counts, palette equality and headless frames must never substitute
for whole-view or real-device evidence.

## Which RTS Experience Improvements Are Proposed After The Parity Audit?

These are review recommendations, not newly accepted release requirements.
Missing shipped capabilities remain owned by the capability inventory; optional
ideas must not delay or redefine that parity contract.

| Priority | Proposal | Acceptance and boundary |
| --- | --- | --- |
| 1 | Stable command-center layout: compact top status strip, resource table, right radar, inspector; reuse the C# hierarchy. | Preserve row/scroll/focus during refresh. Keep essential controls visible, remove redundant search/reload chrome, and provide consistent resize behavior. |
| 2 | Restore the complete filter/preset workflow instead of sending each field through a separate management dialog. | Chips show actual active expressions, mode and result/total count; reset is obvious. Age/UID/limit and saved-filter migration must work before parity is claimed. Filtering remains cache-only. |
| 3 | Make radar a useful tactical map, not the only source of state. | Legend/text accompany green fresh, yellow issue and red error. Manual pan/zoom/focused typing temporarily suppress camera interruption. Latest-issue focus resumes deliberately; no idle screensaver. |
| 4 | Distinguish sync progress, data age, authentication and cluster health. | Slowly rising initial load display reaches complete without replaying fresh/issue alerts. Counts, stale badge and login button explain incomplete/forbidden data. Color alone never carries state. |
| 5 | One applicable table toolbox, not several inconsistent lists. | Resources, Events, Ports, Alerts, diagnostics and inspector tables share observable sort/column/copy/hover/keyboard semantics without a new generic widget framework. Preserve identities and scroll anchors. |
| 6 | Readable restrained RTS styling. | Keep the distinctive theme, but reserve gold borders/glow for hierarchy and events. Use readable body/table text, meaningful icons with accessible names, visible focus and reduced motion. Validate every palette on populated narrow/wide views, not palette values alone. |
| 7 | Faster inspection with context-specific actions. | Cached identity, freshness and next action appear immediately; Fresh YAML unlocks editing only after foreground refresh. Common logs/values/links actions use native controls without rebuilding hidden surfaces. |
| 8 | Explainable issue guidance in the inspector and Problems table. | A small typed C++ rule catalog consumes existing raw conditions/container reasons/recent Events/owner context. Each finding names evidence, timestamp, likely cause and safe next diagnostic step; contradictory/missing/stale evidence is explicit. No new requests from hover/paint. |
| 9 | Guided quick fixes through the existing YAML safety path. | Offer a draft only for a narrowly evidenced change, identify workload owner, require fresh data, show Secret-masked diff and expected effect, then explicit confirmation/RBAC/error/readback handling. Never auto-delete/restart Pods or grant permissions. |
| 10 | Native embedded container terminal. | Explicit Pod/container/command choice; direct Kubernetes exec transport without kubectl or WebEngine; session-owned teardown, resize/TTY/exit handling, bounded scrollback and hostile terminal-output tests. RBAC denial remains denial. A shell-less image must not silently create a debug container or change privilege. |

Suggested diagnostic cases are image pull failures, repeated crashes, OOM
termination, failed startup/liveness/readiness probes, Pending scheduling/PVC
issues, missing referenced Secret/ConfigMap and permission/auth failures.
`CrashLoopBackOff` identifies backoff, not its underlying cause. Guidance must
separate observed facts from hypotheses. Start with evidence links, previous
container logs and workload/node context; automated remediation is not an
extension of a read-only hint.

Kubernetes documents per-container exec and explains why crashed or shell-less
images can require a separate ephemeral debugging container:
[Debug Running Pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-running-pod/).
Resource/subresource permissions remain explicit:
[RBAC authorization](https://kubernetes.io/docs/reference/access-authn-authz/rbac/).
Probe guidance must distinguish readiness from restart-triggering liveness:
[Configure probes](https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-probes/).
No arbitrary user scripts, generic automation engine, browser terminal, hidden
cluster writes or additional monitoring provider are implied by these ideas.

### Welche Verbesserungen sind jetzt freigegebener Implementierungsumfang?

2026-10-05: Die aufgeführten RTS-UI-/UX-Verbesserungen sind zur schrittweisen Umsetzung freigegeben, einschließlich eines nativen Container-Terminals, cachebasierter Fehlererklärung und geführter Quick Fixes. Diagnose bleibt lesend und aus dem Snapshot; mutierende Quick Fixes durchlaufen die bestehenden YAML-Diff-, Frische-, Sicherheits- und Bestätigungsgrenzen. Es gibt keine automatische Clusterreparatur und keine aus einer bloßen Empfehlung abgeleitete Schreibberechtigung. Die Release-Freigabe bleibt an belegte Funktions-/Darstellungsparität und die dokumentierten Gates gebunden.
