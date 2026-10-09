# Which Legacy Capabilities Must The Rewrite Account For?

## Captured Reference

Captured on 2026-10-02 at:

`/Users/yuna/.local/share/podlord-comparison/baselines/legacy-2026-10-02.ziS1vL`

The baseline owns its artifact and input identities in `baseline.json`, `SHA256SUMS`, `toolchain-sha256.tsv`, and `resolved-packages.tsv`. It contains the workspace source archive and extracted source, the separately captured installed macOS bundle/archive, the .NET SDK 10.0.301 with runtime 10.0.9, all 48 package archives represented in the captured resolved dependency inputs, and source indices. No production kubeconfig, user settings, or application cache was intentionally included.

The installed bundle reports version `2026.6.20`; the source declares `0.1.0`. Release packaging can override the declared source version, so these labels alone do not establish either equivalence or a mismatch. Source-to-installed-artifact correspondence is **unverified**. No rebuild, offline restore, application launch, or test execution was performed during capture.

The read-only source and bundle archives, rather than a mutable future build workspace, are the preserved reference inputs. The baseline is local and outside the production repository/release. Do not prune it as a temporary test artifact.

## Repeatable Entry Points

The baseline includes:

- `build-source-reference.sh`: copies the captured sources into a new private build workspace, uses the captured SDK and local package feed, and invokes the existing macOS build script with an explicit version and `osx-arm64` target. It does not run the test suite. Rebuild success and restored dependency equivalence remain unverified.
- `launch-installed-reference.sh`: directly launches the captured executable with separate `HOME`, `PODLORD_HOME`, and `PODLORD_CONFIG_HOME`. Update checking is disabled by default for the comparison lane and can be enabled deliberately for its own scenario. The launcher has not been exercised.

The installed artifact and a future rebuilt source artifact are distinct comparison identities until correspondence is established. Use only explicitly designated test kubeconfigs. Environment overrides do not prove isolation of an inherited `KUBECONFIG`, external credential-provider state, OS keychains, or browser sessions. Those boundaries and actual listening-port separation still require public launch/authentication checks before calling the lanes isolated.

The old `scripts/test.sh` contains global `docker image prune -f` and prefix-wide cleanup. It must not be run unchanged for shared comparison infrastructure: safe task-owned cleanup remains required by the accepted contract. The captured old code is not silently patched to conceal that risk.

## Source Evidence And Its Limits

The snapshot indices contain 187 event/status attribute occurrences, 543 binding occurrences, 1,292 public declaration occurrences, and 388 `[Fact]`/`[Theory]` source attributes. These are scan counts, not unique capabilities, executed tests, or proof of coverage.

The raw scanner's `distinct_ui_event_handlers` field reports 88 distinct attribute values, but eight are `IsChecked`-style status bindings, not callback names. Treat that field as an overinclusive raw count: 80 named callback candidates plus eight binding expressions. The frozen metadata is retained with this explicit erratum, not presented as 88 actual handlers.

- `ui-events.tsv`: literal XAML event routes and the overinclusive status-binding values noted above.
- `ui-bindings.tsv`: source binding paths for matching controls to their producers.
- `public-source-members.tsv`: supplementary public source declarations; public methods alone do not prove a reachable UI route.
- `source-testcases.tsv`: source locations and tentative method associations; unresolved associations and actual scenario coverage need reconciliation.
- `environment-inputs.tsv`: source-discovered environment inputs for isolation and configuration review.

Programmatically constructed context menus, keyboard routing, visibility/eligibility guards, dynamic inspector tabs, API discovery, timers, and failure branches are not exhaustively established by these indices. Their runtime/public-path review remains required. This inventory is a source-grounded seed, not a claim that every shipped capability has already been proven.

## Capability Ledger

This table captures C# reference routes. The dated native release assessment
below owns the current native disposition; historical routing notes are not
whole-product parity passes.

`Wired` means a matching literal XAML callback was found; it does not mean the callback was exercised. `Route pending` means an implementation/API was found but the complete public route needs evidence. `Catalog pending` means exact options, kinds, or subfeatures still need inventory. The old/new disposition is pending for every row until its owning contract and tests are reconciled. Requirement prefixes below reference the operational specification rather than define new requirements.

| Capability ID | Observed capability | Source/public-route evidence | Owning contract or intent gap | Evidence state |
| --- | --- | --- | --- | --- |
| LEG-001 | Startup path inputs and initial source loading | `App.axaml.cs` passes desktop arguments; `MainWindowViewModel.LoadStartupKubeconfigs`. | SES, STR; exact accepted argument handling. | Route pending. |
| LEG-002 | Import default/home kubeconfig | `MainWindowViewModel.ImportHome`; `AppState.ImportHomeKubeconfig` uses `PODLORD_HOME`. | SES; source import scenarios. | Route pending. |
| LEG-003 | Import selected paths, including the documented file/folder workflow | `ImportPathClicked`; `ImportPathNow`, `ImportPaths`, `KubeconfigImporter.ImportFile`. | SES; folder recursion/validation inventory. | Wired; catalog pending. |
| LEG-004 | Import pasted kubeconfig text | `MainWindowViewModel.ImportPasteNow`; `KubeconfigImporter.ImportText`. | SES; exact UI entry and validation. | Route pending. |
| LEG-005 | Import generated k3d configuration | `MainWindowViewModel.ImportK3dNowAsync`. | SES; external CLI/service boundary and cleanup. | Route pending. |
| LEG-006 | Refresh imported sources and preserve configuration snapshots | `RefreshSourcesNow`; snapshot-related `KubeconfigImporter` entrypoints. | SES; existing-source refresh UI and snapshot invariants. | Route pending. |
| LEG-007 | Edit/save/remove source metadata | `SaveSelectedSource`, `RemoveSource`; `RemoveSourceClicked`. | SES; edit/delete scope and references. | Removal wired; remaining routes pending. |
| LEG-008 | Edit/save/duplicate session configuration | `SaveSelectedSession`, `DuplicateSelectedSession`. | SES; exact editable fields and duplicate semantics. | Route pending; catalog pending. |
| LEG-009 | Open/activate/close session tabs | `SessionTabClicked`, `CloseSessionTabClicked`; session methods and `AppRuntime`. | SES, STR. | Wired; runtime checks pending. |
| LEG-010 | Detach sessions and maintain single-session placement across windows | `DetachSessionTabClicked`; `AppRuntime.OpenOrActivateSession` and `DetachSession`. | SES, STR. | Wired; close/placement tests pending. |
| LEG-011 | Resource loading and explicit refresh | `RefreshResourcesAsync`; Kubernetes snapshot service implementation. | SYN, ERR; refresh UI route and actual kinds. | Route/catalog pending. |
| LEG-012 | Resource filter controls, including activity/problem selection | `ActivityOnly`/`ProblemsOnly` bindings; `FilterPickerViewModel`. | FLT, TBL; full filter operator/value catalog. | Native cache-only filtering covers sixteen fields, including Age and UID. Problems/Activity are mutually exclusive and persist per session. The reference-compatible table Limit defaults to 256, accepts 1 through 5,000, and leaves the full filtered Radar, field picker and session-wide alarms intact. Missing measurements never substitute limits or capacity. Full paired presentation remains open. |
| LEG-013 | Save/rename/delete filter presets | `SaveFilterClicked`, `RenameSavedFilterClicked`, `DeleteSavedFilterClicked`; preset methods. | FLT, STR; preset naming, persistence, and scope details. | Native atomic CRUD protects the default and preserves sixteen fields, mode, sort and table Limit. Actual reference `FilterPresetStore.Save` exports pass fourteen Basic/Fusion import, selection, conflict and restart scenarios, including Limit 7; the exported file remains byte-for-byte unchanged. Installed-process migration, file-picker input and complete paired presentation remain open. |
| LEG-014 | Resource/event/port search and next/previous match navigation | Search callbacks; `NextResourceMatch`, `PreviousResourceMatch`, event navigation and port search methods. | FLT, ACC; exact search behavior. | Resources/Events/Ports cache search and shared next/previous Find navigation are implemented. Find wraps, selects cached matches and leaves filters, Radar, inspector and requests unchanged. Ports retain their owning forwards. Full paired presentation and shortcut-catalog evidence remain open. |
| LEG-015 | Resource/event/inspector sorting and table layout/copy interactions | Sort callbacks; `SaveTableColumnLayout`; copyable-cell pointer callbacks. | TBL, INS; all actual table types and dynamic column menus. | Resources/Events/Ports and inspector Events/Links share virtualized columns, typed three-state sorting, persistent hide/pin/order/width, Find, keyboard/context copy and overflow tips. Port filter/sort and inspector sorts survive restart without restarting transports. Link endpoint inspection is verified for pointer, context menu and keyboard. Values/Alerts/Diagnostics toolbox and installed-app paired presentation remain open. |
| LEG-016 | Event workspace and event-to-inspector navigation | `EventsWorkspaceClicked`; `FocusEventAsync`; event search/navigation callbacks. | INS, FLT, TBL. | Workspace wired; selected-event route pending. |
| LEG-017 | Inspector opening, tab selection, closing, and back/forward history | `ResourceDoubleTapped`, `InspectorTabClicked`, close/back/forward callbacks; resource focus methods. | INS; row-click conformance and complete inspector tab catalog. | Wired; conformance/catalog pending. |
| LEG-018 | Fresh YAML loading, editing, apply, and reset | `YamlEditorTextChanged`, `ApplyYamlClicked`, `ResetYamlClicked`; `LoadFreshYamlAsync`. | INS, ERR, SEC. | Wired; safeguards not yet proven. |
| LEG-019 | Delete selected Kubernetes resource | `DeleteSelectedResourceClicked`; `DeleteSelectedResourceAsync`. | ACT, ERR. | Wired; confirmation/uncertainty checks pending. |
| LEG-020 | Navigate resource references and relationship endpoints | `OpenKnownResourceReference`, `FocusRelationshipEndpointAsync`; `Controls/ResourceLinkButton.cs`. | INS; full dynamically constructed links/menu catalog. | Route/catalog pending. |
| LEG-021 | Reveal and copy key/value representations | Reveal and raw/decoded/preferred/key-copy callbacks. | SEC, INS; ConfigMap versus Secret presentation. | Wired; redaction and clipboard checks pending. |
| LEG-022 | Pod log retrieval, pause, and view state | `IKubernetesPodLogPort`; `LogsPaused` binding. | LOG, SES, SYN; complete container and view-control routes. | API/binding evidence; route catalog pending. |
| LEG-023 | Prepare/start/stop/open port-forward tasks | Port workspace/row/prepare/run callbacks; forward task methods and service `StartPortForwardAsync`. | SES, ERR; target eligibility and prepared-action semantics. | Native prepare/start/stop, Pod/Service traffic and session switch/close isolation have public UI and real Kubernetes evidence. The shared Ports table adds cache search, typed sorting, persisted columns, whole-row/keyboard/context inspection, endpoint/cell copy, HTTP/HTTPS actions and stopping. Service defaults/pagination and invalid stream/header/resource responses have passing public-boundary regressions. Installed legacy execution/presentation comparison, complete upgrade error classification and detached windows remain open; see the test map. |
| LEG-024 | Resource health and measurement presentation | `ResourceHealthCalculator`; Kubernetes service and workspace models. | MET; actual sources, quantities, and reference markers. | Catalog and public checks pending. |
| LEG-025 | Radar selection, pointer interaction, pan, zoom, reset, and source control | Radar pointer/source callbacks; focus, zoom, pan and reset methods. | FLT, ACC; exact source-control and selection behavior. | Wired; synchronization checks pending. |
| LEG-026 | Radar water and screensaver settings | `RadarWaterEnabledSetting` and `ScreensaverSetting` bindings. | ACC; existing effect behavior and preserved/changed disposition. | Screensaver explicitly removed; water retained with enabled/45% defaults, settings and reduced-motion/hidden lifecycle without Event-local effects. Scoped runtime/visual evidence is recorded in the test map, not a whole-product parity claim. |
| LEG-027 | Alert rule creation/duplication/deletion/enabling and matcher editing | Alert CRUD, group/criterion callbacks and methods. | FLT, ERR; exact rule semantics and filter-dependent evaluation. | Wired; behavior catalog pending. |
| LEG-028 | Alert colors and radar-zoom preview | Color status/none and zoom-preview callbacks. | FLT, ACC; overrides and preview side effects. | Wired; detailed intent/tests pending. |
| LEG-029 | Alert sound selection/preview/source attribution | Sound selection/preview/source callbacks; `AlertSoundPlayer`. | Existing shipped behavior; audio/attribution scenarios need explicit mapping. | Wired; playback tests pending. |
| LEG-030 | Master audio mute | `ToggleAudioMuteClicked` and `ToggleAudioMute`. | Existing shipped behavior; persisted setting/alert interaction mapping. | Wired; persistence tests pending. |
| LEG-031 | Settings navigation and telemetry controls | `SettingsWorkspaceClicked`, `SourcesWorkspaceClicked`; `TelemetrySetting` binding. | STR, SYN, ERR, SEC; full settings/diagnostic catalog. | Wired/binding evidence; catalog pending. |
| LEG-032 | Workspace restore preference | `WorkspaceRestoreSetting` binding. | STR; current versus confirmed restoration behavior. | Binding evidence; restart tests pending. |
| LEG-033 | About content and external links | `AboutSectionLoaded`, `AboutOpenLinkClicked`; `PickAboutBlock`, `OpenAboutUrl`. | Existing shipped behavior; full content/links catalog. | Wired; preservation mapping pending. |
| LEG-034 | Release update checking and download link | `CheckForUpdatesIfDueAsync`, `OpenUpdateDownload`; `DownloadUpdateClicked`, `ReleaseUpdateChecker`. | Weekly anonymous check, explicit check and verified native download/release links; UPD-001 through UPD-008. | Native implementation and public-boundary tests added; compatible published native assets and updated desktop image proof remain required. |
| LEG-035 | Theme and localization implementation | `AppThemeCatalog`, `PodlordLocalizer`. | STR, ACC; actual options, controls, and language fallback. | Catalog/routes pending; not yet asserted as exercised UI. |
| LEG-036 | Command palette and command safety | `OpenCommandPalette`, `ExecuteCommandText`; `CommandSafety`. | Existing source capability; supported commands and UI reachability. | Route/catalog pending; not evidence of a general terminal. |
| LEG-037 | Global keyboard and copy/resize interactions | `WindowKeyDown`, `WindowPointerPressed`, sidebar resize and copyable-cell callbacks. | ACC, TBL, INS; shortcut and focus catalog. | Wired; actual keyboard behavior pending. |

Rows with incomplete intent are not permission to drop the capability. Add newly discovered capabilities to this ledger, map every concrete control/action, and account for all raw-index entries before claiming completeness. Graph removal is explicitly approved and must not be accidentally reintroduced from old planning documents.

## Next Evidence Gates

1. Rebuild the captured source using its recorded toolchain/feed; record build outcome and artifact identity without overwriting the installed reference.
2. Launch the reference in a controlled test profile and prove local-state/port/auth-provider isolation.
3. Resolve literal bindings, dynamic routes, eligibility guards, and all inspector/settings/filter/alert catalogs; classify every shipped capability against the spec.
4. Assign actual public-boundary tests to old/new rows in the existing test map, including failures, reuse, concurrency, and cleanup. Keep source test attributes separate from executed evidence.

The source rebuild and empty-profile smoke verification below cover part of gates 1 and 2. Authentication/port-forward isolation, installed-artifact correspondence, and gates 3 and 4 remain pending. A separate native Qt Quick application now implements the initial file-import/session/resource-read workflow, with local UI and real Kubernetes evidence. This does not establish dual-application scenario parity or complete any capability-ledger row. Native verification is owned by [the test map](k3d-test-map.md).

## What Was Verified On 2026-10-02?

The first offline self-contained publish failed with NU1101 because the captured asset manifests did not include `Microsoft.NETCore.App.Runtime.osx-arm64` and `Microsoft.AspNetCore.App.Runtime.osx-arm64`. Both exact `10.0.9` archives were available locally and were added to the captured feed without changing the frozen source archive. `runtime-package-supplement.sha256` records their hashes; the feed now contains 50 package archives. The original capture metadata remains historical, not a claim that the first feed was sufficient.

The second `build-source-reference.sh` run restored exclusively from that feed and completed the macOS arm64 Release self-contained publish, bundle packaging, ad-hoc signing, and signature verification. Its unmodified bundle is in `builds/source.tGjG4z/out/Podlord.app`. `source-build-evidence.txt` records the source archive, executable, and application assembly hashes; the built bundle version is `0.1.0`.

A separate runtime copy at `runtime/Podlord Legacy Reference.app` changes only bundle naming/identity and its required signature. Its identifier is `dev.podlord.reference.legacy20261002`; `reference-wrapper.sha256` records its identity. `launch-source-reference.sh` starts that copy with isolated `HOME`, `PODLORD_HOME`, and `PODLORD_CONFIG_HOME`, unsets inherited `KUBECONFIG`, and disables update checks. Start it through this script, not by opening the bundle directly. The script remains attached to the caller's process lifetime; use a persistent terminal session.

The initial detached shell launch did not remain running. Desktop app selection then started another source-build instance without the isolated environment; it loaded the normal cluster configuration. That process was closed without UI actions. Background cluster reads and normal-profile writes during that attempt were not instrumented, so normal-profile non-interference is not claimed. The final runtime copy has a distinct app identifier to avoid selecting another same-identifier instance.

| Public boundary / observation | Result | Limit |
| --- | --- | --- |
| Frozen-source offline Release build and packaged signature verification | Passed after the two-package supplement. | macOS arm64 only; not a release-signing or multi-platform result. |
| Source-reference launcher and operating-system process inspection | Running executable resolved to the separately named runtime copy. | Not proof of equivalence with the installed `2026.6.20` artifact. |
| Main window | Rendered `NO SOURCE SELECTED`, `visible: 0/0`, `API: 0/min`, and `Synced: never`. | Empty-profile smoke scenario, not a populated-cluster E2E test. |
| Settings navigation and rendered screenshot | Settings opened; the default alert rules were visible. | No configuration edits, sounds, or alert execution were exercised. |
| Settings / Sources | Sources grid was empty. | No kubeconfig import or authentication was performed. |
| Settings / Diagnostics | Cache `0B`, zero cached snapshots, and an empty request audit were visible. Process RSS displayed `279.3MB`. | One RSS observation, not a repeatable benchmark or performance-gate result. |
| Isolated storage | `profiles/source/config/podlord/store.json` contained no imported contexts or sessions. | Keychain, browser, credential-provider, and migration isolation remain untested. |
| Process TCP socket inspection | No TCP sockets were listed at the observation time. | Snapshot only; no port forward was created or compared. |

No full test suite, container stack, C++ executable, or old/new parity runner was executed for this verification.

The native runtime now also exposes `podlord-source` for owned kubeconfig file import and listing. Source/session CLI composition is executable; this does not establish Qt Quick, authentication, Kubernetes, remaining import-channel or full legacy parity. Current behavior and gaps are owned by the test map and ADR 0017.

## What changed in the appearance evidence on 2026-10-03?

Native appearance now has the 19 existing theme palettes, dark/light,
subtle/medium/arcade, persistent selection, and error-safe selection through a
public Qt dialog. The migration tests compare semantic colors against the C#
catalog and test actual rendered control palette roles. See
[the appearance test evidence](k3d-test-map.md#native-appearance-what-is-actually-verified)
and [the palette/settings decision](../adr/0024-native-theme-palette-and-settings.md).

This does not close appearance/localization or whole-product visual parity.
The desktop comparison shows different typography, layout and texture; the
native app still lacks the reference application's full settings navigation,
localization, and radar presentation. The initial screenshots used empty,
isolated profiles. Subsequent populated comparison evidence is scoped below;
whole-product equivalence and release readiness remain blocked.

## What Did The Populated Comparison Add To The Capability Inventory?

Both independently launched desktop applications used a real local Kubernetes
cluster with 1051 labelled namespaced objects across eight namespaces, plus
controller-created resources and Events. All nine legacy Settings sections,
Resources/Radar/Events/Ports and available inspector pages were captured. The
[test map](k3d-test-map.md#what-did-the-populated-desktop-comparison-on-2026-10-03-establish)
owns the 122 originals, 54 unmodified pairs, actual build/input identities,
public scenarios and cleanup evidence. This supersedes the empty-profile limit
only for the executed scenarios, not the unresolved capability ledger rows.

At that comparison native radar, metrics summary, Events/Ports workspaces,
inspector Events/Links and most Settings sections were absent. Inspector width,
metadata presentation, typography and textures differed substantially. All
19 palette selectors working did not close LEG-035. RWT-016/017 and the native
release completion table now own these required corrections. The first native
radar/related-inspector package is tracked separately from this pre-change
comparison. No omission is removed or reclassified as optional.

## What did the native table/theme comparison establish on 2026-10-03?

LEG-015 now has scoped Resources/Events column-layout evidence: hide/show, pin,
order, width, defaults, persistence and error behavior. It remains incomplete for
all legacy table surfaces. The [test map](k3d-test-map.md#which-native-table-layout-and-package-checks-passed-on-2026-10-03)
links the behavior-to-test ledger and packaged-binary artifacts.

The 19-theme dark/light matrix covers four native surfaces, not every legacy
capability or all inspector/settings/intensity combinations. Three original
C#/C++ comparisons cover Resources, Events and Appearance. They visibly show
missing dashboard, advanced filtering, Ports and Settings/navigation coverage;
matching palette selection is not visual or functional parity. The resource
counts differ and must be reconciled before equal-data comparisons can pass.
The Radar image is populated, but complete accessibility evidence is absent.

The earlier statement that the C++ application did not exist describes the
initial baseline-capture stage only. A native application and scoped package now
exist; neither the initial baseline nor the current screenshots establish release
readiness. The current full native suite passes 845 tests, while branch coverage
and installed-package size still fail their gates. Other open inventory items
remain open; none were removed from scope by this comparison.

## Which Capabilities Advanced In The Native Alarm And Metric Increment?

LEG-024 now has a filtered native Dashboard and inspector gauges grounded in the
Metrics API and separate configured references. Actual zero, unavailable, partial,
stale and above-request usage have public UI tests; a real two-container Pod was
also exercised. This is not yet the complete legacy dashboard or storage-usage
capability.

LEG-027 through LEG-030 now have native rule CRUD, locked defaults with duplication,
AND/OR matcher groups, whole-session cache evaluation, finite radar effects,
filtered focus, persisted mute/reduced motion, and attributed local sound preview.
The full sound catalog/import, precise view-entry/camera/once behavior, audible
hardware and visual/device comparison remain incomplete. Rule-store protection
and focused public behavior tests are documented in the existing test map.

These rows are partially implemented, not completed or approved for removal.
The other ledger rows retain their existing evidence limitations. The previously
captured column/theme screenshots predate these additions and do not prove their
presentation quality.

## Native Release Assessment On 2026-10-05

Verdict: not a release-ready replacement. This is a capability-by-capability
review of the 37 inventoried C# functions, not proof of every implementation
branch or every device. Native entrypoints were checked against the reference
callbacks and the existing test map. A tested path below does not imply the
remaining paths, visual states or platforms passed.

| Capability | Native public entrypoint and established scope | Release gap or disposition |
| --- | --- | --- |
| LEG-001 | Application `--profile`, help/version and isolated default startup tests; explicit `--kubeconfig` file/file-URL/folder import through the existing asynchronous workspace. | Twenty-five public startup scenarios pass in the SDK build and on the actual portable Cocoa bundle, including thirteen initially failing explicit-import cases. No ambient import, session activation or authentication is introduced. An initial installed re-import timeout remains unexplained. Arbitrary repeated/positional startup paths and other-platform evidence remain open; ADR 0040 owns the scoped behavior. |
| LEG-002 | Settings Sources exposes `importHomeButton` through `Workspace.importHome`; real UI tests import an isolated home kubeconfig without opening a session. | Implemented explicit home action; all-target picker/accessibility and visual comparison remain open. No surprise startup authentication. |
| LEG-003 | Sources file/folder picker and `Workspace.importFile`; path/URL, recursion, partial failure and repeat scenarios. | Implemented paths; all-target picker/accessibility evidence remains open. |
| LEG-004 | `PasteSourceDialog` enters the shared source store through the native paste action with an explicit original path. | Basic/Fusion UI tests cover import, cancellation/Escape, invalid/empty input, invalid origin, busy store and repeat/changed identity. Separate public-store cases check size limits. All-target visual/accessibility evidence remains open. |
| LEG-005 | Native source import and the seventh command-palette action export generated K3D kubeconfigs through the installed CLI, retain immutable snapshots and normalize only parsed local endpoints. Forty-one external-CLI scenarios and an owned real K3D/Kubernetes run pass. | Installed-app/all-platform visual evidence remains open; Docker and K3D are needed only for this optional import action, not ordinary Kubernetes access. |
| LEG-006 | Settings Sources exposes explicit `refreshSourceFilesButton` through `Workspace.refreshSources`; real UI tests cover unchanged, changed, unavailable and repeat refresh while preserving snapshots. | Automatic source-refresh workflow and full reference visual equivalence remain open; explicit refresh is already implemented. |
| LEG-007 | Sources lists contexts and supports import/reload/open plus per-context display aliases through a native dialog and the public source CLI. The existing source store retains canonical identity, YAML and credentials; unchanged reimports preserve aliases. | Alias UI has 24 passing Basic/Fusion scenarios and format/CLI validation is included in the broader regression. Removal, filter assignment and installed populated-source parity remain open. Source aliases deliberately do not rename independent sessions. |
| LEG-008 | `SessionManager` exposes context/namespace configuration, closed changed-config snapshots and independent closed copies; native rename is also available. | Real Basic/Fusion UI tests cover unchanged/changed/invalid/busy saves, context changes, repeated saves, named/unnamed copies, cancel/Escape, restart and narrow Close accessibility. Complete all-target visual/accessibility parity remains open. |
| LEG-009 | Open/activate/close tabs and persistent sessions; public UI/session lifecycle tests. | Implemented paths; cross-window placement is separate. |
| LEG-010 | Native windows share one transport/credential/update owner and have independent cached views. Tabs, saved sessions and imported contexts open in separate windows without duplicate placement; drafts and shared preferences are preserved. | Basic/Fusion public window scenarios and real Kubernetes shell/forward transfer pass. Physical C#/C++ multiwindow images, installed restart and platform lifecycle evidence remain open. |
| LEG-011 | Background discovery/list refresh, partial cache publication, explicit refresh/authentication. | Real local Kubernetes read paths established; exact legacy Problems sets and all discovery/platform variants remain open. |
| LEG-012 | Cache-only global search and sixteen field filters, including typed Age and UID, Problems/Activity, quantity/readiness/restart tests. The editable table Limit defaults to 256 and caps only the sorted table projection, including in the narrow drawer. | Measurements deliberately do not substitute limits/capacity. Public tests check the real table rows, late filter matches, full picker/Radar scope, reset, presets, session isolation, refresh and restart. Complete paired visual evidence remains open. |
| LEG-013 | Native saved filter CRUD, per-session restoration and default/protected preset behavior. Actual C# `FilterPresetStore.Save` exports are imported through the public native workspace and selected through QML controls; fields, Problems/Activity, unavailable metrics, repeat/conflict protection, Limit 7 and close/reopen are checked. | Native file-picker, installed-process migration and complete paired visual evidence remain open. The reference display Limit is not a resource predicate; both view and preset stores now preserve it. |
| LEG-014 | Resources/Events/Ports cache search and shared next/previous/wrap Find; inspector history back/forward. | Complete paired presentation and shortcut-catalog evidence remain open. |
| LEG-015 | Resources/Events/Ports, inspector Events/Links/Values, alert rules and Diagnostics use the shared virtualized table with applicable sorting, column controls, Find, copy and overflow presentation. Inspector endpoint navigation, masked value actions, stable rule selection and persisted layouts have executable regressions. | See the Values, alert-table and inline-Diagnostics entries in the test map for executed paths. Physical platform input and whole-product visual parity remain separate gates; shared controls alone do not prove every surface. |
| LEG-016 | Cached Events and UID-bound resource navigation. | Tested paths; explicit all-state visual and keyboard comparison remains open. |
| LEG-017 | Row/context inspection, cached presentation, foreground refresh, history and stale/failed handling. | Fresh edit/write safeguards tested; whole inspector visual/performance/assistive-technology equivalence remains open. |
| LEG-018 | Fresh-only YAML edit, local validation, diff, version/UID checks, apply/readback/conflict flows. | Real local write evidence exists; complete legacy editor presentation and all supported resource/action variants remain open. |
| LEG-019 | Target-specific delete confirmation, UID/version checks, errors and uncertain-outcome readback. | Real local deletion demonstrated; other platforms and every reference action remain unclaimed. |
| LEG-020 | Cached owner/node/namespace links and related Events. | Native subset exists; complete legacy relationship catalog and matching table controls remain open. |
| LEG-021 | Values use the common five-column table with sort, Find, persisted column controls, hover, reveal and explicit preferred/key/raw/decoded copying from the cached Inspector document. Secret copying does not reveal the screen. Binary decoding fails visibly without replacing the clipboard. | Basic/Fusion public table and copy/lifecycle tests cover these paths. Installed C#/C++ visual comparison and physical-device input remain open. |
| LEG-022 | Default all-container logs, explicit container selection, bounded polling/retention, pause/follow/copy. | Tested cache and real Kubernetes paths; reference restart/history controls and every visual state remain open. |
| LEG-023 | Pod/Service forwards use direct API transport, session-owned actions and a shared virtualized table with sorting, columns, Find, copy and inspection. Real Kubernetes traffic verifies uninterrupted window transfer and independent close/port release. | Physical installed-table comparison and the complete upgrade-error matrix remain open. Browser-action tests are distinct from actual browser/server protocol compatibility. |
| LEG-024 | Health/CPU/memory/storage snapshots, unavailable/stale states, typed quantities and loading health. | Real metrics established; all suggested markers/aggregation variants and required installed-app performance profile remain open. |
| LEG-025 | Deterministic reference projection, clickable additional Namespace resources, filters, zoom/drag/keyboard and alert focus. | Exact recorded topology established; all-theme populated full-window comparison, Metal/mobile rendering and performance remain open. |
| LEG-026 | Background water, no event water effects; hidden/minimized/reduced-motion ownership tests. | Idle screensaver explicitly removed by confirmed specification; do not migrate it. Water still needs complete old/new visual evidence. |
| LEG-027 | Session-wide Alerts with three reference defaults, CRUD, AND/OR groups, active matches and draft failures. The shared table supports stable UUID selection, three-state sort, column controls, Find and copy; match ticks update the Active column only. | Matcher presentation and full installed-reference images remain open. Filters intentionally never hide alarm evaluation. |
| LEG-028 | Status/fresh/custom color, animation, alert focus and persisted actions. Editor zoom preview evaluates the unsaved draft against the real cache without saving or playing sound, reveals the Radar and rejects stale-scope results. | Public preview tests cover invalid/empty/loading/hidden/narrow/repeated scenarios. Full installed-reference visual comparison remains open. |
| LEG-029 | All 117 reference catalog entries, 116 embedded assets, local metadata search, persisted selection, preview and explicit source-link/retry actions. | Cocoa/Darwin decoding and source actions are tested; physical audibility, other-platform decoding and full editor visual comparison remain open. |
| LEG-030 | Master mute and reduced motion exposed to Alerts/Radar. | Native tested behavior; persisted restart and reference audio/keyboard comparison remain explicit gates. |
| LEG-031 | Nine inline Settings sections, bounded diagnostics snapshot, explicit refresh, telemetry off. Runtime and audit tables have sorting, copy, persisted column controls, Find and narrow-screen keyboard tests. | Complete installed-reference diagnostics images and telemetry capability disposition remain open; do not silently add telemetry. |
| LEG-032 | Sessions/filters/sorts restore automatically by default; a real Workspace checkbox controls automatic tab restoration on the next launch. Disabled startup retains session identities/history/configuration and saved views, allows explicit reopen and does not recreate forwards. | 28 Basic/Fusion public startup/UI scenarios cover the setting. Saved view retention, concurrent-window placement ownership and actual installed C#/C++ restart comparison remain open. |
| LEG-033 | About shows product/version/privacy text, six reference project/support links, bundled MIT license and grouped attribution for all seven shipped audio packs. | Offline third-party runtime distribution notices/source delivery remain open. External-browser handoff tests do not prove physical-device browser availability. |
| LEG-034 | `ReleaseUpdates`, About update controls, startup/activation scheduling, private atomic result cache. | Weekly policy confirmed on 2026-10-08. Exact native OS/architecture assets only; existing C# artifacts are never offered. Publication and new desktop screenshot proof remain open. |
| LEG-035 | Nineteen theme palettes and dark/light have executable palette/render scenarios; inline language selection exists. Theme intensity was explicitly removed on 2026-10-08 (ADR 0024), including native runtime/API/storage state. | Full translated-control coverage, all-surface visual, contrast, focus and physical-device evidence remain open. Old profiles must migrate without losing unrelated settings. |
| LEG-036 | Native Qt-Action palette exposes all seven reference commands: Resources, Events, Sources Settings, Port Forward preparation, Settings, Problems toggle and Import K3D. Navigation buttons reuse the same actions; all 36 public palette scenarios pass after the import addition. | Full installed-reference/platform comparison remains open. This palette is not an embedded container terminal. |
| LEG-037 | Table/radar keyboard paths, inspector Back/Forward, Ctrl/Meta+K palette activation and keyboard command selection, plus sidebar resizing. | Complete reference global shortcut/focus/copy/resize catalog and all-target assistive-technology evidence remain open. |

### Which Cross-Cutting Gates Still Block Release?

- Native distribution is not wired into release assets. The added native
  preflight gates CI/release but intentionally preserves the old distribution.
- The local arm64 package passes static dependencies and size limits:
  36,524,314 ZIP bytes; 98,726,138 installed regular-file bytes. Only ad-hoc
  signing is established. Runtime plugin loading, clean-device installation,
  Developer ID/notarization and complete license notices are separate gates.
- Native Windows/Linux/Android/iOS installs, mobile authentication, precise
  OS/CPU support and device-specific capability differences are not established.
- Fresh first review run: native 2044/2045, with a reproducible search-restore
  test race. C# 458/458 but its coverage gate fails at 94.95% lines; branches
  are 83.90% against that runner's existing 80% gate, not the 90% native gate.
  Corrected native executions and coverage are recorded by the entrypoints in
  [the test map](k3d-test-map.md), not inferred from earlier green counts.
  Post-correction: 2045/2045 pass in 447.47 s; 40 repeated search-restore
  executions and seven Cocoa package startup cases pass. Native coverage is
  96.79% lines but 80.52% branches, so the 90% branch gate still fails.
  Lowest source branch coverage includes resource apply (54.17%), field-filter
  UI ownership (59.89%), deletion (61.11%) and port-forward transport (63.89%);
  passing happy paths do not establish their unexecuted failures/lifecycles.
- The measured 5,000-Pod offscreen frame p95 of 8.203 ms does not establish the
  <=8 ms UI-frame target, installed-app RSS/idle CPU, Metal or mobile budgets.
  The full three-session plus visible-log profile remains required.
- Current reference screenshots are partial same-cluster evidence, not identical
  live snapshots or all themes/surfaces. The old Alerts comparison predates the
  latest table/editor refactor; do not present it as a current native screenshot.
- The frozen C# baseline remains available. Migration/rollback scenarios and
  actual installed-artifact correspondence still require explicit evidence.

### Welche Assessment-Punkte wurden am 2026-10-05 nachgezogen?

Die native Sources-Oberfläche ergänzt Paste, expliziten Home-Import und Datei-Refresh. Der Session-Manager ergänzt Kontext-/Namespace-Snapshots und unabhängiges Duplizieren über die vorhandenen Stores. Diese Korrektur gilt nur für diese Aktionen; Source-Umbenennung/-Entfernung sowie C#-Session-Sicherheitsstufe/Farbe/Icon sind weiterhin offen. Ein grüner Komponententest ersetzt weder tatsächlichen Desktop-Vergleich noch die ausstehende Plattform-, Signierungs-, Lizenz-, Performance- und Branch-Coverage-Freigabe.

### Which audio parity gap was closed on 2026-10-06?

LEG-029 now preserves the complete C# source catalog: 117 entries, including the silent action, and all 116 embedded audio files. The native chooser searches the reference metadata locally, keeps selection unchanged when search results disappear, displays purpose/attribution/source, and persists every catalog ID through the existing alert store. The source-catalog comparison reads the actual reference file; it is not an installed-C# visual comparison.

Real Cocoa/Darwin decoding and repeated playback are exercised for every asset. The final catalog package passed 16 physical dependency tests and five isolated Cocoa startup/metadata cases. Repeated playback with the package's own Cocoa/Darwin plugins was also checked in a disposable copy of the preceding package. The chooser's source action covers all seven audio packs, project attribution, filtered selection and explicit retry after browser handoff failure; it neither saves the draft nor contacts Kubernetes. About groups all 116 assets into seven attributed pack links instead of constructing 116 rows. The combined sound-source/About subset passed 102/102. The test map owns the commands, counts, package sizes and cleanup scope. These results do not establish audible physical-device comparison, another platform's codec support, full visual parity or release approval.

The unchanged native branch gate and C# line/branch gates still block release. The complete sound/source/credits native run passed 2,834 tests with 97.05% lines and 81.73% branches; the C# run passed 458 tests with 94.95% lines and 83.90% branches against the aligned 95%/90% gates. Later credential/authentication changes must be evaluated in a fresh complete run rather than borrowing those earlier results.

### Which authentication gap was narrowed on 2026-10-06?

The native resolver now preserves `KubeconfigAuthLoader.AuthProviderToken`'s cached access/id token capability through the ordinary bearer request owner. Explicit token/token-file values remain authoritative; cached provider values never authorize a provider refresh, browser or executable. Invalid or absent values fail explicitly. Public HTTP tests cover actual authorization headers, resource cache publication, 401 suspension and snapshot-preserving rotation. Proxy transport, impersonation and interactive legacy provider refresh remain unsupported; they must not be inferred from cached-token support. Referenced credentials now share the existing bounded regular-file input policy. Exact evidence and limitations belong in the test map.
