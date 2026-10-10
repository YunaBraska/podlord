# k3d And Performance Test Map

Podlord's Kubernetes integration tests use a disposable k3d cluster. The suite is intentionally slow and proof-oriented.

Not every behavior belongs in k3d. Real Kubernetes proves API truth, RBAC, metrics, port-forwarding, and object lifecycle behavior. UI freeze and redraw behavior must be verified with deterministic headless/UI budget tests because cluster startup, Docker, image pulls, and API latency would hide the actual regression. Fog machine, wrong room.

## Cluster

- One k3d server node
- Disposable kubeconfig under the test temp directory
- Admin context from k3d
- Limited RBAC context generated from a ServiceAccount token

## Scenarios

| Scenario | Resources | Proof |
|---|---|---|
| Flat resource explorer | Namespace, Node, Pod, Deployment, Service, EndpointSlice, ConfigMap, Secret, Job, CronJob, PVC, NetworkPolicy, Event | Podlord can list a real cluster across namespaces and API groups. |
| Filters | Pod and Deployment queries by kind, namespace, status, and search | Filtering is applied after real API reads. |
| Secret redaction | `Secret/podlord-secret` | Secret data and managed fields do not reach YAML output. |
| Broken workload | `Deployment/podlord-broken` and its pod events | Image-pull failures surface as status/events. |
| Logs | `Pod/podlord-log` | Pod log tail uses the bound kubeconfig context. |
| Jobs | `Job/podlord-success`, `Job/podlord-fail`, `CronJob/podlord-cron` | Batch statuses are visible. |
| Networking | `Service/podlord-healthy`, generated EndpointSlice, NetworkPolicy | Network resources list and inspect. |
| Cluster-scoped detail | Node and Namespace | Cluster-scoped detail paths work without namespaces. |
| RBAC | `ServiceAccount/podlord-limited` | Forbidden API responses become explicit freshness failures. |
| Boundary validation | Pod detail without namespace | Invalid input fails before network calls. |

## UI And Performance Map

| Behavior | Verification | Current proof | Budget |
|---|---|---|---|
| First load does not show partial rows or radar | Fake Kubernetes HTTP + ViewModel | `First_load_does_not_render_partial_cache_rows_or_radar_until_sync_finishes` | Rows/radar stay empty until first sync finishes. |
| Real cluster first load, tabs, radar, health, inspector | k3d E2E + ViewModel | `Podlord_ui_state_survives_real_k3d_loading_tabs_radar_health_and_inspector` | Tab switch while loading under 750 ms. |
| Real deployments, events, metrics, inspector | k3d E2E + warmed cache | `Podlord_ui_drives_real_k3d_events_deployments_metrics_and_inspector` | Real API behavior, no timing budget. |
| Real RBAC fallback | k3d E2E + limited kubeconfig | `Podlord_ui_drives_real_k3d_limited_rbac_namespace_scope` | Forbidden state appears; namespace scope still loads. |
| Real native port-forward | k3d E2E + localhost HTTP | `Podlord_ui_starts_and_stops_real_k3d_port_forward` | Local HTTP responds through the forward. |
| Large cache filter apply and clear | Public ViewModel budget test | `Large_cache_filter_changes_are_cache_only_and_budgeted` | Each local filter action under 1 s; no network request. |
| Radar resize and zoom | Public ViewModel budget test | `Large_cache_radar_viewport_and_zoom_updates_are_budgeted` | Resize under 1.5 s; zoom under 1 s; no network request. |
| Graph/events workspace materialization | Public ViewModel budget test | `Large_cache_graph_and_events_workspace_materialization_are_budgeted` | Workspace materialization under 1.5 s; no network request. |
| Cached tab switch | Public ViewModel budget test | `Cached_tab_switches_restore_session_state_under_budget` | Each cached tab switch under 1.5 s; no network request. |
| Inspector cache-first focus | Public ViewModel budget test | `Inspector_focus_renders_cached_summary_before_fresh_detail_returns` | Cached inspector appears under 150 ms before fresh detail returns. |
| Dispatcher stall on settings/source view | Headless Avalonia UI | `Opening_sources_settings_updates_title_and_active_tab_state_without_stalling_dispatcher` | Open sources settings under 250 ms. |
| No redraw churn on unchanged data | Fake Kubernetes HTTP + ViewModel | `Unchanged_refresh_does_not_redraw_resource_table_or_radar` | Resource and radar collections do not republish. |
| Visual frame stability on tiny cache delta | Headless Avalonia UI | `Tiny_cache_delta_keeps_visible_rows_and_frame_stable` | Render hash stays identical. |

## Known Hot Paths

| Path | Why it is watched | Guard |
|---|---|---|
| `MainWindowViewModel.ApplyLocalFilterCore` | Sorts and filters cached rows, evaluates alerts, syncs resource rows, updates radar and pulse data. | Large cache filter budget tests. |
| `MainWindowViewModel.UpdateRadarBlocks` | Rebuilds the deterministic radar island and alert visual state. | Radar budget tests and radar behavior tests. |
| `MainWindowViewModel.SelectWorkspace` | Schedules secondary graph/events work and may restore rendered state. | Graph/events workspace budget tests. |
| `MainWindowViewModel.SelectedSession` / `ActivateSessionTab` | Saves and restores per-session rendered state, filter, radar, and selection. | Cached tab switch tests and k3d multi-session tests. |
| `MainWindowViewModel.OpenSelectedResourceAsync` | Must render cached data before fresh API detail returns. | Inspector cache-first budget test. |
| `KubernetesResourceService.WarmResourceCacheAsync` | Real API fan-out, request queue, cache merge, metrics fallback. | fake Kubernetes cache tests and k3d E2E tests. |

## Gaps

| Behavior | Best test layer | Status |
|---|---|---|
| Full window FPS under manual pointer movement | Manual profiling or future UI automation harness | Not stable enough for k3d. |
| OS compositor flicker | Real desktop smoke/profiling | Headless tests can catch redraw churn, not compositor brightness flicker. |
| Very large production cluster scale above fixture size | Synthetic public ViewModel budget tests plus optional stress fixture | k3d can grow this later, but runtime would be slow. |
| Long-running memory growth | Runtime profiler and diagnostics assertions | Not a k3d-only problem. |

Run:

```sh
scripts/test.sh
```

The script starts Colima when available, ensures k3d/kubectl exist, creates the cluster during tests, and deletes it afterward.

## C++ Rewrite Comparison Evidence

The specification interview is complete. Continuous old/new comparison is required by RWT-009 through RWT-015 in `podlord-operational-spec.md`; sequencing is owned by `../roadmap.md`. This section seeds comparison work, not a completed feature inventory or executed test report. Existing testcase mappings remain in force until reconciled against actual shipped behavior and the accepted contract.

Before changing replacement-sensitive code, record an immutable old baseline identity, runnable artifact identity, required toolchain/dependencies, and independently repeatable build/run commands. Record the new build identity alongside every comparison. Old/new runs must use isolated local state and listening ports. Mutations need equivalent isolated test-owned resources or reset initial conditions; running both deletes against one object is not a valid parity check.

For each inventoried function, extend this map with its public scenario, owning requirement or explicitly unresolved intent, actual assigned test name, old/new outcomes and build identities, supported target, evidence reference, disposition, and remaining gap. Dispositions are preserved, intentionally changed, approved removed, or unresolved. An unassigned or unresolved required function blocks complete-parity claims. Planned tests and successful compilation are not passed public-behavior evidence.

| Comparison area | Owning contract | Public boundary/scenarios to inventory | Old/new evidence and test assignment |
| --- | --- | --- | --- |
| Imports and source snapshots | SES; shipped-function inventory | Actual supported file, folder, pasted/default/generated sources; unchanged/changed imports; validation, errors, and original-file preservation. | Pending inventory and test assignment; not executed. |
| Session lifecycle | SES, STR | Usage ordering and naming, tabs, detached windows, restart restoration, isolation, close behavior, and forwarding ownership. | Pending inventory and test assignment; not executed. |
| Resource discovery and scope | SYN, ERR, FLT; shipped-function inventory | Actual supported kinds and custom-resource handling, API discovery, empty/partial/denied results, queued reads, late responses, and context isolation. | Pending inventory and test assignment; not executed. |
| Tables and navigation | TBL, INS-001 through INS-003, FLT | All actual table types, sort/layout/copy/overflow controls, row/context-menu inspection, filtered membership, and scroll/selection retention. | Pending inventory and test assignment; not executed. |
| Inspector and mutation | INS, ACT, SEC | Cached/fresh detail, draft protection, previews, concurrent edits, apply/delete outcomes, secret presentation, auth failures, and uncertain response loss. | Pending inventory and test assignment; not executed. |
| Pod logs | LOG, SES-026 through SES-027 | Single/all-container selection, ordering/source labels, partial failure, pause/follow, size limits, hidden polling, and dispatched-request lifecycle. | Pending inventory and test assignment; not executed. |
| Port-forwards | SES; shipped-function inventory | Actual supported Pod/Service targets, real local connections, collisions/failures, tab detach/switch/close, and local-port release. | Pending inventory and test assignment; not executed. |
| Resource measurements | MET, SYN | Real sources and quantities, request/limit markers, absent/stale/partial data, storage semantics, and any actually sourced recommendations. | Pending inventory and test assignment; not executed. |
| Radar, alerts, and audio | FLT, ACC; shipped-function inventory | Actual selection/zoom/rules/status/audio behavior, filter scope, non-replay on tab switch, Settings controls, and reduced motion. | Pending inventory and test assignment; not executed. |
| Settings and diagnostics | STR, SYN, ERR, SEC; shipped-function inventory | Existing configuration and diagnostics surfaces, validation, persistence, stable snapshots, error feedback, reauthentication controls, and sensitive-data exclusion. | Pending inventory and test assignment; not executed. |
| Accessibility | ACC | Real keyboard/focus/dialog operation, accessible status/error descriptions, supported assistive technology, text scaling, and small-window reachability. | Pending target/tool availability and test assignment; not executed. |
| Performance and release | PER, RWT | Public interaction/frame measurements, idle CPU/RSS, build/package/install on claimed targets, baseline availability, migration, rollback, and cleanup. | Pending benchmark protocol, target matrix, and test assignment; not measured. |

Any additional shipped function discovered outside these seed areas must receive its own mapping; this table is not evidence of completeness. Known old failures do not define desired behavior: compare with the specification and record the discrepancy. Approved Graph removal is an intentional scope difference, not a missing feature to reintroduce; any surviving graph dependencies require reconciliation.

Normalize only demonstrably incidental comparison differences, with a recorded reason. Do not normalize away target identity, authorization, failures, operational side effects, or retained-state differences. Verify app-owned test cleanup on success, failure, interruption, and partial setup, retaining at most the latest task-owned test image where useful. Do not prune shared or unrelated resources.

### Captured Legacy Reference And Capability Mapping

The 2026-10-02 baseline is recorded in [the legacy capability inventory](legacy-capability-inventory.md). It preserves workspace sources, the independently captured installed bundle, SDK, dependency inputs, and 48 available package archives. At this initial capture stage, source-to-artifact correspondence, offline rebuilding, runtime isolation, and old/new executions were unverified; the C++ application had not yet been implemented. Subsequent scoped implementation and evidence are recorded below.

The inventory owns source-observed capabilities LEG-001 through LEG-037. Those identifiers are evidence/disposition keys, not additional normative requirement definitions. Extend the comparison rows above with these keys and actual testcase assignments. The raw scan found 388 Fact/Theory attributes, not 388 proven public scenarios or executed tests. Its handler metadata includes eight status-binding expressions; use the documented erratum rather than assert that all 88 distinct values are callbacks.

| Comparison area | Source capability keys to reconcile | Immediate coverage gap |
| --- | --- | --- |
| Imports and source snapshots | LEG-001 through LEG-008 | Resolve import variants and public routes, snapshot/edit/delete semantics, and existing source-test associations. |
| Session lifecycle | LEG-009, LEG-010, LEG-032 | Prove window/tab placement, close ownership, and restart behavior against the accepted contract. |
| Resource discovery and scope | LEG-011, LEG-012 | Inventory supported kinds/CRDs and bind actual discovery, partial/error, filter, and refresh routes to public tests. |
| Tables and navigation | LEG-013 through LEG-017, LEG-037 | Map every table/control, dynamic column/context menu, saved filter, search, and keyboard action. |
| Inspector and mutation | LEG-017 through LEG-021 | Inventory tabs/reference links and prove draft, preview, concurrent-write, secret, deletion, and uncertain-outcome behavior. |
| Pod logs | LEG-022 | Resolve full container/pause/follow/budget UI routes and assign actual public log tests. |
| Port-forwards | LEG-023 | Prove actual target eligibility, real connections, prepared actions, ownership, port isolation, and cleanup. |
| Resource measurements | LEG-024 | Inventory real sources, markers, missing/stale/partial values, and storage semantics. |
| Radar, alerts, and audio | LEG-025 through LEG-030 | Resolve every current rule/matcher, preview, sound, source attribution, mute, effect, and filter interaction. |
| Settings and diagnostics | LEG-031, LEG-032, LEG-035 | Inventory all settings/diagnostics controls, persisted fields, localization/theme options, and sensitive-data boundaries. |
| Additional shipped surfaces | LEG-033, LEG-034, LEG-036 | Explicitly map About, update/download behavior, and any reachable command-palette commands instead of silently omitting them. |
| Accessibility, performance, and release | All applicable LEG keys | Execute public flows on supported targets and establish real benchmark, migration, rollback, and package evidence. |

Before running the recorded old test script, replace or isolate its global `docker image prune -f` and prefix-wide cleanup behavior for comparison use. Preserving that script as baseline evidence is not permission to prune unrelated resources. No test execution or coverage pass is claimed by this inventory update.

### Preserved legacy reference smoke verification (2026-10-02)

These are manual public-boundary checks, not automated test methods or full k3d E2E coverage. Build and runtime details, including the initial unsafe launch attempt, are recorded in [the legacy inventory](legacy-capability-inventory.md#what-was-verified-on-2026-10-02).

| Behavior / scenario | Public entrypoint | Check name | Result / remaining gap |
| --- | --- | --- | --- |
| Rebuild frozen legacy source without network package restore | `build-source-reference.sh` / macOS package build | `legacy_offline_release_build` | Passed with SDK 10.0.301 and 50 captured packages, including two runtime supplements. Other architectures and installed-artifact correspondence unverified. |
| Launch legacy reference with empty isolated state | `launch-source-reference.sh` / native Resources and Settings UI | `legacy_empty_profile_launch` | Passed for the distinct-identifier wrapper: no sources/resources, zero request audit/cache, isolated store, and no observed TCP sockets. Initial normal-identifier selection launched a normal-profile instance; keychain/browser/auth/forward isolation unverified. |
| Open reference settings surfaces without importing data | Native Settings / Alerts, Sources, Diagnostics | `legacy_empty_settings_navigation` | Rendered settings and empty Sources; diagnostics visible. No edits, alerts, audio, or populated-cluster behavior tested. |
| Close owned comparison processes | Native application Quit / OS process table | `legacy_reference_quit_cleanup` | All three task-started app processes exited. Failed build workspace and stale launch/PID files removed; reproducible baseline and successful build retained. |

### Native session runtime verification (2026-10-02)

Scope: the standalone `podlord-session` CLI and its real local filesystem/process boundary. This supplies part of LEG-008/LEG-009 and RWT-003/RWT-007/RWT-011; it does not establish legacy/new GUI parity. `sh scripts/test-native.sh` rebuilds, executes isolated scenarios concurrently, benchmarks the public CLI, merges only fresh-run LLVM profiles, and enforces 95% line / 90% branch coverage. Test names below have the prefix `native.sessions.`; malformed-input tables register one explicitly named CTest case per input, rather than combining different failures into one testcase.

| Behavior / scenario | Public entrypoint | Test name(s) | Result / remaining gap |
| --- | --- | --- | --- |
| Empty read is side-effect free; new profile is private | CLI `list` / `create` | `empty_read`, `missing_profile_read`, `new_profile_private`, `private_permissions` | Passed on macOS arm64. Windows ACL behavior and device profiles remain unverified. |
| Named/unnamed creation and canonical namespace scope | CLI `create` | `create_unnamed`, `create_named`, `normalize_namespaces`, `invalid_namespace`, `empty_context`, `whitespace_context`, `invalid_context_control`, `invalid_name` | Passed. Context identifiers are stored, not yet resolved against imported kubeconfigs. |
| Immutable configuration copies and repeated equivalent input | CLI `snapshot` | `unnamed_snapshot`, `named_snapshot`, `unchanged_snapshot`, `snapshot_invalid_context`, `snapshot_ordinal_overflow` | Passed. Final UI edit-to-snapshot routing is not implemented. |
| Activation, reuse, close, and retained history across real process restarts | CLI `open` / `close` | `activate_idempotent`, `activate_switch`, `close_preserves`, `reopen_usage`, `close_already_closed`, `close_inactive` | Passed. Usage is recorded; final presentation ranking, tabs/windows, and tab-owned forwarding are still pending. |
| Delete and reuse only the highest freed unnamed number | CLI `delete` / `create` | `delete_active`, `delete_inactive`, `reuse_highest`, `retain_gap`, `ordinal_overflow` | Passed. No port forwards exist in this slice, so forwarding cleanup is not proved by these checks. |
| Rename and counter exhaustion fail explicitly | CLI `rename` / `open` | `rename_named`, `rename_unnamed`, `rename_invalid_name`, `rename_ordinal_overflow`, `count_overflow` | Passed without changing persisted data on failure. |
| Invalid or absent identifiers | CLI lifecycle commands | `invalid_id`, `close_invalid_id`, `delete_invalid_id`, `rename_invalid_id`, `snapshot_invalid_id`, `missing_session`, `snapshot_missing`, `close_missing`, `delete_missing`, `rename_missing` | Passed through the actual executable. |
| Malformed scalar, identity, name, scope, date, and count input | CLI reading an externally changed catalog | Each `bad_*` row case in `native/CMakeLists.txt`; `bad_count`, `duplicate_id`, `duplicate_ordinal`, `corrupted_store` | Passed; the original invalid file is retained. This tests real storage input, not a replaced internal implementation. |
| Unsupported envelope or future fields must not be discarded | CLI catalog mutation | `unsupported_version`, `unknown_field`, `bad_root_*`, `root_not_object`, `active_closed` | Passed. Migration from legacy `store.json` has not been implemented or asserted. |
| Lock contention, permission failure, filesystem errors, and path aliases | CLI real filesystem operations | `busy_store`, `failed_write`, `read_denied`, `read_directory`, `lock_denied`, `empty_profile`, `profile_is_file`, `relative_profile`, `symlink_profile`, `store_symlink` | Passed. Unix-only filesystem checks are not registered as substitute Windows tests. |
| Concurrent writes preserve all committed identities and numbers | Twelve concurrent real CLI processes | `concurrent_creates` | Passed. Only explicit `Busy` results, which prove no write occurred, are retried after competing writers exit. |
| Missing, unknown, conflicting, or repeated arguments; help/version | CLI argument parsing | `missing_command`, `unknown_command`, `conflicting_options`, `extra_arguments`, `repeat_profile`, `repeat_context`, `repeat_name`, `namespace_conflict`, `name_conflict`, `snapshot_name_conflict`, `extra_create_arguments`, `extra_rename_arguments`, `missing_profile`, `parse_failure`, `help`, `version` | Passed. |

Executed result: **115/115** CTest cases passed, concurrent execution with independent temporary profiles, **1.36 seconds** in the recorded final run. Native production source (`session_store.cpp` and `session_main.cpp`) reached **98.75% line coverage**, **95.99% branch coverage**, and **100% function coverage**. Both gates passed; this is not whole-product or legacy-suite coverage.

Remaining unexecuted guards include failures while protecting a newly created directory, opening/protecting an atomic-save temporary file, incomplete write/commit publication failure, the defensive error-enum fallback, and stdout write failure. Ordinary read, lock, malformed-input, and invalid-parent failures were exercised. The remaining guards depend on filesystem/media failure timing or a C++ enum value that this CLI cannot produce; no owned code path was mocked to manufacture coverage. These are explicit residual gaps, not an exhaustive-fault-injection claim.

The repeatable benchmark starts the actual CLI for each read of a catalog created by 16 real CLI `create` operations. The final recorded 50-sample warm run on macOS 27.0 / Qt 6.11.2 measured **p50 11.393 ms**, **p95 13.313 ms**, and **max 13.811 ms**. This measures process startup, local list execution, and client JSON validation. It does **not** measure cached Qt rendering, the 5,000-resource workload, cold app startup, Kubernetes latency, or any UI performance gate. The runner retains `coverage/benchmark.json`, `coverage/report.txt`, and merged profiles in its selected build directory while removing per-run raw profiles and temporary test catalogs.

No production/shared cluster, Docker container, credentials, browser login, or .NET companion was used. Full Qt UI, kubeconfig/authentication, Kubernetes requests/cache, log/YAML/forwarding behavior, legacy comparison, package-size/RSS/CPU gates, and non-macOS evidence remain pending.

### Native session contract correction (2026-10-02)

The initial 115-test result above is historical component evidence, not session-contract conformance. Six subsequent public-CLI regressions failed against that implementation: named suffixes, non-accumulating suffixes, independent sequences, renamed sequences, occupied labels, and 30-day usage ranking. The implementation now follows the accepted contract; obsolete lifetime-counter and global-number-exhaustion expectations were removed.

| Requirement | Public boundary | Executable evidence | Remaining gap |
| --- | --- | --- | --- |
| SES-001-003, SES-011 | CLI snapshot/list/open | `unchanged_snapshot`, `unnamed_snapshot`, `history_copy` | Actual imported source-change discovery and endpoint/auth binding |
| SES-005-007 | CLI snapshot/list | `named_suffix`, `successive_suffix`, `unnamed_snapshot` | Automatic names from imported context metadata |
| SES-008-012 | CLI open/close/selection; public clock boundary | `activate_idempotent`, `activate_switch`, `reopen_usage`, `usage_window`, `cutoff`, `cutoff_excluded` | Rendered selector integration |
| SES-013-014 | CLI selection | `recent_tie`, `used_before_unused`, `never_used_newest`, `id_tie`, `unused_id_tie` | Rendered selector integration |
| SES-015-016 | CLI list/open/selection | `stable_order` | Actual tab/window lifecycle |
| SES-017-019 | CLI snapshot/delete/rename | `reuse_highest`, `retain_gap`, `independent_sequence`, `renamed_sequence`, `occupied_suffix`, `rename_noop` | Rendered session actions |
| Native catalog compatibility | CLI create/selection | `old_schema`, `sequence_mismatch`, `selection_bad_store`, malformed-ingress cases | Verified legacy import and native schema-1 recovery tooling |

Verified locally: 135/135 tests passed; LLVM production coverage 98.92% lines, 94.95% branches, 100% functions. Run: `PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`. macOS 27.0 arm64, Qt 6.11.2, Release with coverage instrumentation. Warm 16-session CLI list benchmark: 50 samples, p50 12.129 ms, p95 14.976 ms, maximum 17.374 ms. This measures process startup and JSON validation, not Qt Quick rendering or the release performance profile.

Tests use real runtime processes and filesystem/locking. Exact cutoff tests additionally use the public reference-clock entrypoint; controlled timestamps are external clock/history ingress, not replaced internal implementations. Every test owns its temporary profile. Native test execution starts no containers and does not prune shared images. Forwarding, Kubernetes E2E, legacy comparison, UI performance and non-macOS support remain unverified. The previously documented atomic-write failure and platform-specific gaps remain.

### Native source/session workflow verification (2026-10-02)

This supersedes the earlier native runtime totals. The executable boundaries are real native CLI processes, actual filesystem/locking and supplementary public reference-clock calls. Neither the Qt Quick interface nor Kubernetes connections exist in this slice; these tests are not Kubernetes E2E or complete application parity.

| Contract or behavior | Public boundary | Tests | Remaining gap |
| --- | --- | --- | --- |
| ADR 0004: content-addressed source copies | Source CLI import/list | `native.sources.deduplicate`, `changed`, `different_path`, `normalized_path`, `deleted_original` | Watchers and folder/default/paste/generated import channels |
| SES-002: retain active binding after changed import | Source CLI to session CLI workflow | `native.sources.session_binding` | Actual endpoint/credential execution and rendered selection |
| SES-003: repeated changed configuration is idempotent | Session CLI snapshot/list | `native.contract.repeat_changed_snapshot`, `unrelated_equal_config` | Automatic source-change orchestration |
| SES-009-014: 30-day UTC window and ordering | CLI selection; public clock entrypoint | `usage_window`, `cutoff`, `cutoff_excluded`, `cutoff_timezone`, recency and ID-tie cases | Selector presentation |
| Source syntax, types, UTF-8 and BOM retention | Source CLI import/list | `native.sources.bom`, `invalid_utf8`, malformed-source cases | Effective connection-configuration validation |
| Credential configuration is metadata, not an import side effect | Source CLI import | `native.sources.no_exec`, `auth_*`, `success`, `server_credentials`, `server_query`, `server_fragment` | Auth transports, explicit login/renewal and relative credential/plugin paths |
| Broken or ambiguous source references are visible | Source CLI import | Missing-reference, server and duplicate context/cluster/user cases | Rendered source diagnostics |
| Recency without another source index | Source CLI import/list | `native.sources.recency`, `recency_tie` | Settings snapshot/explicit-refresh integration |
| Independent record failure does not suppress healthy sources | Source CLI list | `native.sources.isolated_corruption`, schema/hash/encoding/root/filename cases | Cache-first UI presentation of partial errors |
| Private local storage and access revocation | Source/session CLI list/import | `private`, `read_denied`, `directory_read_denied`, `profile_no_traverse`, `profile_read_denied`, `owned_no_traverse`, symlink and lock cases | Windows ACLs, other-platform execution, ancestor/ACL revocation and storage-fault integration |
| JSON response delivery fails explicitly | Both native CLIs | `output_write_failure`, `output_write_failure_large`, parser/help/arity cases | Interrupted-pipe signal policy and process-interruption lifecycle |

Failing-first checks exposed rejected UTF-8 BOMs, misleading empty-directory results, apparently usable ambiguous references, catalog-wide suppression after one damaged record, DST-dependent usage windows, falsely successful buffered output, repeated replacement duplication and inaccessible-profile reads. Their corrections are now exercised through the public boundaries above. CLI output/error formatting has one shared owner; coverage includes production headers as well as all production `.cpp` files.

Final local run: **232/232 passed**, 99.03% production lines, 95.28% branches, 100% functions. Per-file runtime coverage includes `kubeconfig_store.cpp` (99.03% lines / 94.04% branches), `session_store.cpp` (98.81% / 94.55%), both CLI entrypoints and shared output code (100% / 100%). Platform: macOS 27.0 arm64, Qt 6.11.2, yaml-cpp 0.9.0, Release with LLVM instrumentation. Command: `PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`. Captured evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-final-run.txt`; profiles and reports are in that build's `coverage` directory.

Warm CLI list process benchmarks include startup, actual store reads and JSON validation. Each uses 16 records and 50 samples: sessions p50 11.849 ms, p95 13.487 ms, maximum 13.692 ms; owned source snapshots p50 13.122 ms, p95 13.755 ms, maximum 14.170 ms. These are neither a UI frame measurement nor proof of release performance or improvement against the legacy GUI.

Ordinary local-file tests do not exercise every protection/open/write/commit failure, owned-file mid-read failure or lock-permission failure; isolated filesystem/volume/ACL fault evidence remains required. The defensive unsupported-enum branch has no external CLI producer. Unsupported native session schema 1 is preserved, not reconstructed into invented event history; recovery and legacy migration remain separate gaps. Other platforms are not passed by omitting their permission tests.

Each test owns its temporary profile, and the script deletes only its fresh raw-coverage directory. Permission-failure tests restore directory access even when their assertions fail. A directory left by the original failing read-permission regression was restored and removed. No containers were started, no shared images were pruned and no production/shared cluster was contacted. Full legacy tests still require correction of their unsafe stack-cleanup script. Qt Quick behavior, Kubernetes E2E, all remaining capabilities, installed-artifact performance, migration/rollback and platform/distribution evidence remain open gates.

### Native Qt Quick read-workspace verification (2026-10-02)

This is the first independently runnable native application workflow, not full application parity. QML runs through its actual controls and public mouse/keyboard interactions; source/session stores, filesystem, transport, queue and cache are real. Only the external Kubernetes HTTP boundary is simulated for deterministic failures. The separate real Kubernetes runner uses an actual local k3s API, CA and client certificate, not that boundary simulator.

| Contract / scenario | Public boundary | Executable evidence | Remaining gap |
| --- | --- | --- | --- |
| Empty startup presentation and explicit profile | Native application CLI; visible QML controls | `native.application.{help,version,missing,relative,repeated,extra}`, `native.ui.empty` | Packaged module failure, startup persisted-cache restoration, windows/settings and mobile platforms |
| File import to actual owned context/session/table | QML file-path input, Import and Open context | `native.ui.success`, `relative_token`, `basic` | Folder/default/paste/generated/watch imports, source/session editing and removal |
| External API discovery and complete paginated collection | QML resource table against HTTP boundary | `success`, `missing_kind`, `repeated_page`, `invalid_list`, `invalid_discovery` | New/removed discovery membership, scope/RBAC catalog, independent discovery TTL, page-version and duplicate-UID reconciliation |
| Static credential resolution and fail-closed unsupported auth | QML import/open to actual connection resolver | `relative_token`, `basic`, `empty_token`, `plugin`, `invalid_ca`; real Kubernetes client-certificate workflow | Valid relative CA/key/cert files, TLS rejection matrix, token rotation, other credential validation failures, exec/auth-provider/proxy/impersonation and compression/environment behavior |
| Distinguishable failure without clearing healthy collections or leaking bodies | QML status and resource table | `auth`, `forbidden`, `redirect`, `malformed`, `rate_limit`, `retain` | Confirm/cancel/re-fail authentication, cross-session credential suspension, backoff timing/date bounds and recovery, connection/TLS/other HTTP faults |
| Whole-row inspector and right-click target | Actual QML row and context-menu clicks | `success`, `context_menu` | Full inspector catalog, fresh YAML/edit protection, navigation/history, Secret reveal/copy, actions/events/logs/metrics |
| Local tri-state sort, filter and independent copy | Header, filter field and keyboard copy | `sort`, `filter`, `copy` | Every column/kind, semantic numeric/temporal values, keyboard overflow, saved/reordered/hidden/pinned layouts, persistent session filters/sorts and tab-isolation checks |
| Sent read survives tab close without unrelated presentation | Row click, delayed HTTP detail, Close tab | `slow_close` also checks that the external socket received its delayed response instead of an early disconnect | Original cache reuse after reopening, queued-obsolete work, priority/coalescing order, multiple tabs/windows and forwarding ownership |
| Real Kubernetes read through the native UI | `sh scripts/test-native-kubernetes.sh`; QML Import/Open/filter/row click | Marker ConfigMap appears and a fresh metadata detail arrives over verified TLS with client-certificate authentication | Legacy parity on the same scenario, resource mutation/CRDs/RBAC/metrics/actions/streaming and real platform-native populated-window accessibility |

Failing-first evidence included a JSON-reference lifetime crash in pagination, missing optional continuation handling, reversed `NONE` ordering, and a clipped empty-state message found in native macOS presentation. The first real Kubernetes run rejected genuine list objects because item `kind` was omitted and resource names included RBAC colons; the shared API ingress was corrected. A later real run exposed typing a filter before asynchronous session activation; the control now becomes available for its actual target session. These are corrections to the native implementation, not changes to the confirmed requirements.

Final deterministic suite: **260/260 passed**, **5.39 seconds**, macOS 27.0 arm64, Qt 6.11.2, yaml-cpp 0.9.0, Release with LLVM instrumentation. Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-read-checked-run.txt`. Whole native production coverage, including the real application entrypoint, is **95.73% lines / 80.44% branches**. The branch gate **fails** and `scripts/test-native.sh` correctly returns nonzero. No runtime file was excluded to conceal the gap. Notable gaps are connection-material failures, queued/read/backoff and multi-session lifecycle branches, source/session read/mutation failure presentation, model boundary contracts and packaged-engine failure. Their public checks remain required before this runtime's quality gate is passed.

Real Kubernetes execution and cleanup evidence is recorded separately from the deterministic run. The initial k3d attempt failed before node creation while retrieving its tools node and rolled back its network; that is not a k3d pass. The replacement runner owns one loopback-only k3s container and its anonymous volumes; it checks cleanup and retains the pinned `rancher/k3s:v1.35.5-k3s1` image without pruning unrelated images. The frozen legacy source/build/runtime copies remain unchanged. No production/shared cluster or user kubeconfig is used.

Native macOS GUI smoke also showed the distinct `dev.podlord.native.reference` bundle, accessible import/context/session/filter/header controls and the empty private profile; it was closed normally. This is not a populated-cluster macOS accessibility audit, a full old/new comparison, a release package or a 5,000-resource performance result. CLI process benchmarks remain narrower observations and are not used to claim UI gains. All uncompleted migration/rollback, full behavior, cleanup interruption, performance and supported-platform gates remain open.

Final real local Kubernetes run against the same native build: **passed**. Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-read-final-real.txt`. The owning container was removed, and a subsequent scoped container/network check found no task-owned cluster containers or the earlier k3d network. The real run is not included in the deterministic LLVM percentages above; profiles from different earlier implementation revisions must not be blindly merged.

### Native confirmed-authentication verification (2026-10-03)

This supersedes the native deterministic totals above, not the remaining migration gates. The new checks drive actual QML buttons and real credential child processes, HTTP transport, source/session stores and caches. Only the external identity provider and the deterministic Kubernetes HTTP boundary are simulated. The real Kubernetes runner instead uses an actual local k3s API with verified CA and client-certificate authentication.

| Requirement / scenario | Public boundary | Executable evidence | Remaining gap |
| --- | --- | --- | --- |
| ERR-005, STR-005: initial and restored sessions require consent | QML Import/Open/Authenticate/Cancel; restored workspace public entrypoint | `native.authentication.cancel_confirmation`, `restore`, `success` | Full restored-window/workspace state, exact dialog target changes and native platform accessibility |
| Versioned exec command, literal args, original directory and environment | QML confirmation to real external process and HTTP authorization | `success`, `v1beta1`, `relative_command` | PATH lookup, inherited environment/platform collisions, installHint presentation, other field-validation paths |
| Unsupported or conflicting ingress fails without execution | QML import/open | `invalid_mode`, `requires_stdin`, `invalid_args`, `invalid_env`, `mixed_credentials` | Terminal input and reserved cluster config extensions remain unsupported; broader malformed-field matrix required |
| Credential output cannot authorize malformed reads | QML confirmation, process output, API request observation | `invalid_json`, `wrong_version`, `missing_status`, `no_credentials`, `bad_token`, `expired`, `bad_date`, `failed_exit`, `failed_start`, `large_output` | Certificate-pair rejection matrix, UTF-8/root/status/value types, process crash/I/O faults and stderr/output flooding |
| ERR-004-006: expiry/re-failure cannot silently invoke login | QML Refresh/Authenticate; external request/process counts | `retry_401`, `expiry`, `shared_401` | Independent credential isolation, delayed old-401 race, rate-limit interaction, real browser OAuth and central sync integration |
| ERR-005-006, ERR-014: one login for shared tabs | QML confirm, context selection, Open context and loading controls | `shared_running` | Multiple windows, scope variants and closure/selection races |
| ERR-011: provider completion is not API recovery | QML status and cached table after another 401 | `retry_401`, `shared_401` | Delayed successful-check status, exact selection/scroll retention |
| Cancel and shutdown own their credential process | QML Cancel login; workspace/application lifetime and external OS process boundary | `cancel_running`, `shutdown_running` | Full packaged-app termination/interruption; provider-launched detached descendants are not claimed as owned |
| Process-lifetime approval and cache survive reopening | Public workspace Close/Open with real stores and cached table | `cache_reopen` | Actual tab control and large-cache performance |
| Confirmed exec certificate reaches actual Kubernetes | Real local runner; QML import/open/confirmation/filter/inspector | `authentication_ui_test real`, alongside `workspace_ui_test real` | Actual vendor provider/browser, rejected/rotated certificates, legacy-side same-workflow parity |

Final deterministic run: **287/287 passed**, **9.85 seconds**, macOS 27.0 arm64, Qt 6.11.2, yaml-cpp 0.9.0, Release with LLVM instrumentation. Command: `PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`. Captured output: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-auth-final-run.txt`. Whole native production coverage is **96.00% lines / 80.18% branches**; the 90% branch gate still fails and the script returns nonzero. All production source/header files remain in the report. The real Kubernetes checks are separate evidence and are not included in these deterministic percentages.

The initial external-process tests exposed their own incorrect comparison of macOS `/tmp` and `/private/tmp`; canonical path comparison corrected that test ingress. A missing Qt key header was a build error and was corrected. A shared login now refreshes the visible matching session rather than only the original hidden tab. These are implementation/test corrections, not specification changes.

Both real local Kubernetes checks passed with the same production source revision. Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-auth-real-run.txt`. The owning container was removed; a subsequent scoped check found none. Temporary credential files and profiles are private and owned by each executable. The runner keeps only its pinned cluster image and does not prune unrelated Docker state. Frozen legacy source/build/runtime references remain untouched. Full migration, browser/terminal/provider compatibility, central synchronization, UI performance, 95/90 quality gates, platform distribution and actual old/new parity remain incomplete.

The runner now records the owning container's actual anonymous volume names before removal and verifies their absence afterwards, rather than assuming that a label-only volume query covers them. Repeated real static/exec Kubernetes execution with these checks **passed**: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-auth-cleanup-run.txt`. Only the current run's container and identified volumes are removed; no global container/image/volume pruning is used.

## Native Synchronization And Cache Evidence (2026-10-03)

[ADR 0020](../adr/0020-native-sync-cache-ownership.md) owns this increment's scope. New behavior tests use real settings/profile storage, real native code and a local fake only at the external Kubernetes HTTP boundary. Each case owns its temporary profile and server.

| Behavior | Executed public boundary | Test name | Remaining gap |
| --- | --- | --- | --- |
| Save and restore request/inactive limits | Actual QML settings dialog and reopened profile | `native.ui.settings_save`, `native.ui.settings_restore` | Full legacy settings import and other native settings |
| Reject corrupt configuration without requests or overwrite | QML import/open with invalid profile settings | `native.ui.settings_invalid` | Complete parser failure matrix |
| Reject concurrent and stale settings replacement | QML Save with real file lock or another public store write | `native.ui.settings_busy`, `native.ui.settings_conflict` | Multi-process interactive workflow |
| Enforce configured spacing | QML import/reads observed at local HTTP ingress | `native.ui.limit` | Shared budget across independent windows/controllers |
| Prioritize inspector ahead of queued pagination | Actual QML resource-cell click and HTTP request order | `native.ui.priority` | All endpoint/page failure combinations |
| Coalesce repeated inspector clicks | Actual QML clicks while external detail response is delayed | `native.ui.coalesce` | Broader cross-session/parameter matrix |
| Expire list/detail retention without renewing timestamps | Actual QML Refresh after an externally controlled UTC clock advances | `native.ui.cache_expiry`, `native.ui.detail_expiry` | Mixed collection age and wall-clock discontinuities |
| Reuse collection discovery during background reads | Public native controller tick and real HTTP ingress | `native.ui.sync` | Independent discovery TTL and membership reconciliation |
| Execute owned periodic synchronization | Real controller timer, public source connection and HTTP ingress; no manual tick | `native.ui.periodic_sync` | Visible QML automatic-update and idle/focus cadence matrix |
| Keep configured inactive-session cadence independent | Public controller with two real source contexts and observed HTTP authorizations | `native.ui.inactive_sync` | Full multi-window lifecycle |
| Finish a sent read without restarting closed-session follow-ups | QML tab close during delayed external discovery response | `native.ui.hidden_followup` | Every in-flight endpoint type |
| Disconnect callbacks and close transport when owner is destroyed | Public controller destruction during delayed HTTP response | `native.ui.shutdown_pending` | Packaged-application interruption and shutdown |
| Render markup literally without image fetching | Actual QML table render, hover and keyboard focus with external resource markup | `native.ui.markup` | Native accessibility and full presentation audit |

The exec-expiry fixture now permits three seconds for genuinely spaced discovery requests before expiration; it still verifies suspension and cache availability. No runtime deadline or automatic authentication retry was added.

Command: `PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`. Latest evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-sync-safe-run.txt`. Result: **303/303 cases passed**, 45.04 seconds; production line coverage **96.11%**, branch coverage **79.41%**. The command correctly exits **1** because the 90% branch gate fails. Production sources and headers remain in the report; passing cases do not imply all reachable paths are covered.

Warm CLI benchmarks from that same run measured session-list p50/p95 of 11.432/13.139 ms and source-list p50/p95 of 15.276/15.832 ms (16 records, 50 samples). These are CLI boundaries, not UI rendering, legacy-comparison or memory evidence.

Real Kubernetes static/confirmed-exec workflows after the new gate and synchronization implementation passed in `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-sync-real-run.txt`. The final current-build run, including presentation safety changes, also **passed**: `PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native-kubernetes.sh`, evidence `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-sync-final-real.txt`. The runner removed its owned container `podlord-native-run-zrwupx` and checked actual anonymous-volume removal. No production/shared cluster or global prune is used.

The requirement ledger remains open for discovery freshness, API membership changes, the remaining settings/capabilities, legacy parity, failure-path coverage, UI performance, accessibility, platform packaging and release evidence. No complete SYN or legacy-capability conformance is claimed.

## Native Read-Settings Boundary Evidence (2026-10-03)

These forty cases exercise `ReadSettingsStore::load/save` with actual isolated local filesystem input. They supplement, not replace, the QML settings cases above. No owned storage or locking implementation is faked. Each CTest case has its own temporary profile and can run independently.

| Behavior | Public entrypoint | Test names under `native.settings.` | Remaining gap |
| --- | --- | --- | --- |
| Missing settings retain actual defaults without creating a profile | Store load | `missing` | Full application startup and legacy settings migration |
| Reject invalid profile and creation paths | Store load/save | `empty_profile`, `relative_profile`, `profile_file`, `parent_file` | Platform-specific filesystem errors |
| Validate input ranges before writing | Store save | `invalid_limit_low`, `invalid_limit_high`, `invalid_inactive` | Other settings remain outside this store |
| Round-trip zero and maximum supported values | Store save/load | `zero`, `maximum` | Controller cadence with extreme intervals |
| Create private settings/profile permissions | Store save plus actual filesystem permissions | `private` | Non-Unix ACL behavior |
| Preserve state on stale and concurrent updates; allow reuse | Store save/load | `conflict`, `repeat`, `concurrent` | Concurrent packaged windows/processes; this case uses real threads and locks |
| Distinguish occupied lock, invalid lock path and denied writes | Store save with real held lock, directory or permissions | `busy`, `lock_io`, `lock_denied` | Disk exhaustion and publication failure after acquiring a valid lock |
| Reject settings directory and oversized input without replacement | Store load/save | `settings_directory`, `oversized` | Resource exhaustion outside the input-size cap |
| Reject malformed JSON and invalid root/schema | Store load/save | `malformed`, `empty_document`, `array_root`, `scalar_root`, `extra_key`, `missing_key`, `version_string`, `version_future` | No unsupported-version migration is authorized |
| Reject typed/range errors without overwriting original bytes | Store load/save | `limit_string`, `limit_negative`, `limit_fraction`, `limit_high`, `limit_null`, `inactive_string`, `inactive_negative`, `inactive_fraction`, `inactive_high`, `inactive_null` | No additional field types are supported |
| Reject symbolic links and denied reads | Store load/save with real filesystem input | `profile_symlink`, `settings_symlink`, `read_denied` | Filesystem replacement races and non-Unix behavior |

The first focused execution passed 38/39 cases and failed `native.settings.lock_io`: an actual directory at the lock path incorrectly produced `Busy`. Distinguishing only Qt's error enum did not fix the regression; the first full execution still failed that same case. Explicit directory rejection plus classification of other lock I/O failures corrected the owner. The regression expectation was not weakened.

Command: `PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`. Current evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-settings-checked-run.txt`. **343/343 cases passed** in 42.40 seconds. Whole-production coverage is **96.22% lines / 80.70% branches**; the script correctly exits **1** for the remaining branch gate. `read_settings.cpp` itself measures **98.04% lines / 91.67% branches**. Atomic publication failure, disk-full writes and newly-created-profile chmod failure are not reproduced by these local cases; they remain explicit unexecuted error paths, not claimed unreachable or covered.

Same-run CLI benchmarks: sessions p50/p95 11.367/13.060 ms; source snapshots 15.327/15.665 ms. These are process/list boundaries with 16 records and 50 samples, not UI or comparative performance evidence. Deterministic raw diagnostic profiles are removed after recording the reports.

Current-build real Kubernetes static and confirmed-exec certificate workflows **passed**: `PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native-kubernetes.sh`, evidence `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-settings-real-run.txt`. The runner removed its own container `podlord-native-run-wshlzm` and verified actual anonymous-volume cleanup. No production/shared cluster or global resource prune was used.

## Which native Pod-log behaviors have executable evidence?

Decision: [native Pod-log ownership](../adr/0021-native-pod-log-ownership.md).
Public entrypoint: real Qt Quick controls through `pod_logs_ui_test`, with a local
HTTP server only at the external Kubernetes boundary. Stores, request queue,
authentication, cache, model and timers are not replaced.

- `native.logs.all`, `single`, `select`, `literal_all`: default aggregate and individual selection, exact per-container requests, nanosecond ordering, visible origin, and a real container named `all` distinct from the aggregate option.
- `partial`, `malformed`, `invalid_utf8`: successful history survives named container failures and invalid response data.
- `auth`, `backoff`: credential suspension without automatic retry and shared server Retry-After admission.
- `pause`, `cadence`, `repeat`: paused automatic work stops, resume requests foreground data, automatic cycles remain at least three seconds apart, repeated boundary occurrences do not grow history.
- `late`, `queued_select`, `hide_window`: obsolete unsent reads are removed; sent reads finish at the original boundary without updating a hidden view; reopening displays retained cache immediately.
- `resource_refresh`, `recreate`: logs do not coalesce away explicit resource refresh; a new Pod UID cannot inherit the previous UID's entries or be hidden behind stale detail metadata.
- `detail_recreate`, `detail_clock_same`, `detail_clock_backwards`: a newer inspector response replaces older list metadata, including equal or backward wall-clock observations.
- `detail_expiry`, `open_detail_recreate`, `pagination`: detail expiry does not resurrect an older listed UID; a fresh GET changes an already-open log owner; terminal pages preserve the first-page observation order instead of hiding a later inspector response.
- `priority`, `negotiation`: explicit foreground log reads bypass a 64-resource discovery backlog; requests reuse the accepted API media negotiation while successful response bodies remain plain-text streams. Both failed before their respective fixes.
- `scroll`, `eviction`, `expiry`: real wheel/key interaction preserves a retained reading anchor, explicit live-follow reaches the end, size/TTL removal keeps follow off and explains the actual removal cause.
- `invalid_zero`, `invalid_negative`, `invalid_fraction`, `invalid_empty`, `invalid_text`: invalid MB input leaves the old setting intact and displays a validation error.
- `limit`, `restore`: lowering the persisted budget trims oldest history immediately, full-entry copy is not the clipped label, and a new controller reads the saved limit.
- `native.settings.log_zero`, `log_negative`, `log_fraction`, `log_string`, `log_high`, `log_null`, `log_missing`, `version_one_upgrade`: persisted version-two validation and atomic version-one upgrade through the public settings store.

The default-log feature, dispatch-based cadence, stale Pod-UID handling, hidden-window
polling and cache-expiry notice were reproduced by failing tests before their
respective fixes. The literal-container-name test also failed before separating
selection identity from the visible `all` label.

Latest deterministic command:
`PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`.
Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-uid-authority-checked-run.txt`.
Result: **387/387 passed**, 78.24 seconds, **96.29% production lines / 79.95% branches**,
with production sources and headers included. The command exits **1** because the
required **90% branch gate still fails**. QML behavior is exercised, not represented
as C++ line coverage. This result does not prove full migration or release readiness.

Remaining log evidence: complete public admission/error matrix; exact incremental
query and timestamp-format variants; per-Pod/per-session selection and position
restoration; paused/in-flight cache-expiry interactions; container restart/rotation;
budget increase and packaged-app restart; external-markup network-negative checks;
keyboard/accessibility audit, frame/RSS benchmarks and frozen-legacy comparison.
A clipped list label and retained-history budget do not establish a total RAM cap.

The real runner rejects remote Docker endpoints before creating resources and pins
the resolved local socket for the whole run. A negative public script check with
`DOCKER_CONTEXT= DOCKER_HOST=ssh://must-not-connect.invalid sh scripts/test-native-kubernetes.sh`
exits 1 with an explicit local-socket requirement before connecting. Local image
archives are loaded using the documented [K3s image-import directory](https://docs.k3s.io/add-ons/import-images),
not a presumed `k3s ctr` subcommand in the pinned container image.

Metadata-ordering correction: `native.logs.detail_recreate` first failed because
fresh inspector containers were hidden by an older listed UID, producing real
HTTP 400 responses at the external boundary. The existing resource cache now
owns accepted metadata order using process-local monotonic observations; list
collections still own table membership. [ADR 0022](../adr/0022-native-resource-metadata-authority.md)
records the invariant. Seven focused public-QML scenarios passed in 28.94 seconds:
`recreate`, `detail_recreate`, `detail_clock_same`, `detail_clock_backwards`,
`detail_expiry`, `open_detail_recreate`, `pagination`. These checks cover both
receipt directions, clock adjustment, detail-lease expiry, owner replacement and
first-page ordering. They do not establish malformed-metadata conformance,
every paused/in-flight lifecycle, discovery reconciliation or legacy parity.

Local runtime constraint: the real Pod fixture encountered DiskPressure with
about four GiB free in Colima. The runner changes only its own ephemeral node's
hard disk thresholds to 512 MiB, preserving the 100 MiB memory and five-percent
inode thresholds. It does not change Colima configuration or delete shared images.
See the [Kubernetes eviction threshold rules](https://kubernetes.io/docs/concepts/scheduling-eviction/node-pressure-eviction/).
The existing pinned image archive is seeded before starting the owned container,
and an explicit test ServiceAccount avoids default-account bootstrap timing.
Token automount is disabled on both that account and its Pod. These constraints
are test-fixture configuration, not production storage-pressure coverage.

Current-build real Kubernetes workflows **passed** after the metadata-authority,
foreground-queue and media-negotiation fixes:
`PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native-kubernetes.sh`.
Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-uid-authority-real-run.txt`.
The native Qt Quick UI, running offscreen with the software backend, imported a
real private client-certificate kubeconfig, exercised confirmed exec credentials,
selected the exact Pod among ten name-matching resource rows, and displayed both
`podlord-alpha-real-log` and `podlord-beta-real-log` with their container origins.
The runner exited **0**, removed its own container `podlord-native-run-dglptk`,
and checked that its labelled container and actual anonymous volumes were absent.
All failed attempts also ran scoped cleanup. The pinned cluster image remains;
no global Docker prune or production/shared Kubernetes access occurred.
This proves these workflows, not native-OS accessibility, GPU performance, all
log failure paths or complete legacy parity. Metadata-ordering behavior is proved
by the deterministic public-QML regressions above, not by this real-cluster run.
All three offscreen processes also reported a 31 ms font-family alias lookup for
`Sans Serif`; native font selection and measured startup/frame/RSS performance
remain explicit gaps, not inferred gains from the rewrite.

## Which native read-only inspector behaviors have executable evidence?

Decision: [native inspector document boundary](../adr/0023-native-inspector-document-boundary.md).
Public entrypoint: actual Qt Quick controls in `workspace_ui_test`, backed by the
real workspace, settings, stores, request queue and resource cache. A local HTTP
server substitutes only for the external Kubernetes API, not owned services.
The following 32 added `native.ui` scenarios provide scoped evidence for
INS-004-007, read-only INS-008/009 behavior and individual Secret presentation
requirements. They do not establish editing/apply or complete Secret safety.

- `inspector_yaml`, `inspector_cached`: actual GET documents, immediately available retained YAML, fresh replacement, visible resource name while waiting, scoped timestamps/loading status, read-only controls, quoted strings preserving scalar types and visible-YAML copy.
- `inspector_scroll`: real wheel interaction preserves the same resource's settled reading position when fresh content replaces its cached snapshot.
- `inspector_failure`, `inspector_malformed`, `inspector_forbidden`: HTTP 500, invalid JSON and HTTP 403 retain the accepted document; diagnostics do not expose response-body Secret values.
- `inspector_auth`, `inspector_redirect`, `inspector_backoff`: authentication suspension without automatic retry, rejected redirect and shared Retry-After admission at the public inspector boundary.
- `inspector_expiry`, `inspector_close_late`: detail lease expiry clears visible content; a sent read finishes without reopening or populating the closed inspector.
- `inspector_secret_hidden`, `inspector_secret_reveal`, `inspector_secret_copy`, `inspector_secret_yaml_copy`: default masking includes encoded YAML and duplicated last-applied content; reveal affects only one Values entry; individual copy does not implicitly reveal; copying YAML after a reveal still returns the masked document.
- `inspector_secret_reset`, `inspector_secret_session`, `inspector_secret_resource`, `inspector_secret_window`, `inspector_secret_tab_close`: explicit reveal resets on inspector close, identical-document session changes, resource changes, hidden windows and tab closure.
- `inspector_secret_refresh`: a metadata-only refresh preserves the revealed value and actual text selection, while the other value remains hidden. Native Qt model behavior suffices; no separate selection cache was added.
- `inspector_secret_binary`, `inspector_secret_stringdata`: non-text encoded content remains labelled base64 without lossy conversion; individual stringData reveal/copy works through actual virtualized controls and keyboard navigation.
- `inspector_secret_invalid_encoding`, `inspector_secret_invalid_field`, `inspector_secret_invalid_key`, `inspector_configmap_invalid_value`: invalid encoded data, non-object value maps, invalid keys and non-string ConfigMap values are rejected before replacing accepted content.
- `inspector_configmap`, `inspector_markup`: ordinary ConfigMap values are directly readable/copyable, including an offscreen virtualized row; markup is literal text rather than an image-fetch surface.
- `inspector_custom_secret`, `inspector_wrong_version`, `inspector_list_wrong_version`: API version participates in resource identity; another API's kind named Secret keeps its own schema and YAML, while inconsistent detail/list versions cannot replace accepted data. Missing per-item type metadata is inferred from the authoritative collection path.

Failing-first evidence includes the initially absent YAML surface, same-document
session redaction, custom-kind collision, wrong detail/list API versions,
missing cached resource name and whole-YAML Secret disclosure after individual
reveal. The last regression failed in 3.28 seconds before always masking Secret
YAML. Seven focused checks then passed in 26.71 seconds: YAML, scroll, reveal,
metadata-only refresh, same-document session reset, custom Secret and YAML copy.
An initial QJson temporary-reference crash and unsupported QML key-handler names
were repaired, not papered over with alternate implementations. The existing
priority fixture was corrected to emit its actual discovered API version; no
production compatibility fallback was added for invalid data.

Final deterministic command:
`PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`.
Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-inspector-final-checked-run.txt`.
Result: **419/419 passed**, 106.12 seconds, **96.77% production lines / 80.33% branches**,
including production headers. The script correctly exits **1** because the
required 90% branch gate still fails. Executed QML interactions are not counted
as C++ line coverage. This is not full migration or release evidence.

Same-run public CLI benchmarks, 16 records and 50 samples: session listing
p50/p95 **12.142/12.517 ms**; owned source snapshots **17.211/17.830 ms**.
These include process startup and output validation. They are neither UI/frame
measurements nor legacy-comparison evidence.

Current-build real Kubernetes command:
`PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native-kubernetes.sh`.
Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-inspector-final-real-run.txt`.
Result: **exit 0**. The actual Qt Quick UI imported a private client-certificate
kubeconfig, read real ConfigMap YAML/values, inspected a real Secret with default
masking and individual reveal/copy, exercised confirmed exec certificates, and
displayed both real multi-container Pod log origins. The runner removed its
owned container `podlord-native-run-brvugb` and verified its labelled container
and actual anonymous volumes were absent. The pinned local image remains; no
global prune, shared-resource deletion or production cluster access occurred.

The actual QQuickWindow screenshot
`/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/e2e/native-inspector.png`
was visually inspected: resource name, scoped detail timestamp, masked alpha/beta
YAML and explicit unavailable-editing notice are visible. It uses the offscreen
software backend, not the native GPU compositor or a screen reader. Those
processes still report **30-33 ms** for the missing `Sans Serif` family alias;
font selection and measured startup/frame/RSS improvement remain open.

Remaining reachable evidence and implementation gaps:

- Protected fresh-baseline editing and local discard guards now have a separate evidence section below. Diff and confirmation, apply authorization, conflict recovery and uncertain-write read-back remain unimplemented. Original editing documents, never masked YAML, must own future write payloads; SEC-008 payload preservation is not proved.
- Complete public detail admission/error and queued-before-send cancellation matrices remain unexecuted. The late-close case proves sent-read completion only. Empty value copy, every encoding/control-character variant, long-value preview/hover/full-copy limits and the complete reveal/lifecycle matrix remain incomplete.
- Secret response-body diagnostics have explicit negative cases, not an exhaustive audit of all logging, export, error and write paths. A masked visible document does not prove a total resource-cache RAM budget.
- Discovery freshness and membership reconciliation, operational writes/deletion/forwarding, remaining settings/auth/source channels and multi-window request ownership remain migration work.
- Native keyboard/screen-reader/contrast audits, large-document and 5,000-resource measurements, UI/frame/idle-CPU/RSS targets, dual-lane legacy parity, migration/rollback and supported-platform packaging/release gates remain open. No capability-ledger row is declared fully migrated by this increment.

## Which protected native YAML editing and discard paths are exercised?

Contract: INS-008-015, with local baseline ownership defined by
[ADR 0008](../adr/0008-protect-yaml-editing-and-apply.md). Entry: actual Qt Quick
Edit YAML, textarea, refresh, resource row, session/context, tab and native
window-close actions in `workspace_ui_test`. The external Kubernetes HTTP
boundary alone is substituted. No native apply transport is implemented.

Thirty added `native.ui.inspector_edit_*` scenarios:

- `fresh`, `cached`: cached/queued YAML is read-only; successful current-scope GET completion enables an explicit edit start without another request. Reopening from cache waits for the new read rather than borrowing old permission.
- `failure`, `malformed`, `auth`, `identity`: HTTP 500, invalid response data, authentication rejection and missing resource version leave editing unavailable while retaining readable cached data. Authentication is not retried automatically.
- `same_version`, `refresh`, `expiry`: equal-version successful reads still count as fresh completion; newer versions are indicated without replacing the draft; expiring the passive detail cache does not erase the editing baseline or text.
- `secret`, `empty`, `revert`: editing starts from masked Secret presentation, an empty local draft remains protected, and restoring the original text removes the unapplied-change guard. These are not Secret write-payload tests.
- `clean`, `clean_tab`: clean editing can close without a discard question and becomes read-only before asynchronous tab closure can admit another edit.
- `close_stay`, `close_accept`, `close_escape`, `close_keyboard`: Stay/Escape preserve the current draft; Discard closes the inspector without a write; the initially focused Stay button has a visible native focus cue and keyboard activation does not discard the text.
- `resource_stay`, `resource_accept`, `resource_bound`: resource change waits for confirmation; the originally requested path survives a subsequent filter/row change instead of being retargeted by its old index.
- `window_stay`, `window_accept`, `tab_stay`, `tab_accept`: actual native window and tab-close boundaries preserve the draft unless discard is explicitly confirmed.
- `session_stay`, `session_accept`, `open_context_stay`, `open_context_accept`: switching existing sessions and opening another context use the same guard before catalog mutation or retargeting.
- `late`: an earlier resource response cannot enable editing for a newly selected resource whose own fresh response is still pending.

These cases assert external observed HTTP methods remain GET: leaving, closing,
discarding, editing and refresh never automatically send a YAML write. They
exercise local editing only, not INS-016-028 or a complete YAML apply capability.

Failing-first evidence: the absent edit surface failed `fresh` in 2.28 seconds.
The first new dialog run had 14/28 failures and a real implicit-width binding
loop; fixed native dialog geometry removed that loop. Added `clean_tab` and
`close_keyboard` regressions both failed before clean-transition freezing and
explicit Stay focus. A stronger visual-focus assertion then failed in 8.45
seconds before using the native keyboard focus reason. No custom key handler
or parallel dialog implementation was added to production.

Final whole-production command:
`PODLORD_TEST_JOBS=2 PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native.sh`.
Evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-yaml-edit-final-isolated-run.txt`.
Result: **449/449 passed**, **263.63 seconds**, **96.73% production lines / 80.18% branches**.
The script exits **1** for the remaining 90% branch gate, not for failed scenarios.
Production sources/headers are included; QML execution is not C++ line coverage.
Same-run process/list benchmarks, 16 records and 50 samples: sessions p50/p95
**11.446/13.389 ms**, owned source snapshots **15.527/16.345 ms**. No UI or legacy
performance gain is inferred from these CLI measurements.

### What test reliability remains unexplained?

The earlier four-job run passed 449/449 in 133.25 seconds
(`native-yaml-edit-checked-run.txt`). A later four-job run failed 18/449 in 332.77
seconds (`native-yaml-edit-final-checked-run.txt`), including existing auth,
invalid-list and rate-limit cases. Seven isolated checks then had three auth
failures (`native-yaml-edit-failure-isolation.txt`); reduced concurrency alone
did not eliminate the symptom. Font-alias work in those failures measured
180-252 ms, versus about 30 ms in other runs. Host CPU activity was also observed,
but neither observation establishes a causal explanation.

After adding phase diagnostics, three focused auth cases passed in 4.91 seconds,
then each passed five repetitions in 23.46 seconds total
(`native-yaml-auth-diagnostic.txt`, `native-yaml-auth-repeat.txt`). The final
two-job suite passed. Diagnostics report phase, readiness/state flags and external
call counts, not credentials, response bodies or provider stderr. A failed test
has not yet been captured with a sufficient phase diagnosis to explain the
intermittent failures. This remains a verification gap; timeouts, authentication
behavior and the runner's default parallelism were not changed to hide it.

### What did the actual local cluster prove?

Command:
`PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core sh scripts/test-native-kubernetes.sh`.
Final evidence: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/native-yaml-edit-final-real-run.txt`, **exit 0**.
The actual client-certificate UI read a real ConfigMap, began a local YAML draft,
preserved it through a real refresh, kept it on Stay, then discarded it and
returned to the freshly read server document. The later real Secret workflow
kept YAML masked and revealed/copied only the selected value. Confirmed exec
certificate authentication and both real container log origins also passed.
The runner removed `podlord-native-run-vcc97c` and verified owned-container and
actual anonymous-volume cleanup. It retained the pinned image without global
pruning or access to production/shared clusters.

An initial new real run failed without phase diagnostics and removed its own
container `podlord-native-run-vldumn`. Two further runs passed and cleaned up
`podlord-native-run-hmo336` and `podlord-native-run-0qohwo`; the final run above
includes the visible-focus correction. The initial failure is not claimed fixed
merely because later runs passed. The real workflow now has stage diagnostics
and an optional failure image to improve the next reproduction.

Actual current QQuickWindow images were visually inspected:
`/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/e2e/native-yaml-discard.png`
shows the retained real draft, target name, modal Stay/Discard controls and visible
Stay focus; `native-inspector.png` shows hidden Secret values and explicitly
unavailable apply. These use the offscreen software backend, not native compositor,
screen-reader, mobile or full performance evidence. Final real processes still
report 29-31 ms of missing `Sans Serif` alias lookup.

Remaining reachable work: draft/public-API admission and full failure/lifecycle
matrices, resource recreation during editing, external catalog changes during a
pending transition, editing cursor/selection/scroll behavior, actual protected
apply with masked preview and original Secret preservation, conflict comparison/
reconciliation and uncertain-outcome read-back. Complete accessibility,
large-document/cache RAM and UI/frame/CPU measurements, frozen-legacy parity,
migration/rollback and platform/release gates remain open. There is no draft disk
persistence, new timer, second resource cache or new dependency; the immutable
original document and its editable text have distinct required purposes.

## What does native protected Apply currently prove?

Scope: INS-016 through INS-029 and SEC-001/SEC-008/SEC-009 through actual Qt Quick controls,
the real stores, the shared Kubernetes transport and public JSON preparation/merge
entrypoints. The external fake is only Kubernetes HTTP; it is not an internal
service replacement. `yaml-draft-check-test real_apply` is the independent real
cluster lane, with `kubectl` changing and reading back only test-owned objects.

| Behavior / scenario | Public entrypoint | Test assignment | Remaining gap |
| --- | --- | --- | --- |
| Fresh edit, local syntax/identity admission, Unicode, duplicate keys, aliases, malformed input | YAML editor / Check YAML; public `checkYamlDraft` | `native.yaml_check.*` | Full local-check behavior is not server schema validation. |
| Startup cannot open a context before profile settings/catalog readiness | Actual Workspace and Main.qml / Open context | `native.yaml_check.ui_early_context` | The discovered startup race is fixed; this does not explain earlier intermittent failures without evidence. |
| Preparation yields the UI loop; late check output cannot undo refresh invalidation | Workspace preview action / native Check and Refresh controls | `native.yaml_apply.ui_preparing`, `native.yaml_check.ui_check_refresh` | Large-document frame/RSS measurements and compositor evidence remain required. |
| Frozen target, masked field diff, Cancel/Escape focus, no unconfirmed PATCH | Native preview dialog | `native.yaml_apply.ui_preview` | Native screen-reader and device-specific focus evidence remain required. |
| UID/version guarded JSON Patch and fresh acknowledged read-back | Native Apply confirmation | `native.yaml_apply.ui_success` | Real counterpart is `real_apply`, recorded below when executed. |
| Preserve original Secret bytes through an unrelated edit | Actual masked Secret editor / confirmation | `native.yaml_apply.ui_secret`, `native.yaml_apply.secret_retained`, `secret_changed`, `secret_new_placeholder` | User-entered values may be visible only in the local draft; loaded values and every comparison remain masked. |
| Canonical Secret write-only input, override, Unicode, empty values, unchanged intent, expansion limit and non-Secret fields | Public `prepareYaml` and `yamlChangesObserved` | `native_yaml_apply_string_data_value`, `override`, `unicode`, `empty`, `same`, `limit`, `non_secret` | Six cases failed before normalization; all seven passed after. Real native typing and write-back are assigned to `real_apply`. |
| Server changes preserved; overlaps explicitly chosen; recreated UID refused | Native comparison and choice controls | `native.yaml_apply.ui_conflict`, `ui_overlap`, `ui_recreate`; public `merge_*` | Multi-row keyboard/assistive-technology and large-conflict-list behavior remain to be proved. |
| Auth/authorization/rate-limit/validation/redirect failures never auto-write or echo body data | Native confirmed action / external HTTP boundary | `native.yaml_apply.ui_auth`, `ui_forbidden`, `ui_rate`, `ui_validation`, `ui_redirect` | Current fixtures exercise these paths, not every provider or cluster policy. |
| Lost/malformed responses read back once without a second PATCH; unobserved intent remains uncertain | Native action / external HTTP response loss | `native.yaml_apply.ui_lost`, `ui_malformed`, `ui_unobserved`; public `observe_*` | Other network/proxy/platform implementations still need evidence. |
| Discard cancels only a queued write; sent write/read-back finish without reopening the inspector | Actual discard guard and inspector close | `native.yaml_apply.ui_queue_cancel`, `ui_hidden` | Additional window/session and shutdown timing matrices remain required. |
| Document/patch/alias-expansion limits, typed scalars and exact supported integers | Public `prepareYaml` | `native.yaml_apply.text_limit`, `patch_limit`, `alias_expansion`, `zero_limit`, `bad_tag`, `bad_int`, `bad_bool`, `integer_overflow`, `nonfinite`, `integer`, `typed_bool` | Boundary-size and stress/RSS matrices remain open. |
| Positive configurable 3 MiB default; previous profile schemas retained; stale saves rejected | Real private settings store / Settings limit action | `native.settings.yaml_default`, `yaml_save`, `yaml_zero`, `yaml_negative`, `yaml_conflict`, `version_two_yaml_upgrade` | Native Settings UI error/keyboard matrix and cross-platform permissions remain open. |

Focused current checks passed **165/165 in 38.50 seconds** before the final payload
cleanup and canceled-preview feedback regressions. Those two regressions failed
first and were then corrected. Full-suite, coverage and real-cluster results below
must be used for the final build, not this intermediate count.

Public syntax-check measurement, actual Release/coverage build, 10,000 fields,
159,054 UTF-8 bytes, 50 samples: **p50 12.118 ms / p95 13.174 ms**. This measures the
local parser entrypoint only. It is not cached rendering, an Apply/frame/RSS budget,
a legacy comparison or a portability result. The expensive local actions now run
with immutable snapshots outside the UI thread; read/paint/hover/scroll still do
not schedule them.

Evidence directory: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core`.
The full current-run report is `native-apply-full-run.txt`. The real runner report
is `native-apply-real-run.txt` when executed. Earlier focused logs and failing-first
admission/layout/type/lifecycle checks remain evidence of their exact builds only.

### Which real Kubernetes behaviors now have evidence?

The local real runner completed successfully on 2026-10-03 using a test-owned
K3s v1.35.5 cluster, explicit private kubeconfigs, loopback TLS and client
certificates. `native-apply-real-run.txt` records the executed lanes:

- Native import, resource selection, YAML/values, explicit individual Secret reveal/copy and protected draft transitions.
- Local syntax checks leave real ConfigMap and Secret objects unchanged, verified independently with `kubectl` and byte comparisons.
- Confirmed exec client-certificate authentication and default multi-container Pod logs.
- Confirmed native ConfigMap JSON Patch, deliberate version-conflict reconciliation and preservation of an independently written foreign label.
- Unrelated Secret editing preserves the original bytes. A newly entered `stringData` value is written through the native confirmation workflow, checked independently using `kubectl`, and remasked in fresh YAML. Removing that test value restores byte-identical original `data`, independently checked by the runner.

The runner's cleanup trap verifies removal of its own cluster container and
anonymous volumes; private run files are removed. Only the existing pinned K3s
image is retained. No production context, shared container, shared image or global
prune operation is used. Final cluster identity: `podlord-native-run-gn8lvg`.

Current QQuickWindow images `e2e/native-yaml-apply.png` and
`e2e/native-yaml-apply.png.secret-entered.png` were visually inspected. The latter
captures the retained YAML scroll position in generated metadata, not a view of
all Secret fields. The actual editor text and masked diff are asserted separately.
These are software/offscreen Qt images, not native compositor, assistive-technology,
installed legacy parity or device-portability evidence.

Earlier real attempts failed before Apply while opening ConfigMap values. The
public mouse driver now settles pending Qt layout work before choosing click
coordinates; stage diagnostics are retained. Subsequent real lanes passed, but
that is not proof that every earlier intermittent failure has been explained.
The added Secret cleanup assertion also failed first because the removed field
name remains in server-owned `managedFields`; the corrected test checks the actual
edit and independently compares final Secret data, not incidental metadata.

Still required: repeated real-lane reliability, recycled conflict-row presentation
and keyboard/assistive-technology coverage, read-only YAML keyboard scrolling,
large-document/frame/RSS/CPU evidence, the branch gate, provider/platform matrix
and complete frozen-legacy workflow comparison. Green Apply evidence does not
complete the migration.

### What did the final protected-Apply regression run establish?

`native-secret-final-run.txt`: **574/574 passed in 303.78 seconds**, two isolated
test jobs. Production coverage is **96.25% lines / 78.78% branches**. The script
correctly exits nonzero because the **90% branch gate still fails**; the threshold
was not lowered and no coverage exclusions were added. A green scenario result
must not be described as a green release gate.

The actual QML Secret-input regression is `native.yaml_apply.ui_secret_input`:
local input is visible while editing, confirmation is masked, canonical data is
sent, original data survives, and fresh loaded text is remasked. Together with the
real lane, this resolves the approved presentation decision; it does not prove
all Secret/provider/platform or failure combinations.

Current public CLI list measurements include process startup and JSON validation,
16 records, 50 samples on macOS arm64 / Qt 6.11.2:

| Boundary | p50 | p95 |
| --- | --- | --- |
| Sessions | 11.675 ms | 12.180 ms |
| Owned kubeconfig snapshots | 16.439 ms | 18.645 ms |

These are CLI measurements, not cached UI rendering, frame latency, RAM, energy,
old/new comparative gains or cross-platform performance. The real UI processes
still report 30-35 ms of missing `Sans Serif` font-family alias lookup; this is a
measured remaining startup cost, not an optimization already delivered.

### Which file and folder import behaviors are exercised through public boundaries?

The recursive import increment adds `podlord-source import-path FILE_OR_FOLDER`
and uses the same store from the actual QML Import button and folder chooser.
The existing `import FILE` command retains its single-snapshot output contract.
Healthy files are published independently; partial CLI results retain all outcomes
and exit 1 rather than hiding failures behind a successful exit. No import runs
authentication plugins or contacts a cluster.

| Behavior | Public entrypoint | Test | Remaining gap |
| --- | --- | --- | --- |
| Single file and recursive extension-independent folder import | Source CLI | `native.source_input.file`, `.nested` | Populated old/new workflow comparison |
| Empty folder, missing path, blank input and extra arguments | Source CLI | `.empty`, `.missing`, `.blank`, `.arguments` | Large-directory latency and memory |
| Partial success and entirely invalid input | Source CLI | `.partial`, `.all_invalid` | Concurrent changes to files during scan |
| Repeated import and changed-file snapshot retention | Source CLI plus listing | `.repeat`, `.changed` | Automatic replacement-session creation |
| Home expansion and unsupported home syntax | Source CLI | `.home`, `.unsupported_home` | Other OS environment/path evidence |
| Profile subtree exclusion and rejected profile scan root | Source CLI | `.inside_profile`, `.profile_root` | Windows ACL evidence |
| Child symlinks are not followed | Source CLI | `.symlink` (Unix) | Windows junction/reparse-point evidence |
| Deleted original remains independently owned | Source CLI plus listing | `.deleted` | Watcher and replacement-session workflow |
| Explicit store contention and unreadable root | Source CLI | `.busy`, `.denied` (Unix) | Concurrent writers and unreadable child directories |
| Folder import populates contexts and presents partial failures | Actual QML Import button and details dialog | `.ui`, `.ui_partial` | Native folder-picker interaction, keyboard and assistive technology |

Failing-first evidence: `.nested` and `.ui` both failed against the previous
implementation, then all **20/20** registered scenarios passed after the shared
import path was implemented. The UI scenarios use real stores and QML controls;
their kubeconfig represents an external boundary and no cluster is opened.
They are not real Kubernetes or platform-portability evidence.

### What has the native desktop comparison actually shown?

Both preserved C# and current C++ application processes were opened on macOS
with fresh, separate private profiles and no ambient kubeconfig or update checks.
Native compositor screenshots and accessibility trees were captured under the
private comparison evidence directory for the C# Settings sections Alerts,
Appearance, Diagnostics, Graphics, Privacy, Sources, Sync, Workspace and About,
and the native empty Resources surface. Native synchronization settings were
observed as a real visible dialog; an earlier screenshot taken before raising the
window is not valid visual evidence for that dialog.

This establishes reachable legacy settings navigation, not behavior parity.
The native application still lacks most corresponding settings surfaces and
several resource/events/ports, radar, alert and workspace capabilities. Empty
profiles cannot establish cluster workflow equivalence. Full populated C# versus
C++ comparison and required platform/distribution gates remain open; the product
is not release-ready.

## Native appearance: what is actually verified?

The appearance migration covers 19 existing themes, dark/light variants, and
subtle/medium/arcade intensity. `native.appearance.*` contains 124 scenarios:
114 combinations through the actual QML dialog, plus restart, policy
preservation, cancellation, lock contention, cross-window conflict, unknown or
empty theme, invalid variant, invalid intensity, and unchanged selection.
The tests compare all 17 semantic colors against the C# source catalog and
inspect actual input text, background, and placeholder palette roles. Optional
`PODLORD_THEME_SCREENSHOT_DIR` captures each rendered combination.

The dark placeholder case failed before its palette role was corrected. The
original 119 appearance cases passed; the five additional validation/no-op
cases passed after correcting the test's success return. Broader regression
and coverage results are recorded separately; appearance counts do not prove
whole-product parity.

Desktop evidence from 2026-10-03 uses separate empty native and C# profiles,
without importing any cluster. Both applications were switched through their
public selectors. Evidence directory:
`/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-03`.

| Scenario | Native screenshot | C# screenshot | Remaining gap |
| --- | --- | --- | --- |
| Sirocco Command / dark / subtle | `native-theme-sirocco-dark-subtle.png` | `legacy-theme-sirocco-dark-subtle.png` | Different controls, typography and layout; empty profile only |
| Gunmetal Sector / light / arcade | `native-theme-gunmetal-light-arcade.png` | `legacy-theme-gunmetal-light-arcade.png` | Static texture differs; radar and full settings navigation are not equivalent |

Screenshots were visually inspected. They are not equal-size pixel diffs and do
not establish contrast gates, all-menu parity, mobile support, or behavior with
populated cached lists, active logs, an edited YAML document, or running forwards.
The general appearance/localization capability remains partially implemented:
theme controls now exist, but localization and full surface parity remain open.

### What passed after the theme implementation?

On 2026-10-03, the complete native suite passed **718/718** tests in 316.33 s.
A separate appearance run passed **124/124** in 14.20 s. Evidence lives in
`native-theme-full-run.txt` and `theme-complete-run.txt` under the native build
report directory. The full run overlapped an isolated focused regression run;
its wall time is not a performance benchmark.

The full coverage report is **96.24% lines / 78.70% branches**. The runner exits
unsuccessfully because the required 90% branch gate is not met, even though all
executed tests pass. This remains a release blocker, not an ignored check.

`native-theme-real-run.txt` records the real local Kubernetes lanes passing:
TLS/client-certificate resource/inspector/Secret reads, local YAML checks without
server writes, confirmed exec authentication, multi-container logs, and
confirmed JSON Patch with concurrent-change reconciliation and Secret
write/read-back/remasking. Independent Kubernetes reads verified the write
results and unchanged original Secret bytes. The owned cluster
`podlord-native-run-zrfn2q`, its volumes and temporary runner files were removed
by the successful cleanup trap. The pinned reusable K3s image was retained.

Both empty-profile desktop comparison processes exited successfully. Their
owned temporary profile directory was removed; the four named screenshots were
retained as evidence. No production or shared cluster was used. These real
Kubernetes lanes regress existing resource behavior after the palette changes;
they do not yet drive theme changes while a populated inspector is open.

## What did the populated desktop comparison on 2026-10-03 establish?

The local-only `scripts/test-native-visual-kubernetes.sh` harness creates a real,
test-owned K3s cluster. Independent Kubernetes reads verified 1051 labelled
namespaced objects across eight namespaces: 512 ConfigMaps, 256 Secrets,
64 Deployments, 64 Services, 32 each of StatefulSets, DaemonSets, CronJobs and
PVCs, 16 ServiceAccounts, eight suspended Jobs and three directly created Pods.
Eight Namespaces, controller-created Pods, system objects and real Events are
additional. Bulk workloads are inactive; two Deployment replicas and a real
two-container logging Pod run. Error-image and crashing Pods exercise problems.
These are real Kubernetes objects, not application fixtures or mocked internals.

Both applications used separate private profiles and the same loopback TLS
cluster. Desktop navigation and captures were driven through public controls.
The harness verifies cluster setup/counts/cleanup; it is not an automated assertion
of application parity. Initial setup failures and desktop automation interruptions
were not counted as successful application tests. Foreground relaunches were
needed after the original background app processes exited; their cause remains
unresolved. Both final app processes exited successfully.

Evidence directory:
`/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-03-rich-cluster`.
It contains 122 distinct original screenshots, their accessibility snapshots,
`capture-journal.json`, independent `kubernetes-inventory.json`, 54 side-by-side
PNG pairs, `comparisons.json`, `index.html` and `views.html`.
C# is left, C++ right. Original decoded screenshot pixels are neither cropped,
scaled nor overpainted; unused canvas space is white. The original screenshot
service supplied JPEG, so PNG pairs preserve decoded pixels, not lost JPEG detail.
Window dimensions differ (2880x1896 versus 2280x1544); these are not normalized
pixel diffs or controlled frame/performance measurements.

| Scenario | Public boundary / evidence | Observed result | Remaining gap |
| --- | --- | --- | --- |
| Populated Resources and name filter | Actual resource controls; `view-resources`, `view-filter-config` | Both find the eight same-named ConfigMaps across all test namespaces | Entire discovery inventory equivalence and sync convergence not asserted |
| Whole-row inspection | ConfigMap and Pod cells; `view-inspector-overview`, `view-pod-overview` | Clicking cells opens both inspectors for the actual objects | Context menus, keyboard and recycled rows not exhaustively checked |
| ConfigMap YAML and long values | Inspector tabs; `view-inspector-yaml`, `view-inspector-values` | Real content visible; native Overview is raw metadata JSON and YAML view displays JSON-formatted content | Structured overview, syntax presentation, usable width and overflow parity |
| Secret presentation | Inspector tabs; `view-secret-yaml`, `view-secret-values` | Loaded values stay masked; no Reveal used | Full redaction/security behavior is not proved by screenshots |
| Multi-container logs | Actual Logs selectors; `view-pod-logs`, C# `view-pod-logs-alpha` | All selected by default; both real streams observed, native includes source labels | Ordering, failures, hidden polling and size limits need separate assertions |
| Radar membership | C# Problems toggle and resource search; `view-radar-all`, `view-radar-problems`, `view-filter-config` | Problems reduces list from 1417 to 73; radar dims nonmatching tiles. ConfigMap search produces eight matching rows and filtered radar | Native radar absent, therefore parity blocked; radar selection/zoom not exercised |
| Events and Ports workspaces | C# navigation; `view-events`, `view-ports` | Real Events shown; Ports captured in empty state | Corresponding native routes absent; no forwarding connection started here |
| Inspector Events and Links | C# Pod tabs; `view-inspector-events`, `view-inspector-links` | Events populated; selected standalone Pod has empty Links | Native tabs absent; nonempty relationship scenario still needed |
| Settings navigation | All nine C# sections; native Appearance and Sync dialogs | Reachability and visible content captured | Native Alerts, Diagnostics, Graphics, Privacy, Sources listing, Workspace and About counterparts absent |
| All themes and variants | Public selectors; 42 `theme-*` pairs | All 19 themes captured and visually reviewed in dark/light/subtle; Sirocco also medium/arcade in both variants | Not every view in every combination; restart, contrast, reduced motion and mobile evidence remain separate |
| Lossless comparison placement | Public composition CLI; `scripts/test-visual-evidence.cjs` | Pixel-by-pixel placement check with actual captured images | Screenshot service compression and differing window sizes are not removed |
| Owned-resource lifecycle | Driver trap plus normal UI quit | Container `podlord-visual-run-grqt7y`, its volumes and private run profiles removed; retained pinned K3s image only | Interruption during desktop capture remains a separate reliability scenario |

Visual findings block release: missing native radar/metric summary/workspaces/settings,
raw and narrow inspector presentation, different typography and texture treatment,
and incomplete populated-discovery/refresh evidence. Many native non-workload rows
show `Unknown` where C# shows `Observed`; snapshots often report stale/loading.
This review does not assign a network/performance cause without measurements.
Matching palette changes are not whole-product visual or functional equivalence.
No product behavior or approved scope was changed during this evidence pass.

Reproduce cluster setup with `/bin/sh scripts/test-native-visual-kubernetes.sh`;
use only its printed private kubeconfig/profile paths for desktop navigation.
Create the printed run directory's `complete` file after captures and application
exit to trigger cleanup. The driver also cleans up on failure or timeout.
Compose with `node scripts/compose-visual-evidence.cjs ABSOLUTE_EVIDENCE_DIRECTORY`.
Installed `sharp` and `pngjs` must be available to Node (use `NODE_PATH` if needed).
Run `node scripts/test-visual-evidence.cjs LEGACY_SCREENSHOT NATIVE_SCREENSHOT`
to check composition through the real CLI without modifying originals.

## What Does The Native Radar And Related Inspector Package Prove?

Owning contracts: FLT-003 through FLT-010, INS, TBL, ACC, RWT-016/017;
LEG-015/016/017/020/025. Alert evaluation is separately ALT-001/002 and is
not implemented by this presentation package.

The real resource proxy is reused directly by a standard, virtualized Qt
GridView. There is no separate radar membership cache or refresh loop.
Hidden radar releases its delegate model. C++ retains per-session workspace,
filter, sort, zoom and scroll state within the process. Restart persistence,
arbitrary two-axis legacy camera/effects and full filter operators remain gaps.

Overview is now a structured cached field list. Related Events match the selected
UID, not a same-named replacement. Links resolve owner/dependent UIDs with API
identity and namespace guards, plus explicit Node/Namespace references. Missing
cache relationships are not guessed. The Events workspace projects normalized
core/events.k8s.io Event metadata from the existing cache; its table shares the
resource table's UI, tri-state sort, copy and escaped overflow behavior. Event
navigation resolves the target UID or inspects the Event when that target is
not cached. Context menus retain resource paths rather than recycled row numbers.

| Scenario | Public entrypoint / test | Current evidence | Remaining gap |
| --- | --- | --- | --- |
| Initial missing radar | QML Resources/Radar action; `native.ui.radar_filter` | Failing-first regression recorded before production changes | Pre-change output is not proof of new behavior |
| Shared filter, counts and empty/reset | `native.ui.radar_filter`, `radar_empty` | Passed in the first focused 12-case run | Real populated desktop rerun pending |
| More resources than viewport | `native.ui.radar_many` with 1001 externally served resources | Passed, including scroll and local filter | 5000-resource public UI/frame/RSS benchmark pending |
| Keyboard, sorting, zoom, hidden state and session restoration | `native.ui.radar_keyboard`, `radar_sort`, `radar_zoom`, `radar_hidden`, `radar_session` | Passed | All pointer/recycling/failure/restart/platform paths remain to be expanded |
| Theme changes with populated radar | `native.ui.radar_theme` | Check added; run pending | Actual desktop screenshots/contrast/reduced motion remain separate |
| Structured overview, related Events and owner/dependent navigation | `native.ui.inspector_related_overview`, `inspector_related_events`, `inspector_related_links` | Passed | Real nonempty relations/Events and stale-update/lifecycle evidence pending |
| Replaced target UID | `native.ui.inspector_related_wrong_uid` | Passed; unrelated Event is excluded | Modern Event API and malformed external metadata need further cases |
| Events workspace, typed count sorting, copy and missing-target navigation | `native.ui.inspector_related_workspace`, `inspector_related_event_sort`, `inspector_related_event_copy`, `inspector_related_event_missing` | Checks added; run pending | Column persistence/pinning, full discovery, event search navigation and legacy parity remain open |

The original overview regression initially failed at test setup because the
external server selected the older ConfigMap/Secret scenario; that setup was
corrected rather than counted as an overview implementation reproduction.
Existing overview checks now read the public accessible summary, while new
structured-field checks inspect actual rendered rows. YAML formatting/redaction
and draft/apply ownership are unchanged. Neither these checks nor palette
selectors establish complete visual equivalence or native release readiness.

### What Did The Event Alias Regression Prove?

| Behavior | Public boundary | Test | Evidence |
| --- | --- | --- | --- |
| Core and modern representations of the same Event appear once in resource table, radar, Events workspace and related Events; modern count/message are preserved | Native UI with external Kubernetes HTTP boundary | `native.ui.inspector_related_alias_same` | Failed before the shared projection fix; passed afterward |
| Distinct Event UIDs are retained even when name, namespace and regarding resource match | Same public UI boundary | `native.ui.inspector_related_alias_distinct` | Passed before and after the projection fix |
| Inspector log requests take priority while discovery still has pending collections | Native UI with 64 external workload collections | `native.logs.priority` | Click after first rendered inspector frame; unchanged 2.2-second log deadline and explicit incomplete-workload assertion; passed |

The focused package run passed 23/23 tests in 44.99 seconds, including all nine radar scenarios, related inspector views, the Events workspace, sorting, clipboard and context-menu behavior. `radar_theme` checks all 19 themes in dark and light variants through the shipped model and UI. These checks do not replace screenshot evidence or establish full legacy equivalence.

The preceding full run passed 734/735 tests; the log-priority scenario clicked before the new inspector layout had rendered. Its correction waits for the public window's first frame rather than waiting for detail networking or weakening the priority deadline. A fresh complete run is required for the final gate.

| Additional regression | Public boundary | Test | Failing-first evidence |
| --- | --- | --- | --- |
| Navigation coordinates stay fixed during explicit synchronization and after completion | Native radar UI and Refresh action | `native.ui.radar_refresh_layout` | Failed with conditionally visible loading bar |
| Modern Event with null eventTime displays its real deprecatedLastTimestamp and count | Native Events table with external Kubernetes HTTP boundary | `native.ui.inspector_related_alias_legacy_time` | Failed with the previous timestamp normalization |

The real modern Event inventory contains null `eventTime` and a populated `deprecatedLastTimestamp`. These are compatibility fields returned by the real API, not invented replacement dates. Screenshot review also reproduced loading-bar layout shifts. The corresponding fixes belong to shared normalization and shared loading layout, not individual views.

### Radar and Events desktop verification, 2026-10-03

The focused public-boundary suite passed **25/25** tests after the canonical Event, timestamp compatibility and stable refresh-layout corrections. The log-priority, same-UID Event alias and refresh-layout cases each passed five consecutive executions (**15/15** executions). The priority case waits for the inspector's first presented frame, not its detail request, and still requires log results while collection work remains outstanding.

Real desktop verification used the same disposable local cluster for the frozen C# reference and current native application. Eight ConfigMaps selected by the shared filter matched the independent Kubernetes API inventory and both table and radar. Four final side-by-side comparisons were visually reviewed: Events workspace, inspector overview, inspector related Events and inspector Links. The related-Events capture proves the empty state for that ConfigMap; populated related Events remain separately covered through the external API boundary UI tests, not through that screenshot.

Evidence index: `/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-03-radar-events/index.html`. Scope, capture classifications and remaining visual gaps: the adjacent `review.md`. Original screenshots are preserved; comparisons concatenate C# left and C++ right without resizing or cropping. The intermediate Events comparison predates the timestamp correction and is not final release evidence.

The driver and both separately relaunched applications exited successfully. Its owned `podlord-visual-run-shiprg` container, volumes and private profiles were cleaned up; scoped Docker inventories confirmed no remaining owned container or volume. The pinned reusable image was retained and unrelated resources were not touched.

This is a verified increment, not full migration completion. Alarm scope is specified for the whole session independently of presentation filters; alarm implementation remains open. Table column controls, advanced filters, settings parity, richer radar markers, complete current-build theme/view captures, accessibility and platform release evidence remain release blockers. The complete suite and coverage result follow below once the final isolated run completes.

### Settings document compatibility and validation

| Behavior | Public entrypoint | Executable case | Result |
| --- | --- | --- | --- |
| Version-three settings load and upgrade without losing limits | `ReadSettingsStore::load/save` | `native.settings.schema_v3_upgrade` | Passed |
| Invalid YAML limits: zero, negative, fraction, text, integer overflow, null or missing key | `ReadSettingsStore::load/save` | `native.settings.schema_yaml_{zero,negative,fraction,string,high,null,missing}` | Seven independent cases passed; original bytes retained |
| Version-four zero YAML limit | `ReadSettingsStore::load/save` | `native.settings.schema_v4_yaml_zero` | Passed; original bytes retained |
| Unknown version-three or version-four fields | `ReadSettingsStore::load/save` | `native.settings.schema_v3_extra`, `native.settings.schema_v4_extra` | Passed; original bytes retained |
| Non-text theme name, variant or intensity | `ReadSettingsStore::load/save` | `native.settings.schema_theme_name_number`, `native.settings.schema_theme_variant_null`, `native.settings.schema_theme_intensity_object` | Passed; original bytes retained |
| Unknown canonical theme name, variant or intensity | `ReadSettingsStore::load/save` | `native.settings.schema_theme_{name,variant,intensity}_unknown` | Three independent cases passed; original bytes retained |

These seventeen isolated cases passed together in 0.56 seconds. They use actual private filesystem profiles and atomic settings persistence, not an internal storage fake. No production settings behavior was changed.

The preceding complete desktop/core suite passed **739/739** tests in **358.87 seconds**. Its measured coverage was **96.08% lines / 78.30% branches**. The executable release gate returned failure because branches remain below the required **90%**. Passing tests must not be interpreted as passing the release gate. The next combined run includes all 756 cases.

### Combined verification result, 2026-10-03

`PODLORD_NATIVE_BUILD_DIR=/Users/yuna/.local/share/podlord-comparison/native-builds/session-core PODLORD_TEST_JOBS=2 /bin/sh scripts/test-native.sh` passed **756/756** tests in **520.90 seconds**. Coverage: **96.13% lines / 78.73% branches**. The script returned exit code **1** at the unchanged release coverage gate: the line minimum of 95% is met, the branch minimum of 90% is not. The preceding settings additions increase measured branch coverage but do not close that blocker.

Final log: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/radar-events-settings-final-full-run.txt`. CLI benchmarks again measured only 16-record catalog listing with process startup and JSON validation: session p95 **13.668333 ms**, owned-kubeconfig p95 **17.66225 ms**. They do not establish large-cluster desktop performance.

After the combined run, scoped Docker inventories again showed no owned `podlord-visual-run-shiprg` container or volume. No production or shared cluster was used or changed. Full visual and functional parity, whole-session alarms, current-build theme matrix, platform packaging and the branch-coverage gate remain open release requirements.

## Which native table-layout and package checks passed on 2026-10-03?

Scope: TBL-004 through TBL-007 for Resources and Events only, plus the existing
native regression suite. This is not complete desktop, mobile or legacy parity.
The authoritative layout schema comes from the actual table model's stable
header IDs. A single profile-level `table-layouts.json` stores separate layouts
for Resources and Events; rendering reads the in-memory snapshot, while an
explicit Save performs the atomic write asynchronously.

### Public UI behavior

All names below use the `native.ui.` prefix and the real QML/Workspace entrypoint.
The local HTTP boundary replaces only the external Kubernetes API. Owned stores,
models, persistence, validation and application control flow run unchanged.

| Behavior | Public action | Test suffix | Remaining gap |
| --- | --- | --- | --- |
| Hide a column | Columns, Show, Save | `columns_hide` | Other table types |
| Reveal a column | Columns, Show, Save | `columns_reveal` | Other table types |
| Pin a column | Columns, Pin, Save | `columns_pin` | Other table types |
| Reorder columns | Columns, Earlier/Later, Save | `columns_order` | Other table types |
| Change width | Columns, width, Save | `columns_width` | Drag-resize is not implemented |
| Cancel draft | Columns, Cancel | `columns_cancel` | None in this scope |
| Restore defaults | Columns, Defaults, Save | `columns_reset` | None in this scope |
| Restore layout after restart | Save, recreate workspace/profile | `columns_restart` | Installed-device migration |
| Keep table layouts independent | Resources/Events, Columns | `columns_isolation` | Other table types |
| Reject stale writes | Save conflicting profile layout | `columns_conflict` | None in this scope |
| Report busy store | Save while profile lock is held | `columns_busy` | None in this scope |
| Reject invalid width | Edit width, Save | `columns_invalid_width` | None in this scope |
| Retain at least one visible column | Hide final column, Save | `columns_last_visible` | None in this scope |
| Preserve sort and copy after pin/order changes | Header click, Copy | `columns_sort_copy` | Other table types |
| Operate column controls with keyboard | Space/Return in Columns | `columns_keyboard` | Full assistive-technology audit |
| Pin non-first Namespace with correct value, copy and sort | Pin Namespace, copy cell, click header, horizontal scroll | `columns_pin_namespace_basic`, `columns_pin_namespace_fusion` | Other table types |
| Scroll 1,001 resources with every column pinned | Pin all, resize, scroll pinned table | `columns_pin_all_basic`, `columns_pin_all_fusion` | 5,000-resource UI performance budgets |
| Reuse filtered Event delegates correctly without fetches | Clear/reapply filter ten times, open related inspector | `inspector_related_alias_reuse_basic`, `inspector_related_alias_reuse_fusion` | Broader lifecycle/RSS budgets |

The initial missing-Columns regression is retained in `columns-before.txt`.
Non-first pin failures are retained in `pinned-before.txt`; the 19 column UI cases
then passed in `pinned-overflow.txt`. The full-suite Event-alias failure is retained
in `columns-final-suite.txt`. Its visual-tree diagnostics showed duplicate cached
cell names; UI checks now resolve active cells through the documented public
`TableView.itemAtIndex` API rather than selecting an arbitrary cached delegate.
The repeated-filter checks also exercise genuine empty-field keyboard input.
`event-alias-public.txt` records five repetitions per style, ten filter cycles per
execution. The final full suite additionally asserts that these filter cycles do
not add Kubernetes requests. No production model workaround was added for the
test lookup.

### Public layout-store behavior

The 68 `native.table_layout.*` tests call `TableLayoutStore.load/save` with real
owned filesystem paths and atomic persistence. Locks and concurrent calls are
real; no owned storage implementation is faked.

| Behavior | Test suffixes | Remaining gap |
| --- | --- | --- |
| Missing configuration, invalid profile and table | `missing`, `empty_profile`, `relative_profile`, `unknown_table`, `profile_file`, `parent_file` | None in this scope |
| Hide, pin, order, width, maximum width and repeat | `hide`, `pin`, `order`, `width`, `maximum_width`, `repeat` | None in this scope |
| CAS, other-table merge, concurrency and coherent reads | `conflict`, `other_table`, `concurrent`, `read_during_save` | Device/filesystem matrix |
| Lock/file failures | `busy`, `lock_directory`, `file_directory` | Disk-full and hardware I/O faults |
| Reject invalid persisted root without replacement | `version`, `extra_root`, `layout_type`, `unknown_key`, `missing_table`, `array_layout`, `malformed`, `empty`, `array_root`, `oversized` | None in this scope |
| Reject invalid incoming columns | `input_missing`, `input_extra`, `input_id_type`, `input_id_unknown`, `input_visible_type`, `input_pinned_type`, `input_width_zero`, `input_width_negative`, `input_width_fraction`, `input_width_text`, `input_width_null`, `input_width_overflow`, `input_pinned_hidden`, `input_duplicate`, `input_count`, `input_item_type`, `input_no_visible` | None in this scope |
| Reject invalid stored columns without replacement | `stored_missing`, `stored_extra`, `stored_id_type`, `stored_id_unknown`, `stored_visible_type`, `stored_pinned_type`, `stored_width_zero`, `stored_width_negative`, `stored_width_fraction`, `stored_width_text`, `stored_width_null`, `stored_width_overflow`, `stored_pinned_hidden`, `stored_duplicate`, `stored_count`, `stored_item_type`, `stored_no_visible` | None in this scope |
| Private permissions, symlinks and denied access | `private`, `profile_symlink`, `file_symlink`, `read_denied`, `write_denied` | Non-macOS filesystem matrix |

### Executed verification

Evidence root: `/Users/yuna/.local/share/podlord-comparison/`.

| Check | Result | Evidence relative to that root |
| --- | --- | --- |
| Initial 68 store and 15 column UI checks | 83/83 passed, 26.56 s | `native-builds/session-core/columns-verified-focused.txt` |
| Four additional pin/scroll regressions plus column UI checks | 19/19 passed, 33.19 s | `native-builds/session-core/pinned-overflow.txt` |
| Final complete native suite | 845/845 passed, 200.14 s | `native-builds/session-core/columns-final-coverage.txt` |
| Production coverage | Lines 96.14%; branches 79.09%. The unchanged 95%/90% gate fails, script exits 1. | `native-builds/session-core/coverage/report.txt` |
| Session CLI list boundary, 16 records, 50 samples | p50 12.25 ms, p95 13.62 ms, max 14.70 ms | `native-builds/session-core/coverage/benchmark.json` |
| Source CLI list boundary, 16 records, 50 samples | p50 16.33 ms, p95 16.83 ms, max 17.62 ms | `native-builds/session-core/coverage/source-benchmark.json` |
| Clean Release configure with testing/coverage disabled | CTest enumerates zero tests | `native-builds/release-macos-arm64` |
| macOS package preflight | Ad-hoc signature verifies; runtime dependency paths pass; ZIP 43,933,958 bytes; installed regular-file sum 118,242,283 bytes. The 50,000,000/100,000,000-byte package gate fails. | `package-evidence/2026-10-03-macos-release-preflight/package-evidence.txt` |

The package checker is repeatable through
`/bin/sh scripts/check-native-macos-package.sh /absolute/new/evidence-directory`.
It refuses existing destinations, builds Release without tests/coverage, deploys
Qt dependencies, signs rewritten embedded libraries locally, verifies the bundle,
records Mach-O dependencies, hashes and sizes, and cleans its private build area.
`LC_ID_DYLIB` is library identity metadata, not a runtime load dependency; the
check uses load commands so an embedded library's original ID does not create a
false host-dependency failure. This does not replace a clean-device test,
Developer ID signing/notarization, dependency-license/SBOM checks or other targets.

### Real Kubernetes and screenshot evidence

The owned loopback-only `podlord-visual-run-g6050z` stack used
`rancher/k3s:v1.35.5-k3s1` and 1,051 labelled objects across eight namespaces, plus
normal generated Kubernetes resources/events. The current packaged application
was launched separately with its private test profile after an earlier driver
failed to honor the package override. The driver now accepts `PODLORD_NATIVE_APP`.
The separately launched application and driver-owned processes were stopped;
the driver completed with exit 0 and removed its container, volumes and profiles.
Scoped Docker checks found no remaining visual-test containers/volumes. One k3s
image remains; shared images and infrastructure were not pruned.

`desktop-evidence/2026-10-03-columns-final/` contains 158 unique original images:
152 captures across all 19 themes in dark/light for Resources, Events and both
column dialogs, plus Namespace pin, Radar, native Appearance and three legacy
captures. Repeated capture records are not counted as independent unique images.
The capture helper originally wrote originals into the earlier capture directory;
these were copied byte-for-byte into the final evidence directory. The matrix,
per-image accessibility snapshots, journal and `capture-provenance.json` record
that relocation and executable identity. Some earlier same-name captures were
superseded; they must not be treated as immutable baseline evidence.

The native executable SHA-256 is
`51ef77118a292883562e2ffae2dc0c0b5e271e470c0eef06674265ac62350bff`.
The legacy executable SHA-256 is
`bd88bb30065fbdc16f15620c09d239a2cb0c3dc294de29f995532f0058d37c1b`.
Older duplicate native bundles/ZIPs created during this check were removed; their
small diagnostic reports remain. The retained package is the release-preflight
bundle, with the same native executable hash as the screenshots.

All eight dark/light contact sheets and the three unscaled left-C#/right-C++
comparisons were visually inspected. `comparisons.json` records exact source
images and dimensions; the compositor concatenates decoded originals without
cropping or scaling, with canvas padding below the shorter image. No Secret values
were opened for these captures. These four-surface theme checks did not establish
contrast compliance, all intensity variants, all inspector/settings views or full
legacy parity. Radar was visually populated, but its accessibility snapshot became
incomplete during capture; assistive-technology parity remains open.

The comparisons expose real remaining differences: native dashboard metrics,
advanced filter/sidebar behavior, Ports and full Settings/navigation capabilities
are not present at legacy parity. Legacy/native resource totals also differ, so
these screenshots are presentation evidence, not equal-dataset assertions.
Whole-session alarms, broad table coverage, migrations, signed platform/device
releases, complete accessibility evidence, 5,000-resource interaction/CPU/RSS
budgets and the branch-coverage gate remain release blockers. Deployment
control-style selection is awaiting a decision; the size budget is unchanged.

## What Does The Native Alarm And Metric Increment Prove?

The native implementation adds session-scoped Alerts, a filtered resource Dashboard,
and CPU/memory/storage gauges in the inspector. These additions close concrete
workflow gaps, not the entire legacy capability ledger. Verification uses the real
native application/store/cache flow; only the external Kubernetes boundary is
simulated in deterministic UI tests.

| Behavior / scenario | Public entrypoint | Executable test | Remaining evidence |
| --- | --- | --- | --- |
| Add, enable, duplicate, delete and restore custom alarms; retain invalid/busy edits | QML Alerts editor | `native.alert_ui.create`, `disable`, `duplicate`, `delete`, `restart`, `invalid`, `busy` | Full view/theme/device/audible review |
| Rules use the entire originating session despite view filters | QML filter + Alerts results | `native.alert_ui.filter`; `alert_ui_test real <local-kubeconfig>` | Large-session CPU/RSS and multiple-context failure isolation |
| AND/OR groups; exact/prefix/suffix/regex; numeric ranges and absent metrics | QML editor and observable results | `native.alert_ui.and`, `or`, `exact`, `prefix`, `suffix`, `regex`, `numeric`, `missing_metric` | Full legacy matcher parity including compound duration/view-entry behavior |
| Finite effects, no focus replay on tab return, regex exhaustion feedback | Session navigation + public alarm actions/results | `native.alert_ui.hold`, `no_replay`, `regex_limit` | Camera/once semantics and assistive-technology review |
| Private, atomic, conflict-safe rule storage; bad/future data retained | `AlertStore.load/save`, public rule parser | `alerts_store_test` registered scenarios | Windows filesystem-specific checks; migration from legacy rule storage |
| Observed values versus configured requests/limits, actual zero versus unavailable | Inspector gauge labels | `native.alert_ui.metric_success`, `metric_zero`, `metric_empty` | All applicable resource kinds on real supported platforms |
| Partial/stale measurements and invalid external quantities | Inspector and session status | `native.alert_ui.metric_partial`, `metric_stale`, `metric_invalid` | Metrics HTTP failure/backoff retention, transition-to-stale device evidence |
| Unchanged refresh does not rebuild resource presentation | Public refresh + presentation signal | `native.alert_ui.metric_repeat` | 5,000-resource interaction and clean-idle budgets |
| Dashboard follows filters without filter-triggered requests | Dashboard QML route + filter | `native.alert_ui.dashboard` | Full legacy aggregate/dashboard layout parity |
| Quantity normalization, configured references, timestamp/container validation, same-name recreation, idempotent attachment | Public metric normalization API, supplementary to UI cases | 54 `native.metrics.*` scenarios | Non-restartable active init/ephemeral-container aggregation and general storage-usage provider |
| Real multi-container Pod has observed CPU without invented/missing constituents | QML inspector against disposable Kubernetes | `alert_ui_test real_metrics <local-kubeconfig>` | Direct legacy/native screenshot comparison and broader real workloads |

The local Kubernetes runs use the owned visual-test cluster with more than 1,000
labelled objects, real image/crash failures and a ready two-container Pod. Both
`real` and `real_metrics` completed successfully on 2026-10-03. They do not use
production credentials and do not substitute API responses for the real cluster.
The desktop comparison was attempted with isolated profiles but the Mac was
locked; the C# process failed to start its native RenderTimer. No new side-by-side
screenshots or audible-device evidence are claimed for this increment.

The prior 899-test run passed all tests but failed the unchanged branch-coverage
gate (77.37%, minimum 90%); line coverage was 95.74%. The expanded suite's result
must be recorded separately, not inferred from focused tests. Existing package
size, signing/platform, accessibility, Ports, advanced filters/settings, full alarm
sound catalog/import, migration and complete visual parity gaps remain open.

Expanded-suite evidence: `alerts-metrics-full-suite.txt` records **961/961 passing
tests**. Line coverage is **95.92%** (minimum 95%), branch coverage **77.97%**
(minimum 90%): the script therefore exits 1 at its coverage gate, not because tests
failed. The metric normalization file has 100% line coverage and 87.82% branch
coverage in this run. These figures precede the subsequent cached-metric radar
tooltip addition and must not be relabeled as its verification.

The one-hour visual driver timed out without completing desktop review and cleaned
its owned container, volumes and private profiles. Scope-limited Docker checks
found no remaining resources for that run; exactly one `rancher/k3s` image was
retained. The restarted comparison processes were stopped separately. Real checks
had already returned exit 0, but their temporary profile/logs were removed by the
driver before archival; no screenshot or immutable archived real-run log is claimed.

### What Did The 5,000-Pod Tab Benchmark Find?

`native.alert_ui.metric_many` drives real native QML, session/cache/store and table
models with 5,000 Pods received from the explicit fake external Kubernetes HTTP
boundary. Thirty public page switches measure through the next rendered frame and
assert that switching does not fetch new data. This is an offscreen software-render
benchmark, not a physical GPU/device or real-5,000-object-cluster measurement.

Before model reuse, the Radar per-page median was 68.325 ms; the first switch was
71.905 ms. Retaining native view models across hiding, and replacing per-tile
button machinery with Qt Item/HoverHandler/TapHandler plus explicit button
accessibility, gave a Radar median of 19.829 ms in the recorded follow-up. The
Dashboard median was 7.679 ms; Resources 14.398 ms. Cold switches were still about
65-67 ms. These are single-run measurements, not proof that performance budgets
pass. The full benchmark process, including its in-process fake server/data,
peaked at 281,493,504 bytes RSS; this must not be reported as application-only RSS.
The repeatable output is `metric-many-lightweight.txt` in the local native build.

Retaining models preserves scroll/selection but consumes some additional memory.
Hidden radar animations remain stopped, no hidden surface owns a refresh loop, and
the Dashboard's expensive snapshot derivation still runs only when visible/entered.
A failed `native.ui.radar_many` check exposed an End-key/queued scroll-restore race;
its public keyboard-and-filter regression now passes after preserving the explicit
user-selected scroll position.

### Are Hover Values Inert And Shared?

`native.alert_ui.metric_tooltip` and `metric_radar_tooltip` feed untrusted markup at
the external Kubernetes boundary, open public table/radar value tips, and verify
literal text with no external image/API request. Each surface owns one native
plain-text tooltip rather than a popup per cell. This keeps cached metrics visible in
Radar tips without fetching on hover and preserves keyboard-accessible value tips.
The standard model's existing escaped rich-tooltip role was already safe for table
values; the new popup reads the canonical display value instead, avoiding visible
escape sequences and using PlainText for both table and Radar content. Qt's shared
[ToolTip semantics](https://doc.qt.io/qt-6/qml-qtquick-controls-tooltip.html) permit
rich text, so external content is not entrusted to automatic text-format detection.

## Which current desktop comparisons and health regressions were exercised?

The 2026-10-04 pass uses the preserved C# reference and the native application against one task-owned local Kubernetes cluster. The dataset contains 1,051 explicitly labeled resources, including 512 ConfigMaps, 256 Secrets, 32 Pending PVCs, multiple workload kinds, a real two-container logging Pod, and failure Pods. Metrics Server supplies real Pod measurements. System resources and generated Events are additional and change during the run.

There are 598 original desktop screenshots in the local comparison evidence directory ~/.local/share/podlord-comparison/desktop-evidence/2026-10-03-current-pass/. Original SHA-256 hashes are retained. The initial executable identity file records files on disk at recording time; it does not establish the loaded native process identity because that executable was rebuilt during the desktop run. Side-by-side images concatenate untouched originals, C# on the left and native on the right. Contact sheets are navigation aids, not replacement originals.

The primary native capture matrix covers Resources, Events, Alerts, Dashboard and Appearance for all 19 themes in both Dark and Light variants. The C# matrix covers Resources, Events and Appearance for the same variants. An additional native matrix covers filtered Radar, filtered Appearance and the Overview/YAML/Events/Links/Logs inspector tabs. Primary theme selections were checked through desktop accessibility. Additional Radar/inspector interactions used visible coordinates after the native accessibility subtree stopped being returned; pixel review is required. Radar and Logs contact sheets were reviewed.

These are partial comparison evidence, not visual-parity acceptance. Remaining gaps include different discovery scope and filtering between some pairs, native toolbar density and table columns, radar organization, settings coverage, the incomplete C# inspector/theme matrix, and the unresolved native macOS accessibility observation. The captured C# Logs panel still displayed its start-tail prompt; that pair cannot prove equivalent streaming. Native originals show real alpha and beta log lines with the all selector.

| Behavior | Public entrypoint | Executable scenario | Evidence / gap |
|---|---|---|---|
| Warning, normal and expired Events | Real QML alarm list and dashboard via external Kubernetes HTTP boundary | native.alert_ui.health_event_warning, health_event_normal, health_event_old_warning | Native cache owns the shared problem state. |
| Future timestamp is not already recent | Same boundary | native.alert_ui.health_event_future | Failed before the correction. |
| Missing / invalid Event time | Same boundary | native.alert_ui.health_event_missing_time, health_event_invalid_time, health_event_invalid_dates | Creation timestamp is the fallback; invalid timestamps do not create an alarm. Missing-time case failed before the correction. |
| Pending versus Bound storage | Same boundary | native.alert_ui.health_pvc_pending, health_pvc_bound | Pending case failed before the correction. |
| Pod readiness and startup grace | Same boundary | native.alert_ui.health_pod_not_ready, health_pod_starting, health_pod_completed | Unready Running Pod failed before the correction; completed Pods remain healthy. |
| Time-dependent health without a new request | Same boundary; real event loop and owned cache expiry timer | native.alert_ui.health_event_expiry, health_event_future_activation, health_pod_grace_expiry | Assert both alarm/dashboard transitions and unchanged HTTP request count. |
| Restart outliers ignore failed-Pod population | Same boundary; six external Pod documents | native.alert_ui.health_restart_outlier, health_restart_population | IQR uses Running core Pods only, floor 3, rounded-up threshold. |

All 17 focused health scenarios passed. The nine-case initial run captured four failing regressions before the production change. Full-suite and refreshed coverage results are recorded separately after execution; passing component/UI tests do not close the visual, device, packaging or branch-coverage release gates.


The post-health-correction real Kubernetes UI scenarios real and real_metrics both passed against a second task-owned 1,051-resource cluster. The first repeat attempt used an unsupported scenario name, real_alerts, and failed; that invocation is not counted as application regression evidence. Correct scenario results and executable SHA-256 are retained in health-kubernetes-results.json. Both comparison clusters were removed by the driver's cleanup trap. The second C# reference process aborted while trying to start its macOS render timer, so no fresh reference screenshots are claimed for that run.

## Which Resource-Table Migration Paths Were Proven On 2026-10-04?

Scope: one resource-table increment, not whole-application parity. The native table
now has fifteen data columns; port-forward actions and advanced per-column filters
are separate open migration work. Evidence is retained under
`~/.local/share/podlord-comparison/desktop-evidence/2026-10-04-resource-table/`.

| Behavior / requirement | Public entrypoint | Executable scenarios | Evidence / remaining gap |
|---|---|---|---|
| Column inventory and genuine zero/missing usage; MET-001 through MET-011 | Import/open a session through native QML; external Kubernetes HTTP boundary supplies resources and metrics | native.ui.table_columns, table_values, table_zero | Rendered values match actual quantities; missing storage usage is not fabricated from a request. |
| Typed sorting, stable ties, missing values last, NONE; TBL-001 through TBL-003 | Activate real table headers | native.ui.table_sort_cpu, table_sort_memory, table_sort_createdAt, table_sort_ready, table_sort_restarts | ASC/DESC/NONE, quantities with misleading lexical order, missing rows and restored path order are asserted. |
| Cached categorical fields participate in filtering | Edit the native filter control | native.ui.table_cluster_filter, table_owner_filter | Matching Cluster/Owner values are searchable without requests. Four inspector-related tests now include the legitimate Owner match. |
| Copy, literal full-content hover and keyboard tips; TBL-010, TBL-011, INS-003 | Real mouse movement, keyboard focus and clipboard | native.ui.table_copy, table_hover, table_keyboard_hover | Mouse-only failure identified disabled delegate hover. Both access paths use one plain-text tip and assert no API request. |
| Colors are categorical; TBL-008, TBL-009 | Render the native resource table | native.ui.table_identity | Matching identities share colors; metrics/ages are not identity categories. Complete theme/device acceptance remains open. |
| Refresh retains selection and reading position; TBL-012 | Select a rendered row, scroll, activate Refresh and receive new resource versions | native.ui.table_refresh | Inspector identity and both scroll coordinates remain stable. |
| Six-column layout survives expansion; TBL-007 | Real private profile and public TableLayoutStore load/save | native.table_layout.upgrade_columns, upgrade_columns_save, upgrade_columns_invalid | Order/visibility/pin/width survive; load is read-only; explicit save upgrades; malformed bytes are retained. Existing columns_* UI tests cover editing controls. |
| Workload images and Ready; API group identity | Open native QML session through external Kubernetes HTTP boundary | native.ui.workload_deployment, workload_replicaset, workload_statefulset, workload_daemonset, workload_job, workload_cronjob, workload_default_replicas, workload_zero_replicas, workload_missing_ready, workload_custom_kind, workload_custom_pod, workload_images_empty | Full images and replica-based Ready values, including default/zero/missing, are present. Custom kinds cannot borrow built-in interpretations. Non-Pod restarts remain unavailable. |
| Event target as Owner | Same workflow with core and events.k8s.io documents | native.ui.workload_event_owner, workload_modern_event_owner | involvedObject/regarding retain textual Pod/name identity. |
| Deployment/ReplicaSet/Node status | Same workflow with replica counts and conditions | native.ui.workload_status_deployment_available, workload_status_deployment_unavailable, workload_status_replicaset_available, workload_status_replicaset_unavailable, workload_status_replicaset_zero, workload_status_replicaset_progressing, workload_status_node_ready, workload_status_node_false, workload_status_node_unknown, workload_status_node_missing | Available/Unavailable, ScaledZero/Progressing and Ready/NotReady derive from data, not Observed. |

Failing-first evidence: `table-before.txt` records 17/18 initial failures;
`workload-before.txt` records twelve additional workload failures;
`status-before.txt` records nine additional status failures. `table-workload-final.txt`
records **55/55 focused checks passing**. These results do not replace the full suite.

`real_metrics` passed through the native workspace, table and inspector against
one task-owned local Kubernetes cluster, `podlord-visual-run-pq2w7y`. Metrics Server
provided usage for the real two-container Pod. The untouched `native-real-table.png`
is an offscreen software-rendered Basic-style capture of an explicitly saved table
layout with Name pinned and metric/readiness columns shown. It visibly marks stale
measurements: this is not a freshness-budget pass, GPU/device screenshot, theme
matrix or C# side-by-side comparison. Executable/reference/PNG hashes and a result
record are retained alongside it. The Mac was locked during attempted C# desktop
interaction. The driver removed its owned container, volumes, app processes and
private profiles; scope-limited Docker/profile checks found no remaining resources.

The 5,000-Pod public tab-switch benchmark uses real native QML/cache/model and the
explicit fake external HTTP boundary. Sorting and identity-role reads no longer
format display quantities unnecessarily. In single recorded runs, Resources p50
was 18.839 ms before and 18.079 ms after; p95 was 20.211 ms and 19.900 ms. These
samples do not establish a statistically meaningful gain, a physical-device budget
pass or application-only CPU/RSS. Repeatable outputs are `performance-before.txt`
and `performance-after.txt`.

Expanded full-suite evidence: `full-suite-coverage.txt` records **1,024/1,024
passing native tests** in 398.33 seconds. Line coverage is **96.48%** (minimum 95%);
branch coverage is **79.75%** (minimum 90%). The runner exits 1 at the unchanged
coverage gate, not because a test failed. This remains a release blocker. The metric
normalizer has 100% line / 90.25% branch coverage; the layout store has 98.94% line /
94.62% branch coverage. Whole-workspace coverage does not isolate this table increment.

The additional current-source C# checks in `csharp-tests.txt` passed **433/433**:
Core 91, App Layout 69, Kubernetes 59 and App 214. The public dotnet test command
used temporary private configuration and an explicit filter excluding
K3dKubernetesBehaviorTests. It is not a run of the immutable reference executable
or the complete C# real-cluster suite. That real-cluster fixture and scripts/test.sh
contain cross-run prefix/global Docker cleanup and need ownership-scoped correction
before safe concurrent execution. No global prune or deletion of another run was
performed. New native real-cluster evidence is recorded above; it does not substitute
for the missing C# real-cluster and matched desktop comparison evidence.

Specifications and ADR 0010 retain the data-field, missing-value, group-identity,
readiness/status and bounded layout-upgrade constraints. Visual acceptance, the
branch gate, port-forward actions, advanced filters and whole-product platform/
packaging/accessibility gates remain open. A passing table increment is not a
release-readiness declaration.

### Does The Cluster Column Duplicate The Cached Resource Objects?

The follow-up projection change keeps Cluster once per active table rather than
inserting it into every JSON row. The source catalog remains authoritative; no
new persistence or network owner is added. `native.ui.table_cluster_switch` uses
real QML context selection, opens both contexts, filters by their different cluster
names and returns to the original cached session with unchanged request count.
The harness initially assumed a fixed context order; it now selects the actual
opposite context. This harness correction is not an application regression.
`table-shared-final.txt` records **56/56 focused checks passing** after the change.

Three repeatable 5,000-Pod measurements before/after this projection change are
retained as `performance-repeat-{1,2,3}.txt` and `performance-shared-{1,2,3}.txt`.
Median peak RSS across those runs falls from 300,695,552 to 285,212,672 bytes,
a reduction of 15,482,880 bytes (14.77 MiB, 5.15%). This is the complete test
process, including fake external server/data, not application-only RSS. The median
Resources p50 values are 18.280 ms and 18.704 ms respectively; no speed improvement
or device-performance budget pass is claimed. Rendering and data semantics remain
subject to the separately recorded full-suite and real-cluster results.

The post-projection full run in `shared-full-suite-coverage.txt` passed
**1,025/1,025 native tests** in 259.29 seconds. Line coverage remains **96.48%**;
branch coverage is **79.80%**. The unchanged 90% branch gate still rejects the run.
The follow-up real local Kubernetes lane passed all three public UI workflows:
`real`, `real_metrics`, and `real_health`, using the 1,051-resource dataset. This
confirms the compiled projection change against the real API without relabeling the
earlier table PNG as a new executable capture. Visual parity remains open.

### Does Failed Container Creation Risk Removing Another Run's Container?

`scripts/test-native-visual-cleanup.cjs` executes the real Docker CLI and actual
visual-runner script against an explicitly fake external Docker Engine API on a
private Unix socket. No real Docker daemon or owned application path is simulated.
`cleanup-red.txt` reproduces removal of an unrelated container after a creation-name
collision: one of four initial cases fails. The runner now verifies its exact
container ownership label before collecting logs or removing anything. These use
Docker's [container label metadata](https://docs.docker.com/engine/manage-resources/labels/)
and [typed inspection](https://docs.docker.com/reference/cli/docker/inspect/).
Unverified ownership fails explicitly instead of deleting by name alone.

| Public CLI scenario | Expected observable result | Evidence |
|---|---|---|
| preexisting | Existing identity is preserved; no deletion or log collection | cleanup-after.txt |
| collision | A container appearing between pre-check and creation survives | cleanup-after.txt; failed before correction |
| lost_reply | A created, correctly labeled owned container is removed even when creation reports failure | cleanup-after.txt |
| after_create | Owned container/volumes are cleaned following a later external failure | cleanup-after.txt |
| inspect_error | No deletion or log collection without verified ownership; command fails | cleanup-after.txt |
| remove_failure | Failed removal and remaining owned resources are reported; command fails | cleanup-after.txt |

All **6/6** checks pass. They are separate CLI tests, not additions to the 1,025
CTest count. Run with `PODLORD_NATIVE_BUILD_DIR=<real native build> node --test
scripts/test-native-visual-cleanup.cjs`. Node test-server sockets and private
profiles are cleaned at teardown. The guarded visual runner then passed the same
three real Kubernetes UI workflows in `guarded-kubernetes.txt` and removed its own
cluster/profile. Scope-limited final checks found no remaining resources for this
increment's three real-cluster runs or temporary visual profiles. No global image
prune occurred; the existing pinned K3s image remains available. The separate
legacy C# cleanup issues remain an explicit open gap.

The current native/doc/scripts source state is preserved independently as
`native-source-state.tar.gz` beside the evidence, in addition to the untouched
reference and retained recovery snapshot. This is a local recovery artifact,
not a release package or proof of clean-machine/mobile readiness.

### What Did the Unlocked Desktop Comparison Establish on 2026-10-04?

Evidence directory: `/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked`.
The two actual desktop applications used isolated profiles against the same owned,
real K3s cluster. The dataset contains 1,051 labeled Kubernetes resources, including
512 ConfigMaps, 256 Secrets, workload controllers, Services, pending PVCs, failure
Pods and a running two-container Pod. System resources and changing Events add to
the total; the native view displayed 1,838 resources. Legacy and native discovery
inventories differ, so their total counts are not evidence of complete discovery
parity. No production cluster was deliberately selected for testing.

| Behavior / requirement | Public entrypoint and test | Result / limitation |
|---|---|---|
| Overview cluster, owner and numeric restarts | Real resource-row click; `native.ui.table_overview_metadata` | Available metadata is shown without another source/configuration lookup or API call. |
| Complete, absent and partial readiness | Real QML inspector; `native.ui.table_overview_full`, `_none`, `_partial` | Numeric readiness and green/red/yellow indicators agree; the ratio is not sorted as a string. |
| Pending Pod without container statuses | Real QML inspector; `native.ui.table_overview_pending` | Known desired containers produce `0/N`; an unobserved restart count remains absent. Failing-first evidence: `pending-red.txt`. |
| Zero desired workload replicas | Real QML inspector; `native.ui.workload_overview_zero_replicas_basic`, `_fusion` | `0/0` has a neutral indicator and an accessible replica-count description. |
| No fabricated custom-resource readiness | Real QML inspector; `native.ui.table_overview_custom` | A custom resource has no invented Pod/workload indicator. |
| Readiness text copying and stable refresh | Actual keyboard clipboard action; `native.ui.table_overview_copy`, `_refresh` | Copying makes no request; a same-resource update changes the displayed count. |
| Compact usage indicators with reference markers | Real inspector; `native.ui.table_overview_metric_size`, `_basic`, `_fusion` | Standard ProgressBar controls remain 8 logical pixels high; existing usage/request/limit semantics remain intact. |
| YAML after detail expiry, INS-004/008 | Actual view selection; `native.ui.inspector_yaml_expiry_reopen`, `_repeat` | Entering YAML reloads missing detail with foreground priority; repeated selection does not add another read. |
| Failed expired-YAML reload, INS-006/009 | Actual view selection; `native.ui.inspector_yaml_expiry_failure` | Failure is visible and editing stays unavailable. No success is invented. |
| Authentication failure during reload | Actual view selection; `native.ui.inspector_yaml_expiry_auth` | Switching views after rejection dispatches no further request and does not invoke login. |
| Draft preservation after detail expiry, INS-010 | Actual editing and view switches; `native.ui.inspector_edit_expiry_reopen` | Draft remains editable and unchanged; no extra detail read or write is sent. |
| Live Kubernetes alarm and metric behavior | `alert_ui_test real`, `real_metrics`, `real_health` with explicit private kubeconfig | All three passed on the final build. Only the external Kubernetes boundary is involved; no internal implementation is faked. |

The expired-YAML regression was discovered through the real desktop comparison.
Four new cases failed before the correction, while the draft-preservation case
already passed (`yaml-expiry-red.txt`). After correction, 249 focused cases passed
in 77.57 seconds. The final complete native suite passed **1,051/1,051** cases in
**191.36 seconds** (`full-native-final.txt`). Earlier overview harness corrections
are not counted as product regressions: an invalid zero-container Pod fixture was
replaced with a valid Pending Pod, and accessible text is asserted through the
public accessibility interface rather than a nonexistent control property.

`final-yaml-expiry-proof.json` separately records the real five-minute cache expiry:
the overview reported no retained detail, entering YAML without clicking Refresh
loaded the real Pod at `2026-10-04T09:34:44.464Z`, and editing became available only
after this accepted fresh read. The preceding detail was retrieved at
`2026-10-04T09:29:00.135Z`. Draft/authentication failure variants are deterministic
public-UI tests with an explicitly fake external Kubernetes HTTP boundary, not
claimed real identity-provider exercises.

#### What Do the Screenshots Prove, and What Remains Open?

`theme-capture-index.json` records **19 themes x dark/light x two implementations**
at subtle intensity. Each selection was asserted through the running application's
public controls before capture. The actual native application was restarted after
its last implementation change, and all 38 native theme/variant captures were
renewed on that executable. `final-executable.sha256` identifies it; the frozen
legacy executable is identified separately by `before-executables.sha256`.

Untouched original PNGs include the resource table plus Pod overview and Appearance
settings for each combination. `comparison-<theme>-<variant>.png` places C# on the
left and C++ on the right without altering either original. Both viewports are
3,024 x 1,896 physical pixels. Contact sheets are reduced navigation aids, not
pixel-resolution visual acceptance evidence. The five inspector views and global
Events, Radar, Alerts and Dashboard surfaces have additional captures. The legacy
Ports view and all nine Settings categories are captured to expose missing native
capabilities, not to imply that a native counterpart already exists.

The palette families and light/dark transitions render coherently in the captured
surfaces. This does **not** close complete visual/functional parity, all-view theme
acceptance, medium/arcade acceptance, keyboard/screen-reader conformance, mobile
support or signed/clean-machine packaging. Concrete remaining issues include the
space consumed by permanent source/import controls, incomplete advanced filters
and Settings/navigation coverage, native port-forward action parity, radar shape
and glyph presentation, and large-radar accessibility traversal. The desktop
accessibility driver temporarily returned only the window root while the native
radar was visibly rendering the full resource population; this remains an
unresolved accessibility/driver boundary, not an assumed pass or proven product
cause. Live native logs visibly contain both `[alpha]` and `[beta]` streams with
`all` selected. The frozen C# log surface does not provide equivalent per-container
labels in the captured latest line; the reference is evidence, not the authority
for weakening LOG-006.

Changing a legacy ComboBox through its advertised accessibility value setter
crashed the frozen reference with `System.NotSupportedException`
(`legacy-accessibility-crash.log`). Its automatically relaunched default-profile
process was immediately terminated, and comparison continued only after an
explicit isolated-profile restart with the exact reference bundle identity. No
screenshots from that accidental default-profile window are retained as evidence.
Keyboard navigation was used for subsequent legacy theme selection. The frozen
reference itself was not modified.

#### Does the Final Build Pass the Release Gate?

No. `final-coverage.txt` measures **96.94% C++ source/header line coverage** and
**81.04% branch coverage** using final-suite and final real-cluster profiles only.
The unchanged 95%/90% gate rejects the result. QML source lines are not LLVM line
coverage; their executed public behaviors are tracked separately above. Passing
component cases, real Kubernetes workflows and screenshots do not establish the
remaining application capabilities or a release package.

The desktop driver completed its normal cleanup. The two replacement test-app
processes were stopped explicitly because the driver held their earlier PIDs.
`cleanup-result.txt` records the exact owned container absent, the private run
profiles removed and no owner-labeled volumes remaining. No global prune was run;
the pinned existing K3s image was retained. The merged profile and source-state
archive are local evidence/recovery artifacts, not release packages.

### Which startup and session-view gaps were closed on 2026-10-04?

Evidence remains under the private comparison evidence directory
`desktop-evidence/2026-10-04-unlocked`; these checks are not a full-parity or
release-readiness claim. Native application profiles and the frozen C# reference
remain isolated.

| Behavior | Public boundary | Test or evidence | Result and remaining gap |
| --- | --- | --- | --- |
| Source controls do not permanently occupy a populated workspace | Native QML Sources control, keyboard, context selection and failed import | `native.ui.sources_*` in Basic/Fusion; `source-panel-tests.txt` | 27 focused checks passed. Empty workspaces expose onboarding; disclosure preserves cached rows and filters. ADR 0026 owns the decision. |
| Source disclosure remains compatible with authentication | Native QML context selection and external identity-provider process | `native.authentication.*`; `auth-polish-tests.txt` | All 27 passed after the test click driver began polishing changed geometry before clicking. The failed shared-401 check was a stale-coordinate test-driver failure, not permission to weaken auth suspension. |
| Ordinary desktop launch needs no CLI profile argument | Actual native executable in an isolated home | `native.application.startup_*`; `startup-tests.txt` | Seven startup scenarios plus five CLI cases passed. Ambient kubeconfig and exec providers are not imported or invoked. Explicit invalid profiles fail without fallback. Default-launch screenshots are real desktop evidence; packaging/mobile launch remain separate gates. |
| Resource filter and ASC/DESC/NONE survive reopen | Actual QML filter and header controls, then a new Workspace/QML engine using the same real store | `native.ui.view_restore_filter/clear/asc/desc/none_*` | Basic and Fusion passed. This is an application-instance boundary, not a substitute for process restart. |
| Events filter/sort has its own saved state | Actual Events filter/header controls and reopen | `native.ui.view_restore_event_filter/event_sort_*` | Basic and Fusion passed. Stable column IDs come from actual schemas. |
| Invalid/stale preferences do not get silently overwritten | Native QML error and explicit Reload saved filters and sorts action | `native.ui.view_restore_corrupt/conflict_*` | Both styles passed; malformed bytes remain intact and an explicit valid reload replaces the local view. |
| Save failure does not silently close a window | Real window close and Stay/Close without saving/Escape controls, with an occupied filesystem lock | `native.ui.view_restore_close_stay/close_discard/close_escape_*` | Both styles passed; normal close waits asynchronously for pending writes. |
| Persistence validates input and preserves unrelated views | Public `ViewStateStore` load/save against private real files | `native.view_state.*` | All 38 scenarios passed: missing state, invalid profiles/IDs/schema, stable IDs, atomic save, lock/conflict, independent table/session state, private permissions and rejected symlinks. Cross-process simultaneous writes and session-removal cleanup still require their own acceptance. |
| A real native process restores the visible view | Regular desktop window close and no-argument process restart against an owned local K3s cluster | `view-state-before-regression.json`, `view-state-restart-proof.json`, `native-view-state-visible-before/after.png` | Failing-first proof lost filter/DESC. Fixed process restores session, `visual-multi-container`, Name DESC and nine actual resource matches. Foreground screenshots were inspected; background snapshots before Raise are not accepted visual proof. |
| Actual ConfigMap YAML and Secret masking/reveal/copy | Native QML and real local K3s HTTPS API | `views-real-ui-final.txt` | Passed with explicitly created, owner-labeled ConfigMap/Secret prerequisites. The earlier run without those prerequisites failed and is retained as `views-real-ui.txt`, not relabeled as a product success. |

Before the view-state increment the full native suite passed 1071/1071
(`startup-full-suite.txt`). Its expanded first run passed 1132/1133 in 215.69 s
(`view-state-full-suite.txt`); the remaining shared-authentication driver issue
was reproduced separately and corrected, followed by 27/27 authentication checks.
A new full suite is required after subsequent production changes. The earlier
coverage result of 96.94% lines / 81.04% branches predates these increments and
still fails the 90% branch gate; it must not be represented as a current passing
gate.

Per-table-type column layouts remain owned by TBL-007, not by session-specific
filter/sort storage. Window layout (STR-002), complete filters/presets, forwarding,
resource deletion, all retained Settings sections, localization, command palette,
platform packages/signing and clean-device runs still block complete native
release parity. The detailed legacy capability ledger remains authoritative.

## Which session and search increments were verified on 2026-10-04?

| Behavior | Public entrypoint | Test/evidence | Remaining gap |
|---|---|---|---|
| Rename a named or automatic session; cancel, Escape, keyboard/context menu, repeat, busy/conflict, restart and preserve filters/draft | Native QML session controls and real session store | `native.ui.session_rename_*`: 24 Basic/Fusion cases; `rename-focused-final.txt`; prior complete suite `rename-full-suite.txt`: 1157 passed | Fresh isolated desktop rename is now captured; the paired C# rename journey remains unverified. |
| A corrupt session view does not block another session's saves | Native QML session switch/filter controls and public view store | `native.ui.view_restore_scope_*`: both failed before the fix; 88 focused cases passed in `view-state-scope-final.txt` | No new Kubernetes transport behavior is introduced. |
| Session-isolation regression against the complete native suite | All registered public native boundaries | `scope-full-suite.txt`: 1159 passed, 442.15 seconds, before the search implementation | Does not establish missing-feature parity or packaging readiness. |
| Search alternatives, quoted exact values, prefix/suffix, regex, escaping, integer comparisons, whitespace, invalid syntax, bounded regex failure, source updates and sorting | Public `ResourceTable`/`ResourceFilter` model entrypoints | `native.filter.*`: 38 cases; expanded cases failed before implementation; `filter-focused.txt`: all passed | This is search semantics, not advanced per-column filters or presets. |
| Search from real controls, error/reset, sort, repeat, keyboard, radar and saved-state restoration | Native QML resource search controls, real view persistence, external Kubernetes HTTP boundary fake only | `native.ui.filter_search_*`: 26 Basic/Fusion cases; combined `filter-ui-focused.txt`: 64 passed | A fresh actual desktop capture is still required. |
| Visual-run deadline validation and execution evidence survives credential/profile cleanup | Real shell runner, real Docker CLI; Docker Engine external boundary fake | `test-native-visual-cleanup.cjs`: 13 passed in `visual-cleanup-tests.txt`; seven new cases failed against the prior script | No global image prune; unrelated/pre-existing resources are protected. |
| Large live local Kubernetes dataset, alerts, metrics and health | Native QML against an owned real local Kubernetes API | `native-kubernetes-final.txt`: `real`, `real_metrics`, `real_health` passed; 1051 deliberately created resources; owned cluster `podlord-visual-run-6sca3m` removed and its private directory removed | Predates the latest view-error isolation and search changes; does not prove all-menu parity. |
| Current C# non-Kubernetes reference suites | Real `dotnet test` | `legacy-current-tests.txt`: 374 passed (91 Core, 214 App, 69 Layout) | Kubernetes suite excluded; its old global cleanup needs correction before safe execution. |

Evidence directory: `/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked`.
The earlier LLVM report was 97.02% lines and 81.46% branches: the required 95%/90%
gate failed. A passed component or suite count must not be used to override that
gate. Search, current full-suite, current coverage and Event-control evidence are
recorded separately when their runs finish. Theme pairs and inspector comparison
images retain their recorded executable versions; they are not relabelled as
screenshots of a later build.

## Which search, radar and owned Kubernetes checks passed on 2026-10-04?

These results extend the existing ledger. They do not establish complete native
feature parity or a release package. Evidence uses the same isolated local
comparison directory as the preceding entries.

| Behavior | Public entrypoint | Evidence | Result and remaining gap |
|---|---|---|---|
| Event search retains independent expressions, errors, sorting and clear/reset behavior | Actual Event QML controls; real stores; external Kubernetes HTTP boundary fake only | `native.ui.filter_event_*`, `radar-release-gate-run.txt` | 16 Basic/Fusion cases passed alongside the 26 resource-search UI cases. No filter-triggered API calls. |
| Complete native suite after cache-search migration | All registered native public boundaries | Complete search-increment suite result | 1,240 passed; this predates the glyph and import-notice increments. |
| Cached search under a real Kubernetes workload | Actual native QML against an explicitly owned local cluster | `search-kubernetes-real.txt`, `radar-glyph-real-search.txt` | 512 ConfigMaps, 256 Secrets, 24 regex matches and 16 alternative matches, invalid/empty/reset handling and shared radar membership passed. Real API data populates the cache; search itself dispatches no requests. |
| Repeatable model search cost | Public resource-filter model, 5,000 real model rows; 50 measured iterations after warmup | `search-benchmark.json` | p50 9.014417 ms, p95 13.547459 ms; model boundary only, not whole-UI latency or a C# performance comparison. |
| Known and unknown resource-kind glyphs | Actual QML glyph rendering and captured window pixels | `native.glyph.*`, `radar-catalog-focused.txt` | 51 cases cover the 24 known kinds, unknown/empty kinds, shared-shape aliases, updates, unchanged values, invalid colors, visibility, zero size, aspect ratio, resize and two scale factors. The shared C# glyph catalog is retained; this is not pixel-equivalence proof for its separate RadarBlockLayer renderer. |
| Glyphs follow filtered/reused radar delegates without requests | Actual radar controls in Basic/Fusion | `native.ui.radar_glyph_*`, `radar-glyph-before.txt`, `radar-catalog-focused.txt` | Failing-first regression followed by passing focused evidence; 66 glyph/radar/metric-tooltip checks passed. |
| Complete native suite after glyph migration | All registered native public boundaries | `radar-all-suite.txt` | 1,295/1,295 passed in 381.79 seconds. Later import-notice/navigation edits require a new complete run. |
| Local Kubernetes search, alerts, metrics and health after glyph migration | Actual native QML and real local K3s HTTPS API | `radar-glyph-real-search.txt`, `radar-glyph-real.txt`, `radar-glyph-real_metrics.txt`, `radar-glyph-real_health.txt` | All four workflows passed against 1,051 deliberately created resources. Application processes, owned cluster and private credential/profile directory were cleaned; coverage profiles were retained as evidence. |
| Actual desktop glyphs and session rename | Running isolated native desktop application against that owned local cluster | `native-radar-glyphs-64.png`, `native-session-rename-dialog.png`, `native-session-rename-result.png` and accessibility snapshots | Renaming Unnamed to Local radar review preserved session ID, the 24-match regex filter and tile size 64. Cached inspector selection remained operable. |
| Current C# and native radar presentation | Untouched desktop screenshots composed side by side | `comparison-radar-current.png` | C# is left, C++ right. Filters, populations and zoom differ, so this exposes presentation but does not prove equivalent behavior or layout. Full radar accessibility traversal still needs evidence; the capture driver returned only the window root for the larger population. |
| Success notice is hidden in a collapsed active workspace, available on reopen; partial failures remain visible | Actual Sources/import controls and real file import/store | `native.ui.import_notice_*`, `import-notice-before.txt`, `import-notice-final-focused.txt` | Six Basic/Fusion regressions plus related Sources checks: 22/22 passed. Missing-path errors and per-file reports are distinct public outcomes; an incorrect test assumption was corrected rather than changing that contract. |
| Fast repeated Radar Home/End remains navigable | Actual keyboard input and rendered frames with 1,001 resources | `native.ui.radar_navigation_repeat_*`, `radar-rendered-before.txt`, `radar-visible-model.txt` | Both styles reproduced the prior lifecycle defect; all three focused large-radar checks passed with the visible-model binding. An early test-driver attempt sent input at height zero; a real rendered frame is now awaited. Navigation asserts an actually exposed last resource, not a positive internal scroll coordinate. Pre-fix timing is invalid as a success measurement. |
| Initial visible radar population is not just a correct count | Actual rendered resource buttons with 1,001 cached resources | `native.ui.radar_initial_population_*`, `radar-population-before.txt`, `radar1001-before.png` | Both styles failed before the lifecycle correction. The frame exposed only one row while the count reported 1,001. A physically displayed second-row resource is now required; the new full-suite result records its acceptance. The external Kubernetes HTTP boundary is fake in this deterministic regression, not presented as a real-cluster screenshot. |
| Reopening preserves the actual visible end resource | Actual Resources/Radar controls, rendered buttons and unchanged external request trace | `native.ui.radar_reopen_position_*`, `radar-release-gate-run.txt` | Both styles passed. This tests view-detach/reattach behavior rather than assuming retained numeric state proves visible restoration. |
| Local test cleanup protects unrelated containers, including k3d's unlabelled helper | Real shell helper and Docker/k3d CLIs; external Docker Engine HTTP boundary fake only | `test-k3d-cleanup.cjs`, `k3d-tools-cleanup-before.txt`, `k3d-tools-cleanup-focused.txt`, ADR 0028 | 13 passed. A helper is admitted only with the recorded full creation ID and exact name/app/role/cluster metadata. Unknown or foreign ownership fails closed. The fake's successful admission case deliberately fails the external deletion step; real cleanup evidence is separate. |
| Current C# suites including real Kubernetes workflows | Public `scripts/test.sh`, all four real test assemblies, owned local k3d cluster | `legacy-full-owned-run.txt` | 458/458 passed: 91 Core, 214 App, 69 Layout and 84 Kubernetes. The earlier Kubernetes setup failure was real local DiskPressure, not 25 independently established product defects. The disposable node now uses the native runner's disk threshold, preserving memory/inode safeguards. |
| Current C# coverage and cleanup | Existing coverage gate and public runner exit cleanup | `legacy-full-owned-run.txt`, `legacy-owned-run-results` | Line 94.95% fails the existing 95% gate; branch 83.90% passes its existing 80% gate. No task-owner containers or volumes remained. Results were preserved in the evidence directory and the temporary run directory removed. Passing tests therefore do not imply a passing legacy pipeline or the native 90% branch gate. |
| Fresh native local Kubernetes run after radar lifecycle correction | Public `test-native-visual-kubernetes.sh native-e2e`, real local Kubernetes and actual QML | `radar-current-kubernetes-real.txt` | All three registered real alarm/metrics/health workflows passed; the owned cluster and private profiles were cleaned. This runner does not currently execute the separately proven `real_search` workflow, so it is not labelled as a fresh four-workflow run. |

The pre-glyph native report remains explicitly failing: 97.08% lines and 81.87%
branches against the unchanged 95%/90% gate. A current post-navigation native
report is required; it must include every executable that exercises production
code, including the new rendered-glyph executable. QML behavior evidence is not
LLVM source-line coverage.

Spatial radar layout and continuous pan/zoom, complete per-column filters and
presets, native forwarding and deletion, retained Settings capabilities,
localization, multi-window lifecycle, all-current-view/theme comparisons,
clean-device packaging/signing and real target-device runs remain release gaps.
Neither screenshot composition nor a passed local Kubernetes workflow removes
those gaps.

### What Did the Pre-Lifecycle Native Gate Establish?

`radar-release-gate-run.txt` records **1,307/1,307 passed**, 548.68 seconds.
This includes initial rendered population, repeated navigation, position retention
on reopening, source notices and all retained native scenarios. The real local
Kubernetes workflows ran separately; suite elapsed time is not a UI benchmark.
That run predates the subsequent animation-lifecycle and metric-tooltip changes.
An intermediate measurement-driver change separates frame delivery from screenshot
capture; its same two Basic/Fusion navigation scenarios passed again in
`radar-frame-repeat-tests.txt`. The newer implementation is recorded separately
below rather than inheriting this older complete-suite result.

The existing public native runner now includes all production-exercising test
executables in the LLVM report, including rendered glyphs. Its current complete
suite profile is preserved as `radar-current-suite.profdata`, with
`radar-current-suite-coverage.txt`: **5,074/5,241 lines (96.81%)** and
**4,540/5,632 branches (80.61%)**. The unchanged 95%/90% gate **fails**.
This suite-only report and the preceding combined/manual-evidence report cover
different execution sets; their percentages are not a before/after performance
or coverage-regression comparison.

`radar1001-before.png` and `radar1001-after.png` show the deterministic regression
at the external fake Kubernetes boundary. The corrected view fills its viewport
from the same 1,001-resource cache instead of exposing only one row. Those frames
are native before/after evidence, not C#/C++ parity or real-cluster captures.

`radar-navigation-frame-benchmark.txt` measures 50 keyboard navigations after five
warmups through that real native QML/software-rendering boundary: **p50 28.876 ms,
p95 33.336 ms, maximum 34.167 ms** to `frameSwapped`. Including synchronous
screenshot capture, p95 is **34.608 ms**. Both components are reported; the earlier
screenshot-inclusive sample is retained in `radar-navigation-final-benchmark.txt`.
These results do **not** establish the tightened cached-interaction budget or a
hardware-renderer improvement over C#. Actual hardware/device measurements and
further radar optimization remain open, without disabling required themes,
animations or resource membership to obtain a passing figure.

## Which Radar Animation And Hover Behaviors Were Closed On 2026-10-04?

| Behavior | Public entrypoint and scenario | Evidence | Remaining gap |
|---|---|---|---|
| Offscreen-only alarms do not keep presenting frames; returning to the resource restores applicable motion. | Native QML Radar, Home/End keyboard navigation, real alert persistence/evaluation; `native.ui.radar_pooled_{animation,blink,sweep,none,outline}_{basic,fusion}`. | Original pulse regression produced 13 frames in 250 ms after settling. Pool suspension alone and viewport suspension alone did not fix it: an unrelated invisible sweep still ran. Binding sweep to its own visible effect fixed the root cause. All ten lifecycle cases passed as part of `radar-lifecycle-focused.txt`, 83/83 focused tests. | This is frame-activity evidence, not the 60-second one-core idle-CPU gate. |
| Radar hover presents bounded metric bars and configured reference markers without extra HTTP requests or interpreted resource markup. | Native hover/focus at the external Kubernetes HTTP boundary; `native.alert_ui.metric_radar_gauges` and `metric_radar_gauges_narrow`, plus retained metric tooltip tests. | `radar-gauge-before.txt` failed because the gauges/markers were missing. `radar-gauges-focused.txt` passed 43/43 tests; actual Cocoa/Metal capture `native-radar-gauges-metal.png` was inspected. | No invented recommendation; a sourced recommendation provider remains separate. The 720-pixel test is not an iOS/Android device claim. |
| Radar hover uses real Kubernetes metric values and exposes request/limit markers, stale annotations and unavailable storage. | `scripts/test-native-visual-kubernetes.sh native-e2e`, `real_metrics`; the running application, real cache and public tooltip are exercised against the local k3s API. | `radar-hover-real-kubernetes.txt`: real_search, real, real_metrics and real_health passed against owned cluster `podlord-visual-run-0jvxrx`. The dataset contains 1,051 deliberately created resources. `podlord-visual-run-0jvxrx-real_metrics.log` and inspected `podlord-visual-run-0jvxrx-radar-metrics.png` retain the hover evidence. Cluster/profile cleanup succeeded; exact-owner container and volume queries were empty. | The frame legitimately shows ongoing sync and stale cached usage. It is not a fresh-value, C# comparison or full resource-population benchmark. |
| An incomplete native test build fails before Docker access/provisioning; owned cleanup remains fail-closed. | Public runner CLI with a test-owned partial build; the existing Docker-boundary cleanup scenarios. | `radar-incomplete-build-before.txt` records the failing-first missing-executable regression. `radar-runner-cleanup-tests.txt`: 14/14 passed, 781.46 ms. | Existing cleanup ownership limitations remain; no global prune, old-image deletion or production/shared-cluster operation was introduced. |

The real_search workflow was already present in the runner before this increment.
The earlier three-workflow summary omitted its separate invocation; it was not an
implementation gap. The runner now prints that invocation explicitly and checks
both required test executables before provisioning.

### What Do The Radar Timing Measurements Actually Measure?

The navigation protocol uses the actual native window, 1,001 resources, five
warmups, 50 alternating Home/End actions, nearest-rank p95, and a synchronous
screenshot after each delivered frame. `frameSwapped` latency is reported
separately from screenshot-inclusive latency. The executable is the instrumented
Release comparison build, on macOS 27.0 (26A428); this is one session, not the
agreed three-session/5,000-resource/visible-log performance profile.

Before the lifecycle fix, `radar-navigation-profile-before.txt` reported software
p95 **31.456 ms** to frame delivery; `radar-lifecycle-software-benchmark.txt`
reported **30.743 ms** after the fix. These individual samples do not establish a
repeatable navigation-speed improvement. The measurable regression fixed here is
unnecessary continuing frame activity when no moving effect is visible.

`radar-lifecycle-metal-profile.txt` records the actual Cocoa/Metal window: action
to frame delivery p50 **14.563 ms**, p95 **19.386 ms**, maximum **19.961 ms**;
screenshot-inclusive p95 **25.094 ms**. This differs from software rendering and
is not a C# before/after comparison. Renderer/platform labels now come from the
running window rather than the old hardcoded `software` label.

Neither 33 ms software frame delivery nor 19 ms Metal frame delivery is the
8 ms local processing measurement under PER-011. Qt frame profiling separates
polish, synchronization, rendering and display waiting; its coarse diagnostic
trace is preserved but does not establish the complete agreed processing gate.
See the [Qt profiling boundary documentation](https://doc.qt.io/qtcreator/creator-qml-performance-monitor.html).
The actual reference workload, repeated hardware comparison, CPU/RSS budgets,
all-current-theme/view pairs and packaging/platform gates remain open. No required
animation, theme or resource membership was disabled for those timing samples.

### Why Did The First Complete Run Fail Its Log Cadence Check?

`radar-lifecycle-complete-native.txt` recorded **1,318/1,319 passed**, 407.96
seconds; `native.logs.cadence` failed. Three isolated repetitions passed, but the
concurrent Radar/log stress run reproduced the failure after three successful
cadence runs: external ingress times 2,375 ms and 5,363 ms differ by **2,988 ms**.
The observed rows and container requests were correct. The test had compared
remote/event-loop ingress to the local scheduler's cycle contract.

[ADR 0029](../adr/0029-measure-request-cadence-at-local-admission.md) records the
bounded public observation rather than a relaxed timer or hidden test retry. The
request algorithm remains unchanged. The revised public scenario requires
**at least 3,000 ms** between local cycle admissions, actual HTTP requests and
retained contents; ingress times remain diagnostics. `radar-local-cadence-stress.txt`
passed ten cadence repetitions alongside ten real Radar navigation repetitions,
58.77 seconds. These repetitions do not substitute for the current complete suite.

### Which Current Radar Theme Frames Use Real Kubernetes Data?

The hover review found a shared presentation error: unavailable storage inherited
CPU/memory measurement metadata. `radar-storage-before.txt` reproduces it through
the actual tooltip; the common formatter now emits timestamp/window only for an
observed quantity, preserving genuine observed zero. The revised public tooltip
check requires a CPU measurement caption and an explicit no-observed-measurement
storage caption. `radar-storage-focused.txt` passed **58/58**, 6.12 seconds, across
metric parsing/attachment and tooltip/zero/unavailable behavior.

`radar-current-themes-real-kubernetes.txt` subsequently passed real_search, real,
real_metrics and real_health against owned cluster `podlord-visual-run-ccnsx2`.
The native real_metrics workflow now captures all **19 themes x 2 variants = 38**
actual Radar-hover frames, checking the visible measured gauges, bounds and
request/limit markers on each variant. Its `manifest.json` records theme names,
files and observed cache counts: **1,835 cached resources and 9 filter matches**.
The deliberate API dataset remains 1,051 resources; system/generated resources
explain the different cache count. The cluster and private profiles were removed;
exact-owner container and volume queries were empty.

The raw frames are preserved under `podlord-visual-run-ccnsx2-radar-themes/` in the
2026-10-04 comparison evidence directory. `radar-current-dark-theme-hover-overview.png`
and `radar-current-light-theme-hover-overview.png` were inspected as overviews:
these are explicitly cropped/scaled contact sheets, not replacements for the full
raw frames. They show the same real Pod and cached measurements, themed glyphs,
readable metric labels/reference markers and the corrected unavailable-storage
caption. This closes this native hover surface's current theme-capture gap, not
all-view/theme C#/C++ pairing, detailed assistive-technology evidence or spatial
Radar parity. No baseline app or shared/production cluster was modified.

The timing host reports **Mac17,7, Apple M5 Max, 64 GiB RAM**. This supplements the
recorded macOS/build/backend metadata; it does not turn the one-session timing
sample into the agreed three-session reference profile.

### What Does the Complete Gate After the Radar Lifecycle and Metric Fixes Establish?

The current complete native run on 2026-10-04 passed all 1,319 tests in
445.70 seconds. Its retained output is
`/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked/radar-current-complete-native.txt`.
This run includes the viewport/pooling animation regressions, shared Radar
metric hover, unavailable-measurement timestamp correction, and local log
admission cadence assertions described above.

The coverage gate remains failed: 5,078 of 5,243 lines (96.85%) meet the 95%
line minimum, but 4,549 of 5,636 branches (80.71%) do not meet the 90% branch
minimum. Passing executed tests does not establish exhaustive reachable
behavior or release readiness.

The final runner cleanup/preflight suite independently passed all 14 tests in
777.290 ms; its output is `radar-current-runner-cleanup-tests.txt` in the same
evidence directory. The current real Kubernetes run passed `real_search`,
`real`, `real_metrics`, and `real_health`, with 38 native Radar hover theme
captures and an empty exact-owner container/volume query after cleanup.

These results establish this Radar increment only. They do not establish
all-view C#/C++ visual parity, measurement-provider attribution parity, the
agreed multi-session performance budgets, complete feature parity, or
signed and clean-machine-tested cross-platform distribution. Those release
requirements remain open.

## Which Native Single-Resource Deletion Paths Were Closed On 2026-10-04?

| Contract / scenario | Public entrypoint and test | Evidence / remaining gap |
| --- | --- | --- |
| ACT-001/003/004/010: scoped frozen confirmation, initial Cancel focus, cancellation, Escape, selection changes, narrow viewport | Rendered Inspector and modal; `native.delete.preview_*`, `cancel_*`, `escape_*`, `selection_*`, `cluster_*`, `narrow_*` | Passed in Basic and Fusion. No deletion before explicit confirmation; no name typing. |
| Discovery support, missing UID and active local draft | Rendered Delete action; `native.delete.unsupported_*`, `no_uid_*`, `draft_*` | Passed. Initial draft cases failed because the enabled binding did not observe YAML-edit notifications; the dedicated availability notification corrected it. |
| ACT-005/006: normal UID-guarded deletion, rejection and finalizers | Real QML/ResourceClient with external Kubernetes HTTP boundary; `success_*`, `object_receipt_*`, `auth_*`, `forbidden_*`, `rate_*`, `redirect_*`, `conflict_*`, `validation_*`, `finalizer_*` | Passed. Exact DeleteOptions body asserted; no grace/propagation override, force escalation, raw response disclosure or automatic login. |
| ACT-007/008/009: transport loss, malformed/negative acknowledgement, server failure, absent/present/recreated target, invalid or unavailable observation | `native.delete.server_*`, `malformed_*`, `failure_receipt_*`, `lost_absent_*`, `lost_present_*`, `lost_recreated_*`, `lost_invalid_*`, `lost_notfound_malformed_*`, `lost_failed_*` | Passed. Original uncertainty remains uncertainty, even after observing absence. Invalid 404 bodies are not accepted as absence. |
| Unresolved read failure survives navigation | `native.delete.lost_failed_navigation_*` | Failing-first: both styles failed in `delete-unresolved-before.txt`. Session-owned observation guard corrected the loss; both styles passed. |
| Explicit observational recovery, new confirmation and repeat protection | `lost_failed_recover_*`, `lost_failed_refresh_*`, `lost_present_retry_*`, `double_confirm_*` | Passed. Read-back sends only GET; another DELETE needs a new reviewed confirmation. |
| Queued versus dispatched lifecycle | `queue_cancel_*`, `queue_cancel_tab_*`, `hidden_*`, `close_tab_*` | Passed. Unsent deletion canceled; dispatched deletion/read-back finish after inspector/tab closure in the original scope. |
| Confirmed replacement deletion cannot resurrect an older cached UID | `native.delete.lost_recreated_retry_*` | Failing-first: both styles reported `deletes=2 safe=1 cachedRows=1` despite acknowledged absence in `delete-generation-before.txt`. Generation-aware absence invalidation corrected it. |
| Real ordinary deletion and a replacement between confirmation and dispatch | Rendered QML against owned Kubernetes, `resource-delete-ui-test real` and `real_race` | Both passed in owned run `podlord-visual-run-xkjs6o`. Race independently verifies the replacement survives the old UID's rejected request, then deletes only after fresh read/new confirmation. The app uses direct API calls; `kubectl` is external test setup/assertion only. |
| ACT-002 multi-resource selection/deletion | No native entrypoint yet | Open release capability; single-target tests do not imply this behavior. |

The focused current deletion suite passed **74/74** tests in **53.11 seconds**.
The first broader run passed 148/150; its two draft-state failures are retained in
`delete-focused.txt`, followed by the correction and 50/50, 52/52 and 68/68
intermediate runs. The initial attempted pre-implementation test had a filter
setup error; it is not represented as a reproduced missing-UI regression.

The final owned Kubernetes lane passed six scenarios: deletion `real`, deletion
`real_race`, `real_search`, alert `real`, `real_metrics`, and `real_health`.
`delete-current-real-kubernetes.txt` and
`podlord-visual-run-xkjs6o-delete-real{,_race}.log` retain the results.
The earlier owned run `podlord-visual-run-qf5fpb` passed ordinary deletion but
failed the replacement case; its cluster was cleaned. The minimal external-API
regression above localized the stale-generation defect before the successful
rerun. Exact-owner container queries after both runs were empty.

The runner now freezes its three native test executables into its private run
before dependency startup and records their hashes and byte sizes in
`podlord-visual-run-xkjs6o-test-builds.json`. These are public-QML test executable
identities, not installed-release or cross-platform packaging identities. It
cleans the copies together with test kubeconfig/profile/container/volumes and
retains only explicit evidence and coverage outputs. The current ownership,
cleanup and preflight suite passed **14/14**, in **1,359.753 ms**.

Current native confirmation frames are
`podlord-visual-run-xkjs6o-delete-real-confirmation.png` and
`podlord-visual-run-xkjs6o-delete-real_race-confirmation.png` in the established
local evidence directory. The latter was inspected directly: cluster/scope,
resource name, UID, irreversible-action warning and visible Cancel focus are
present, with no target text clipping. These are native-only frames, not C#/C++
paired visual proof. The Mac was locked when the direct comparison surface was
requested; desktop comparison remains unverified.

### What Does the Complete Single-Resource Deletion Gate Establish?

After the generation correction, the complete native gate passed **1,393/1,393**
tests in **534.40 seconds**. `delete-current-complete-native.txt` retains the full
output. Coverage remains a failed release gate: **5,300/5,477 lines = 96.77%**
meet the 95% minimum, but **4,800/5,974 branches = 80.35%** do not meet the 90%
minimum. The script exits nonzero instead of treating passing cases as release
permission. Its profile/report are preserved with this increment's evidence.

This is a completed single-target action increment, not complete migration.
Port forwarding, full filter controls/presets, multi-selection deletion, full
settings/diagnostics, detached windows/restoration, localization/command palette,
spatial Radar parity, complete paired visuals, agreed performance budgets,
coverage and real signed cross-platform packaging remain release requirements.

## What Proves Native Inspector Back/Forward Behavior?

`inspector-navigation-ui-test` drives the real Workspace, QML controls, platform
keyboard binding, request queue and caches. Its isolated HTTP server replaces
only the external Kubernetes API. There are 22 separate scenarios in both Basic
and Fusion, not private-history-state assertions.

| Behavior | Public entrypoint / scenarios | Evidence |
| --- | --- | --- |
| Visible controls, initial/repeat state, invalid directions | Inspector controls and `navigateInspector`; `initial`, `repeat`, `invalid_zero`, `invalid_positive`, `invalid_negative` | Passed. `history-before.txt` reproduces the missing controls before implementation. |
| Cached navigation, native keyboard, narrow layout, filtered-out resources | Back/Forward and platform Back shortcut; `back`, `forward`, `keyboard`, `narrow`, `filter`, `cached` | Passed. Availability and getters make no requests; explicit navigation uses the existing foreground queue. |
| Unsaved draft choice and frozen target disappearance | Real editor and discard dialog; `draft_stay`, `draft_discard`, `draft_escape`, `draft_missing` | Passed. No silent discard, write or target substitution. |
| Session isolation, close/reopen, retained forward branch and visit bound | Public session/Inspector actions; `session`, `close`, `branch`, `limit` | Passed. 32 visits retained; independent sessions do not inherit history. |
| API/auth failure and removed resources | `failure`, `auth`, `missing` | Passed. Cached navigation survives failure without automatic login; collection removal excludes old detail-only entries. |
| Real Kubernetes history | `inspector-navigation-ui-test real` in owned run `podlord-visual-run-kztjhm` | Passed against the same 1,051-resource local cluster as deletion/search/alerts/metrics/health. |

The broader focused suite passed **237/237** cases in **116.72 seconds** in
`history-current-focused.txt`. Initial session/limit failures came from test
setup and were corrected without changing their intended history behavior.
The missing-resource cases then exposed a production defect: detail-cache
fallback incorrectly counted as collection membership. Both styles reproduced
it in `history-missing-diagnostic.txt`; all four missing/draft-missing cases
passed after the authority correction in `history-cache-membership.txt`.

The real lane passed all **seven** scenarios in
`history-current-real-kubernetes.txt`. Its private executable copies and hashes
now cover four binaries. The container/profile/copies were cleaned by the
ownership-checked runner. Its cleanup/preflight tests passed **14/14** in
**1,451.332 ms** in `history-current-runner-cleanup.txt`; an earlier invocation
omitted the required build-directory environment variable and is not represented
as a product regression.

`podlord-visual-run-kztjhm-inspector-history.png` is rendered native QML evidence,
not paired C#/C++ visual equivalence. Direct desktop comparison remains blocked
by the locked Mac. The complete current native coverage gate is recorded
separately below once it finishes; earlier deletion coverage is not substituted
for the changed history implementation.

The completed Inspector-history gate passed **1,437/1,437** tests in **504.29
seconds**, retained in `history-current-complete-native.txt`. Its preserved
profile/report show **5,337/5,514 lines = 96.79%** and **4,846/6,022 branches =
80.47%**. The branch threshold remains a failed release gate. Subsequent
port-forward changes require their own current verification; this evidence
does not establish that later implementation's coverage or release readiness.

## What Proves Native Pod And Service Port Forwarding?

`port-forward-ui-test` drives the real Workspace, Inspector, dialogs, request
queue and loopback TCP connections. Its isolated external HTTP/WebSocket server
replaces Kubernetes only. The current suite passes **78/78 cases** in **59.03
seconds** (`forward-list-current-tests.txt`), with each scenario run in Basic
and Fusion.

| Behavior | Public entrypoint / test scenarios | Evidence and remaining gap |
| --- | --- | --- |
| Running Pod and Service eligibility; terminal Pod, empty selector, no backing Pod and UDP rejection | Inspector prepare/start; `pod`, `service`, `service_numeric`, `selector`, `no_pod`, `udp`, `terminal` | Passed. Named and numeric Service ports resolve to the selected backing Pod. |
| Port validation and atomic occupied-port rejection | Real dialog/TCP listener; `invalid`, `invalid_empty`, `invalid_zero`, `invalid_negative`, `invalid_fraction`, `invalid_text`, `invalid_remote`, `busy` | Passed. Rejection starts no API request. |
| Prepared-target stability and cancellation | Rendered dialog; `cancel`, `selection_changed`, `recreated` | Passed. Escape sends nothing; changed selection/Service UID cannot silently retarget. |
| REST target-resolution failures | `auth`, `forbidden`, `rate`, `redirect`, `malformed` | Passed. These do not prove failed WebSocket-upgrade status/header classification. |
| Kubernetes framing and protocol failure | Actual TCP/WebSocket traffic; `protocol`, `wrong_port`, `channel`, `text`, `remote_error` | Passed. Private remote error content is not shown. |
| Ownership, reuse and concurrent local connections | Public controls and TCP; `switch`, `close`, `close_other`, `concurrent`, `queued_close`, `repeat`, `reuse` | Passed. Closing one session does not interrupt another; ports are reusable; unsent abandoned handshakes are removed. |
| Direct Pod forwarding without an extra read permission | `pod_get_forbidden` | Passed. Matches the C# reference's list-plus-forward RBAC behavior. |
| Real-shaped typed lists and contradictory types | `service_list_items`, `service_wrong_list`, `service_wrong_item` | Four failing-first cases in `forward-list-before.txt`; all six pass after correcting type authority. |
| Streaming content negotiation | Pod scenario with external boundary rejecting REST JSON `Accept` | Failing-first `forward-accept-before.txt`; all cases pass with streaming `Accept: */*`. Real run `zqk8gu` independently recorded HTTP 406 before correction. |
| Real Pod and named-Service forwarding, HTTP response, stop and port reuse | `port-forward-ui-test real_pod`, `real_service` | Passed in owned run `podlord-visual-run-s2hifo`; full lane also passed deletion/race, history, search, alerts, metrics and health. |

`forward-list-current-kubernetes.txt` retains the complete **nine-scenario**
real lane. Its five executable identities are recorded in
`podlord-visual-run-s2hifo-test-builds.json`. The helper HTTP process runs inside
the owned Kubernetes Pod; the app uses direct authenticated TLS/API streaming,
not `kubectl`. The test setup's kubeconfig, copied executables, helper binary,
container and volumes were cleaned by the ownership-checked runner. Current
cleanup/preflight tests pass **14/14**, in **1,549.644 ms**.

Earlier runs remain recorded honestly: `m6nuyi` failed Pod readiness; `0zh6ns`
failed the upgrade; `ztw1nc` was interrupted after accidentally freezing an older
binary; `f1mwgj` passed Pod forwarding but failed Service list resolution. Each
owned stack was cleaned. An intermediate complete gate was stopped deliberately
when the Service failure appeared; `forward-interrupted-before-list-fix-native.txt`
is not represented as a completed gate.

`podlord-visual-run-s2hifo-forward-real{_pod,_service}.png` shows native QML
forward tasks after actual data transfer. The Service frame was inspected:
loopback endpoint, session, namespace/target/port, Listening and Stop are visible.
It is not a paired C#/C++ image. Detached windows, task-table/search/open
affordances, full failed-upgrade classification, all-platform execution and
complete paired visuals remain open under LEG-023 and the release contract.

## What Does The Current macOS Package Preflight Establish?

The real Release build has tests and coverage disabled. Its compact package
retains product palettes and Fusion/Basic controls while removing unused toolkit
styles, QWidget controls and unreferenced Timeline plugins. The earlier package
measured 120,778,551 installed bytes and failed the 100-MB gate.

`forward-compact-closed-macos-package/package-evidence.txt` records the current
arm64 artifact: **36,381,250 ZIP bytes**, **98,412,266 installed logical bytes**,
bundled load-dependency closure passed, ad-hoc signature verification passed.
Executable SHA-256: `bd7ed760207f7723c8f4d1b0d85b2843294bef6c3c1cdecf2c645fe81d4c563e`.
ZIP SHA-256: `00ae496382d74172da59f5fa1fd5176792325ac4c310050add8c9fd80dabee67`.

The original path-prefix-only check missed absent Timeline frameworks. The
current public checker also validates Mach-O input and existence of retained
bundle dependencies. Tests use the actual packaged product and private copies,
not invented library fixtures: **7/7 pass**, **3,823.528 ms**, in
`package-dependencies-current.txt`. Corrupt-executable acceptance was reproduced
first in `package-dependencies-before.txt` before the correction. Private copied
artifacts are cleaned even on test failure.

This closes size/static-dependency preflight only. Clean-machine launch, runtime
plugin loading, exact license/source/notice/relinking obligations, Developer ID,
notarization, other targets and complete functional/visual parity are not
established.

### What Did The Complete Forward-Transport Gate Establish?

The complete pre-priority-fix native run passed **1,515/1,515** tests in
**562.71 seconds**, retained in `forward-final-complete-native.txt`. The source
state is retained in `native-source-state-port-forward.tar.gz`. Its production
profile/report show **5,751/5,973 lines = 96.28%** and **5,155/6,468 branches =
79.70%**. The 90% branch gate fails; the script exits nonzero. No runtime file
was excluded or threshold lowered. Later priority/buffer changes require their
own verification, not this earlier source state's result.

### What Does The Priority And Large-Frame Forwarding Gate Establish?

The current forwarding suite passes **86/86** public QML/TCP cases in **64.83
seconds** (`forward-flow-final-tests.txt`). Additional scenarios are `priority`,
`bulk`, `large_frame` and `oversized`, each in Basic/Fusion. Failing-first
priority tests observed a pending forward dispatched before Inspector detail;
large-frame tests observed zero delivered bytes for a valid 256-KiB frame.
The corrected scheduling and bounded receive budget remove those failures.
Oversized input closes the stream with generic feedback. Its assertion permits
only a prefix of an earlier valid message already delivered before rejection;
it does not accept any oversized-frame payload.

Owned real lane `podlord-visual-run-sxzjbn` passes all **nine** Kubernetes
scenarios in `forward-flow-final-kubernetes.txt`. The complete source epoch,
retained in `native-source-state-forward-flow.tar.gz`, passes **1,523/1,523**
native tests in **570.04 seconds** (`forward-flow-complete-native.txt`). Coverage
is **5,766/5,985 lines = 96.34%**, **5,158/6,472 branches = 79.70%**. The branch
threshold correctly fails; the complete gate exits nonzero. The later field-filter
increment is not covered by this earlier source epoch.

The actual Release artifact `forward-flow-macos-package` measures **36,381,627
ZIP bytes**, **98,412,458 installed logical bytes**. Static dependency closure
and local ad-hoc verification pass. All **8/8** actual-artifact dependency cases
pass in **4,189.981 ms** (`forward-flow-package-dependencies.txt`), including
missing executable and missing Qt WebSockets, corruption and paths with spaces.
Executable SHA-256: `033fd445ef3e56bbbc04494229de70b0848919028e1903d50132c9e45abdd439`.
ZIP SHA-256: `c0b6f5bfcdc3e642487c692ac05af62c075e860e56a4f773b33bbb2c157b6db0`.
This is size/static-dependency evidence, not signing/notarization/license clearance.

### What Do The Current Desktop Comparison Images Prove?

Owned desktop run `podlord-visual-run-1ujvsu` starts the frozen C# reference and
the above packaged C++ executable against one actual local Kubernetes cluster.
The runner records and cleans its owned applications, container, volumes and
private credentials; it does not touch shared clusters or prune global images.

`desktop-theme-manifest.json` records **19** Appearance theme pairs, selected
through the public UI in both applications, **dark/subtle only**. Each theme has
original captures and a `desktop-theme-<name>-comparison.png` showing C# left and
C++ right. No source screenshot is rescaled. Different window sizes are stated
in the comparison header, not hidden as a pixel-equivalence verdict.

Ten further paired frames cover Resources, Appearance, Events, Ports, Inspector
Overview/Logs/YAML/Events/Links and Radar. The Inspector pairs use the same actual
`visual-a/visual-multi-container` Pod. Native All logs contains both alpha and
beta streams; C# selects All containers. YAML capture waits for fresh reference
YAML instead of treating its initial loading frame as completed behavior.
The Radar captures use a large real resource population including 512 ConfigMaps
and 256 Secrets. Secret values are not opened in this review.

`desktop-forward-flow-review-scope.json` records executable identities and scope.
These frames establish actual rendering/interaction paths only. They expose
remaining differences: resource populations/counts differ, C# retains an
integrated spatial Radar/filter sidebar, and the native Settings surface has
fewer capabilities. One native Radar accessibility observation returned only
window/menu nodes despite a populated visible frame; this requires a dedicated
public accessibility investigation, not a presumed screen-reader pass.
Light variants, medium/arcade intensities, every view/theme combination, matched
viewports, all Settings pages, mobile devices and complete visual/functional
parity remain open. The paired package predates the new text-field picker.

## What Establishes Cached Native Text-Field Filtering?

Owning contracts: FLT-001 through FLT-013, STR-003/004, LEG-012/014; ADR 0031.
`field-filter-ui-test` loads the actual Main QML, private real stores and resource
client. Only the external Kubernetes HTTP boundary is simulated. Each scenario
runs independently in Basic/Fusion, with its own profile/server and cleanup.

| Behavior | Public entrypoint / scenarios | Coverage boundary |
| --- | --- | --- |
| Nine supported text fields | Visible Fields dialog; `name`, `kind`, `namespace`, `status`, `node`, `image`, `cluster`, `owner`, `issue` | Cache membership, explicit field isolation and request-count invariance. |
| AND between fields/search, OR inside an expression | `and`, `global_and`, `or`, `exact`, `prefix`, `suffix`, `regex` | Uses the shared production parser and filtered proxy. |
| Empty/missing/malformed expressions | `empty`, `missing`, `invalid`, `unclosed`, `invalid_picker`, `clear_expression` | Visible error, no accidental unfiltered result, preserved invalid draft and explicit recovery. |
| Multi-value picker/custom alternatives | `picker`, `custom_picker`, `picker_search`, `stable_picker`, `refresh_picker` | Options stay stable during sync; explicit cached reload retains selected absent values; search is local. |
| Keyboard/copy/narrow layout | `keyboard`, `copy`, `narrow` | Public controls and clipboard; not an OS screen-reader verdict. |
| Reset/session/restart/selection/Radar | `reset`, `session`, `restart`, `selection`, `radar` | Existing view persistence, isolated sessions, no implicit Inspector retarget and shared count membership. |
| Versioned view storage | `view_state_test`: `fields_save`, `fields_clear`, `fields_conflict`, `fields_other_table`, `legacy_read`, `legacy_save`, `legacy_repeat`, malformed/unknown fields | Actual private files, atomic save/merge, old reads/no-op saves without rewrite, explicit upgrade and invalid-record preservation. |

Both Name UI cases failed before implementation (`field-filter-before.txt`).
The initial focused run passed 144/150; six picker cases exposed insufficient
layout preparation or prematurely reloaded test snapshots. Public test actions
now wait for layout and the observed refreshed membership, not internal state.
The next 150-case focused run passes; expanded coverage remains subject to its
current result rather than that earlier pass. Typed quantity/age/readiness/restart
filters, Problems/Activity, UID and presets remain open. This text-field scope
must not be relabeled as complete filter or whole-product parity.

## What proves measured-only native quantity filtering?

FLT-013 compares actual cached CPU, memory and storage measurements. A request,
limit or capacity remains reference metadata and never supplies missing usage.
Measured zero is a value; unavailable is not zero. CPU input uses cores or milliCPU,
including displayed mCPU; memory/storage support Kubernetes quantity units and
displayed byte units. Comparisons forming a range must all match. Exact numeric
alternatives use OR. Resource fields and the global search use AND.

The public boundary is the real Main.qml field dialog and Resource/Radar models.
Only the external Kubernetes HTTP and Metrics APIs are simulated in the isolated
cases; source import, discovery, sample normalization, scheduling, stores, filtering,
QML input and rendering are real. Every isolated case checks that filter actions
cause no extra external requests. Both Basic and Fusion run independently.

| Scenario | Public entrypoint | Test | Remaining gap |
| --- | --- | --- | --- |
| Cores, milliCPU, displayed units, decimal/exponent/fraction values | Field dialog keyboard input | native.metric_filter.cpu_* | Device-specific keyboard/IME |
| Binary/decimal byte quantities and measured zero | Field dialog keyboard input | native.metric_filter.memory_* | Other-platform rendering |
| Missing usage with configured requests/limits/capacity | Same field dialog | cpu_missing, cpu_limit, memory_missing, memory_limit, storage_capacity, storage_limit, storage_zero | Positive storage usage requires a real supported storage measurement source |
| Range conjunction, exact alternatives, field conjunction | Same field dialog | cpu_range, cpu_range_reverse, cpu_conflict, cpu_alternatives, cpu_range_alternatives, and | Age/readiness/restart predicates |
| Invalid/overflow/underflow quantities and recovery by clearing | Same field dialog | negative, overflow, underflow, bad_unit, bad_operator, missing_operand, clear | Text/regex fallback for legacy quantity displays is not implemented |
| Stale/incomplete measurements retain their actual numeric values | Real sample ingress and field dialog | stale, incomplete | Extended stale-sample lifecycle matrix |
| Shared filtered Radar | Field dialog then Radar tab | radar | Spatial Radar parity |
| Dense real inventory and live Metrics API | Source import, field dialog, Radar | metric-filter-ui-test real <owned kubeconfig> | Results pending until the real lane completes |

Failing-first evidence: metric-filter-before.txt, two public UI failures for missing
quantity fields. The expanded isolated gate is metric-field-filter-expanded-tests.txt:
254/254 passed in 111.33 seconds, including the existing text/parser/view-store cases
selected by that invocation. Earlier canonical-only runs caught integer QVariant
storage and spaced-operator issues; they are retained as failure evidence, not passes.

The complete native gate and real Kubernetes lane are running separately. The
existing 1523-case full gate, 29 paired desktop screenshots and macOS package
precede this field-filter source epoch and do not prove the new code.

The first real field-filter run (podlord-visual-run-luckno) passed the nine existing
Kubernetes scenarios, then rejected a wrong test expectation: two namespaces contain
128 owned ConfigMaps plus two Kubernetes-created kube-root-ca.crt ConfigMaps. The
actual 130 cached matches were correct. The test now compares complete filtered
identities against the pre-action public cache snapshot and separately establishes
128 owned ConfigMaps; the Radar lane narrows Name to ~visual-config- before asserting
128 tiles. Production filtering was not changed to suppress valid cluster objects.
The failed lane cleaned its owned container and volumes. Its log remains failure
evidence; the corrected real lane is a separate run.

Measured-only source epoch: metric-field-final-complete-native.txt records
1700/1700 passes in 751.45 seconds. Lines are 5986/6207 (96.44%); branches are
5396/6732 (80.15%), so the unchanged 95%/90% gate correctly exits with failure.
The new ResourceFilter itself reports 99.19% lines and 93.01% branches, but that
cannot satisfy the whole-product branch requirement. Profiles/report are preserved
as metric-field-native.profdata and metric-field-coverage-report.txt before further
implementation changes.

The corrected real lane (podlord-visual-run-wzirdk) passes all ten scenarios,
including exact cached fields, AND/OR, eight actual CPU and eight actual memory
measurements, no storage-usage substitution, and 128 narrowed Radar tiles. It
cleans its owned container and volumes. Actual rendered evidence is in the
podlord-visual-run-wzirdk-field-filters-{text,cpu,radar}.png files.

The matching Release macOS packaging preflight passes with a 36400720-byte ZIP
and 98447450 installed logical bytes; eight packaged-artifact dependency cases
pass. This is ad-hoc local signing only. The executable SHA-256 is
b178ecc62637586db3f4dcfd4d1d1cdc1013068e21236e03bd78b7d4d93110d1.
The ZIP SHA-256 is
8419e7fef50f1a5bde0e019bdfd9369d4fc03ca06e2b02309340042849fa9273.
These measurements precede the next quantity-display compatibility/centering
increment and do not prove Developer ID, notarization, clean-device deployment,
other platforms or whole-product parity.

## What closes the native quantity-display compatibility gap?

The follow-up covers display_regex, display_prefix, display_suffix, display_contains,
display_unavailable, display_regex_unavailable, display_stale, display_stale_exact,
display_incomplete and display_mixed through the same public QML field controls.
It also covers cpu_millicores, cpu_alias_c, cpu_upper_m, memory_lowercase and
memory_lowercase_range. Source ingestion is unchanged; these are human filter
aliases. center and narrow_center assert observable dialog geometry through the
public Dialog title/properties on both styles. Every case still asserts no extra
external requests from filtering.

Failing-first evidence is metric-display-before.txt, metric-dialog-position-before.txt
and metric-alias-before.txt. The current focused gate, metric-display-focused-tests.txt,
passes 288/288 in 151.22 seconds. It includes the prior numeric/text/parser/view-store
cases selected by that invocation. The valid quantity regex rejection case was
obsolete and now checks malformed regex; valid regex has separate positive cases.

The real lane additionally checks a quantity-display regex against actual measured
identities and records twenty repeated public keyboard-input-to-completed-frame
samples on the dense real cache. It reports software-backend timings, not a native
GPU/cross-device performance guarantee. The completed production source epoch is
recorded below; the previous 1700-case full gate remains tied to the preceding
measured-only source epoch, not this compatibility increment.

The owned podlord-visual-run-jguleb cluster passed all ten native-e2e scenarios.
Its field lane rendered 1847 cached resources, matched eight actual CPU and eight
actual memory identities, rejected missing storage measurements, and showed 128
filtered Radar resources. The twenty public keyboard-input-to-captured-frame
samples measured p50 5.084521 ms, p95 6.949208 ms and maximum 14.959167 ms on
Qt 6.11.2's offscreen software backend. These are not the isolated 8 ms local
frame-work metric or a native GPU/device-family claim. Evidence is
metric-display-health-diagnostic-real.txt and
podlord-visual-run-jguleb-field-filters.log. The centered CPU dialog frame was
visually inspected. Owned containers, volumes, private kubeconfig and profiles
were cleaned; preserved coverage profiles remain separately labeled evidence.

The earlier podlord-visual-run-y125hw run failed in real_health before entering
the field lane. Its generic failure could not identify the failing assertion.
Stage-specific public-state diagnostics were added, and the next real_health
passed. The original intermittent failure is not claimed fixed and remains a
release-stability investigation; its log is retained.

metric-display-macos-package passed bundled dependency and ad-hoc signing
preflights plus all eight packaged-artifact dependency tests in 4198.26425 ms.
Its ZIP is 36402530 bytes and installed regular-file logical sum is 98447482
bytes. Executable SHA-256 is
426c01c6d02b5e5417c7b5657f66c960150538f11730b08556ec186dea9adc28;
ZIP SHA-256 is
eb29d41da1b3eb6e4254ed9c320d122c9b6ec73a1276ff5a7050b19465f26d85.
This artifact precedes the Ready/Restarts control increment. It is neither
notarized nor clean-device, licensing, complete parity or cross-platform evidence.

## How are Ready and Restarts field filters migrated without another engine?

The actual C# CompiledQuery uses its text matcher for Ready fractions and its
OR-alternative integer matcher for Restarts. Native now exposes these two
existing column IDs through the shared picker/compiler/view-state path, without
a new parser, timer, measurement provider or persisted record. Ready does not
reinterpret 1/2 as a percentage or count. Restarts <1 >3 means either comparison,
unlike AND quantity ranges. Missing restart observations do not become zero.

The 21 scenarios each run through real public QML controls on Basic and Fusion:
ready_exact, ready_unready, ready_regex, ready_prefix, ready_suffix,
ready_alternatives, ready_unavailable, ready_numeric, ready_picker, ready_invalid,
restart_zero, restart_greater, restart_alias, restart_alternatives, restart_text,
restart_missing, restart_no_match, restart_or, restart_and, restart_radar and
restart_picker. Their external fake supplies real-shaped Kubernetes responses;
owned parsing, filtering, persistence and rendering remain real. Every isolated
case checks unchanged external request count. ready-restart-before.txt records
42/42 failures before exposing these fields. ready-restart-focused-tests.txt
passes all 291 selected quantity/text-field/view-store cases in 191.95 seconds,
including all 42 new cases. The separate ready-restart-parser-tests.txt passes
all 39 shared parser cases in 0.75 seconds. These are two invocations, not one
330-case result.

The targeted real podlord-visual-run-d1solf lane passed on 1834 cached resources:
seven actual CPU and memory identities, eight observed restart counts, missing
storage measurements, the real two-container Pod's Ready 2/2 under Kind=Pod,
and 128 filtered Radar resources. Its twenty software-backend keyboard-to-frame
samples measured p50 3.432375 ms, p95 4.76475 ms and maximum 4.787583 ms.
Evidence is ready-restart-real-kubernetes-retest.txt and
podlord-visual-run-d1solf-field-filters.log, with text/cpu/ready/restarts/radar
frames saved under that same prefix. Its cluster and volumes were removed and
private credentials cleaned. The initial podlord-visual-run-uvxwfz lane correctly
returned three Ready=2/2 resources: Pod, Deployment and ReplicaSet. The test's
one-Pod assumption was corrected with the explicit Kind filter; no production
behavior was changed to satisfy that obsolete expectation.

ready-restart-macos-package passed closure/ad-hoc signing and all eight artifact
dependency checks in 6082.112708 ms. Download ZIP is 36402607 bytes; installed
regular-file logical sum is 98447482 bytes. Executable SHA-256 is
7b47984f4886183e88a3cfa8e7e858cafb05fbfa6a0f1d761741d0d466ba5ff7;
ZIP SHA-256 is
2ab8f5e3d3781d5b753b6635b62384a681f3de74a885affe655a23678fab4a77.
The native test executable uses actual product runtime/QML with an owned real
Kubernetes boundary; it is not a clean-device installed-package UI claim.
Notarization, licensing and platform/visual parity remain separate release gates.
ready-restart-complete-native.txt passes all 1776 native tests in 811.06 seconds.
The coverage gate correctly exits 1: lines are 6002/6223 (96.45%, passing 95%),
branches are 5421/6758 (80.22%, failing 90%). The threshold was not lowered.
The exact profile/report are retained as ready-restart-native.profdata and
ready-restart-coverage-report.txt. Source inputs are archived separately as
native-source-state-ready-restart.tar.gz. This all-green behavior run does not
erase the branch-coverage, parity or distribution blockers.

The retained C# restart projection returns zero when container status/count data
is absent, while the native projection currently shows unavailable and excludes
it from numeric matches. The 42 UI cases prove the native behavior, not approval
of that difference. FLT-013's actual-measurement decision explicitly covers CPU,
Memory and Storage only. The Restart missing-value rule remains a specification
decision and a compatibility blocker; this increment is not declared complete
parity solely because its current-behavior tests pass.

The native-fields-e2e runner mode starts the same owned dense Kubernetes dataset
but runs only the field public-boundary lane. The full native-e2e mode retains
all ten scenarios. Both use the same child-process owner and cleanup trap,
including interruption of an active field test. The test executable hash is
saved before it runs. This targeted lane does not replace the full regression
gate or claim the omitted scenarios passed in its invocation.

### How is on-demand radar presentation verified?

The 2026-10-05 increment changes only native radar presentation: metric tooltip data is read lazily from the existing cache model; animation objects exist only for visible animated tiles. Animation factors belong to the disposable effect item, while the tile derives its opacity and scale declaratively. Disabling motion cannot leave a tile dimmed or shrunken. No discovery, network request, cache policy, alert rule, filter, or spatial layout semantics changed.

| Public behavior | Entrypoint | Executable evidence | Remaining gap |
| --- | --- | --- | --- |
| Blink, pulse and sweep change rendered pixels; outline stays visibly static | Alert configuration API plus actual QML window capture | `native.ui.radar_animation_{blink,pulse,sweep,outline}_{Basic,Fusion}` | Native GPU/device matrix |
| Reduced motion restores the color-only tile, without further pixel changes or requests | Alert preferences API plus actual QML window capture | `native.ui.radar_animation_reduce_{blink,pulse}_{Basic,Fusion}` | OS accessibility preference integration |
| The above six scenarios use independent private profiles and a minimal external Kubernetes HTTP boundary | Real stores, scheduler, alert evaluator, cache, QML and image capture | Twelve passing tests in `radar-action-fixed-tests.txt`; four reduced-motion failures retained before the fix | No claim that these cases cover all alert lifecycle paths |
| 5,000 cached Pods switch between dashboard, radar and table without reads | Existing public tab-switch-to-rendered-frame benchmark | Three isolated software-backend runs before and after the increment | Native GPU, multiple sessions, sustained logs and RSS/CPU measurements |

Evidence directory: `/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked`.

For the radar-only samples, the median of the three run medians improves from 73.056 ms to 53.549 ms (26.7%). The median of the three run p95 values improves from 90.289 ms to 68.562 ms (24.1%). Each run contains thirty overall tab switches; the radar subset is ten samples, so its nearest-rank p95 is the subset maximum. The 50 ms target is still not achieved. These are local offscreen/software observations, not a cross-platform or GPU release claim. Authoritative measurements are `radar-isolated-before-{1,2,3}.txt` and `radar-final-after-{1,2,3}.txt`.

The initial metric-only test selection included older linked workspace UI executables. Subsequent final evidence rebuilds all targets. `radar-metrics-repeat-*` is another metric-only run, not proof of the later animation-loader change. Only final-epoch results may be used to assess that change.

The Kubernetes runner now exports an owned `RUN/tmp` for child processes after installing its cleanup traps. This closes temporary-directory ownership outside the run. Normal and interrupted cleanup still require executable evidence; no global pruning or deletion of shared images is authorized.

Final linked-epoch focused run: 334/334 tests passed in 240.83 seconds (`radar-final-focused.txt`). This includes the twelve new render cases; they are not an additional 12 distinct cases beyond that selection.

Owned real Kubernetes run `podlord-visual-run-tzqcar`: all ten UI scenarios passed, including Pod/Service forwarding, delete and delete/recreate races, inspector history, cache-only search, details/YAML/Secrets, real metrics, health and field filters. The field lane observed 1,846 cached records, eight actual CPU and eight actual memory samples, unavailable storage, a Ready 2/2 Pod, nine observed restart counters, and 128 filtered ConfigMaps in radar. Twenty public filter-keyboard-to-completed-frame samples measured p50 3.2916875 ms, p95 5.055458 ms, maximum 6.046417 ms on the software backend. The lazy tooltip still displays actual measurements and reference markers in `podlord-visual-run-tzqcar-radar-metrics.png`. Normal cleanup removed the owned container, volumes, processes, credentials and run directory; no shared resources were pruned.

### Does interruption also remove the real UI profile and stack?

`scripts/test-native-visual-interruption.mjs` exercises the real runner and real local Kubernetes. Each case pins a local Docker endpoint, provides a private ambient TMPDIR, waits for the actual owned UI child and its private profile, signals only that runner, and verifies its child, labelled container, exact volumes and run directory are gone. The private driver TMPDIR is removed even on a failed assertion. A frozen pre-fix runner fails because its UI profile is outside the owned run; the corrected runner passes SIGTERM, SIGINT and SIGHUP. Evidence: `radar-cleanup-before.txt`, `radar-cleanup-sigterm.txt`, `radar-cleanup-SIGINT.txt`, `radar-cleanup-SIGHUP.txt`, plus each run's `cleanup-proof.json`. SIGKILL, a failed daemon and abrupt host shutdown cannot be inferred from these signal cases.

The existing `scripts/test-native-visual-cleanup.cjs` remains the separate, minimal external Docker Engine failure-boundary suite. All 15 cases pass after the required field-filter executable was represented in its incomplete-build tests. These exercise ownership refusal, conflicting creation, lost creation replies, removal errors, coverage retention, preflight rejection and desktop timeout validation. They do not replace the real interruption cases.

### What does the current macOS package preflight prove?

`radar-final-macos-package` is a fresh Release build with tests and coverage instrumentation disabled, produced by the existing narrow packager. ZIP size: 36,402,689 bytes; installed regular-file logical sum: 98,447,482 bytes. Bundled dependency closure and local ad-hoc signature verification pass. Nine actual-artifact tests cover the valid bundle, unused Timeline exclusion, spaces and malformed/missing dependency or executable inputs. The packager also removes unused Timeline frameworks when an SDK deploys them, not merely their plugins and QML directory. The current SDK does not deploy these frameworks, so this run proves their absence, not removal of an actually deployed aggregate-SDK framework.

Executable SHA-256: `2e1fed9dcfeae77601fa4314cb35aafb88c9f085695ce9e928c6a152ce9b4c16`.
ZIP SHA-256: `44a9a439f9e8afd8ded1f26b20218e745fd159cba110b2f1f8c55b005f4898a6`.

The broad raw deployment attempt failed its dependency closure and was removed, together with its redundant build directory; its diagnostic logs remain. It is not a distributable artifact. Package closure, file sizes, ad-hoc signing and a CLI loader probe do not establish fresh-device GUI startup, runtime-loaded QML coverage, license compliance, Developer ID/notarization, whole visual/function parity or any other platform.

## What completed the on-demand radar gate?

The final on-demand radar source state completed **1,788/1,788 native cases in
938.44 seconds**. Its line gate passed at **6,003/6,223 (96.46%)**; its branch gate
remains below the required minimum at **5,425/6,758 (80.28%)**. The coverage runner
therefore exited unsuccessfully despite every executed testcase passing. Retained
reports: `radar-final-complete-native.txt`, `radar-final-native.profdata` and
`radar-final-coverage-report.txt`.

The real local Kubernetes run also retained **38 original radar theme frames**
(19 themes, dark and light) in `podlord-visual-run-tzqcar-radar-themes/`.
`radar-real-themes-overview.png` is a labelled-index overview;
`radar-real-theme-source-frames.json` identifies its original frames. The overview
was visually inspected. These are native software-rendered frames from the real
local resource inventory, not a new same-fixture C#/C++ comparison, an approval
of every theme variant/intensity, or evidence for other devices. Raw owned
coverage files were compacted into retained `.profdata` without removing the
original screenshot or failure evidence.

## Does source import accept paths and file URLs at one public boundary?

The file and folder dialogs now pass their selected URL unchanged. The existing
`KubeconfigStore` input boundary uses Qt local-file conversion instead of QML
prefix stripping and JavaScript percent decoding. Plain paths and supported
home-relative paths retain their existing behavior. URL and path representations
of the same file retain the same content-addressed snapshot identity (SES-003).
No authentication or cluster connection is introduced by importing a source.

| Behavior | Public entrypoint | Test | Evidence and remaining gaps |
| --- | --- | --- | --- |
| Encoded and display URLs, literal percent sequences, Unicode, uppercase scheme and ordinary paths identify the exact source | `podlord-source --profile … import INPUT` | `native.source_import.file_url`, `pretty_url`, `literal_percent`, `unicode`, `upper_scheme`, `plain_path` | Passed on macOS; actual Windows/Linux/device execution remains open. |
| Re-import by plain path after URL import deduplicates snapshots | Import and subsequent `list` CLI | `native.source_import.repeat` | Same snapshot ID and one retained catalog entry. |
| Recursive import accepts arbitrary extensions and hidden files; healthy files survive a malformed neighbor | Import-path and subsequent `list` CLI | `native.source_import.folder_url`, `folder_plain`, `partial_folder`, `empty_folder` | Exact successful/error counts and retained snapshots; existing folder/lifecycle tests remain in place. |
| Invalid URLs fail explicitly and preserve the source catalog; private profiles cannot be scanned | Import/import-path and subsequent `list` CLI | `native.source_import.query`, `fragment`, `credentials`, `port`, `malformed_percent`, `encoded_null`, `empty_url`, `private_folder` | Explicit input error, no accidental import, prior snapshots retained. |
| URL import populates visible contexts and reports partial folder failures | Real QML source field, Import button and error-details dialog | `native.source_import.ui_file`, `ui_folder`, `ui_partial` | Public UI passed; these do not exercise OS-native chooser interaction on each supported platform. |

Failing-first evidence: the pre-change source executable failed 17 of the initial
19 URL/path cases (`source-url-red-tests.txt`). The final cases reuse
`source_input_test`; the redundant standalone test executable was removed.
**117/117 source-boundary and import-notice cases passed in 6.61 seconds**
(`source-url-consolidated-tests.txt`), including the three added visible UI cases.
The full updated native gate is tracked separately; this focused result is not a
claim of release readiness.

## What does the completed file-URL source increment prove?

The updated native production source passed **1,810/1,810 cases in 747.60
seconds** with four workers and a process-scoped macOS sleep assertion. No
persistent power preference was changed. Coverage is **6,011/6,231 lines
(96.47%, passes 95%)** and **5,442/6,776 branches (80.31%, fails 90%)**. The runner
exited 1 because the branch minimum remains unmet. Reports:
`source-url-awake-complete-native.txt`, `source-url-awake-native.profdata` and
`source-url-awake-coverage-report.txt`.

The first full attempt remains retained as `source-url-complete-native.txt`:
1,795 cases passed and 15 failed/timed out over 3,334.33 seconds. Several nominal
30-second cases spent over 1,000 seconds suspended. Sleep/Dark-Wake events are
retained in `source-url-power-events.txt`; all 15 affected cases passed without
production changes in an isolated 52.26-second run
(`source-url-failed-isolated.txt`). The successful awake full run, not the retry
alone, is the final complete-test evidence. Failed-run coverage was not replaced
with an older successful profile. Special-character fixture names no longer use
characters forbidden in Windows filenames; actual Windows execution is still
unverified.

The real local Kubernetes file-URL lane also passed. Its public QML source field
received a file URL, the Import/Open Context buttons bound the real source, and
cached field/radar behavior ran against **1,833 actual API records**, including
seven CPU and seven memory measurements, unavailable storage, Ready 2/2,
observed restart counts and 128 filtered ConfigMaps. No limit/capacity fallback
was substituted for numeric measurements. For 20 keyboard-to-completed-frame
samples, software rendering measured p50 **3.4035835 ms**, p95 **4.858125 ms** and
max **5.04975 ms**. This is not a native GPU or cross-device measurement.

Owner: `podlord-visual-run-fyxywu`; report: `source-url-real-kubernetes.txt` and
`podlord-visual-run-fyxywu-field-filters.log`; screenshot prefix:
`podlord-visual-run-fyxywu-field-filters-`. The radar frame was inspected. Normal
cleanup removed the owned cluster/profiles, and the exact ownership-label query
returned no container. No global Docker pruning or shared-image deletion was
performed. These frames are not a new C#/C++ native-desktop comparison.

The matching non-instrumented macOS candidate passed **9/9 artifact tests**
(`source-url-package-tests.txt`). ZIP: **36,403,035 bytes**; installed regular-file
logical sum: **98,447,626 bytes**. Executable SHA-256:
`60c382370a33032d80a10b4aa66e54b00e0929bafb3efea1230dbba2e0eeb294`;
ZIP SHA-256:
`a8dcfb15e84ad6081138776f035c74fa79ecdbfc4465aecc6c5a6af224da9f5e`.
The actual Cocoa/CLI `--version` probe loaded no Homebrew libraries
(`source-url-package-loaded-libraries.txt`). Dependency closure and local ad-hoc
signing passed; clean-device GUI, notarization, license obligations, broader
feature parity and other platform gates remain open. The native desktop-control
retry reports a locked Mac, so no new native chooser/C#/C++ visual approval is
claimed.

### How are Radar health highlights, new-issue focus and incremental loading checked?

The accepted behavior is owned by the Radar presentation section of
[podlord-operational-spec.md](podlord-operational-spec.md) and ADR-0025.
These checks use the actual Workspace, stores, cache, alarm evaluator and rendered
QML. Only the external Kubernetes HTTP boundary is simulated in deterministic cases.

| Behavior | Public entrypoint | Test | Evidence / remaining gap |
| --- | --- | --- | --- |
| Readiness warning is yellow, not red | Rendered Radar after source import | `native.alert_ui.radar_health_warning` | Passed; glyph fill and cached hover/render path. |
| Severe Pod failure is red | Rendered Radar after source import | `native.alert_ui.radar_health_critical` | Passed; retained rule-driven effect. |
| New/changed resource is green | Rendered Radar after source import | `native.alert_ui.radar_health_fresh` | Passed; no render-time requests. |
| Restart outlier is yellow, failed population member red | Rendered Radar, real cache health projection | `native.alert_ui.radar_health_restarts` | Passed; existing IQR/floor rule, no Radar-specific threshold. |
| New problem focuses its own resource, unchanged refresh does not replay | Public Refresh button and rendered Radar selection | `native.alert_ui.radar_focus_changed` | Passed; unchanged older matching resource remains present. |
| Filtered-out new problem does not reappear or steal selection | Public Refresh button and filtered Radar | `native.alert_ui.radar_focus_filtered` | Passed; session-wide alarm still evaluates. |
| First collection appears before a delayed second collection completes | Source import, rendered Radar/counts/loading text | `native.alert_ui.radar_loading_partial` | Passed; external HTTP delay, no simulated internal cache. |

The original six regression cases failed before the production correction.
The scoped rebuilt Radar/alarm/theme lane passed 135/135 in 82.20 seconds;
`radar-highlight-regressions.txt` is retained in the comparison evidence directory.
Rendered deterministic frames are `radar-highlight-warning.png`,
`radar-highlight-critical.png` and `radar-highlight-fresh.png`. They prove native
presentation against an external HTTP test boundary, not a real-cluster or C# comparison.

This increment does not establish spatial-map, two-dimensional pan/continuous-zoom,
all-device or release parity. The existing 5000-resource public-frame benchmark in
this concurrent regression lane measured Radar p50 67.693 ms and p95 81.295 ms;
that is not an isolated before/after measurement and remains above the 50 ms goal.
No complete coverage gate was rerun for this increment.

#### Which real Kubernetes and desktop evidence belongs to this increment?

The corrected `alert_ui_test real_health` passed against the owned desktop cluster
`podlord-visual-run-unbqn7`: the Pending PVC in `visual-a` is yellow and the
CrashLoop/error Pod in `visual-b` is red. Namespace scoping uses the public
Workspace filter entrypoint; resource-name search uses actual keyboard input.
`radar-highlight-real-scoped.txt` and its two rendered resource frames retain
that evidence. Two preceding complete E2E attempts failed in the new test's
fixture assumptions (eight equal PVC names, then the wrong CrashLoop namespace);
their logs remain available. No production workaround was added for these test
errors, and those complete lanes are not recorded as passing.

The existing real cached-field scenario then passed on the same owned API:
1834 accepted cache records, seven observed CPU values, seven observed memory
values, unavailable storage, `Ready 2/2`, eight observed restart counts and
128 filtered Radar resources. Keyboard-input-to-completed-software-frame p95 was
4.612 ms. This boundary is not a native-GPU or C#/C++ performance comparison.

`radar-highlight-csharp-cpp.png` places the actual macOS C# window on the left
and current C++ window on the right. Original full-window PNGs and SHA-256s are
retained in `radar-highlight-desktop-manifest.json`. Both used the same owned
Kubernetes API, but their shown inventories differ: C# 256/1412, C++ 1832/1832.
The image therefore demonstrates current visual differences, not equal membership,
spatial-map parity or complete theme approval. The desktop review runner reached
its 600-second deadline after these captures and cleaned up; the overall desktop
lane is not a passing acceptance gate.

All three containers from this increment were confirmed absent by their exact
ownership labels. The runners also check their owned volumes and remove temporary
profiles. Raw coverage profiles were merged and compacted; evidence images/logs
remain. No shared container/image pruning was performed. Spatial layout, 2D
navigation, continuous zoom and the complete Radar performance/visual gates remain
open.

### Which deterministic native island and focused-navigation paths are covered?

The spatial/input contract is owned by the operational specification and
[ADR 0032](../adr/0032-native-deterministic-radar-island.md). The rectangular native
GridView and its one-dimensional camera APIs are removed. Only the external
Kubernetes HTTP boundary is simulated in the following real-QML regressions;
application stores, cache, proxy, geometry, glyphs and input handling are real.
Each scenario runs in Basic and Fusion.

| Behavior | Public entrypoint | Test scenario | Remaining gap |
|---|---|---|---|
| Island rather than sequential rows | Rendered Radar resource positions | `native.ui.radar_island_layout_*` | Exact C# coordinates are not promised: native resource IDs differ. |
| Two-axis movement, reset, input-focus isolation | Arrow/zero keys and filter focus | `native.ui.radar_island_pan_*` | Other-device input matrices remain release work. |
| Stable surviving positions and no fetch | Public search field | `native.ui.radar_island_filter_*` | Full retained filter/preset catalog has its own gaps. |
| Drag is not inspection; click is inspection | Mouse press/move/release on a resource, then click | `native.ui.radar_island_drag_*` | Touch-device gestures are unverified. |
| Trackpad zoom without fetch | Pixel-delta wheel event through the window | `native.ui.radar_island_trackpad_zoom_*` | Physical device/OS gesture differences are unverified. |
| Continuous pointer-anchored zoom | Angle-delta wheel event through the window | `native.ui.radar_island_zoom_*` | Broad device coverage remains open. |
| Same topology after fresh data | Refresh button, completed sync and rendered position | `native.ui.radar_island_refresh_*` | Initial and refreshed assertions wait for presented UI, not merely loading=false. |
| Sorting changes index, not geometry | Resource header ASC/DESC, return to Radar | `native.ui.radar_island_sort_*` | None within this scenario. |
| Independent session cameras | Pan, open another context, activate original tab | `native.ui.radar_island_session_*` | Camera is in-memory; durable camera restoration is not established. |
| Filtering cannot rebind keyboard inspection | Home, filter out that identity, Enter, explicit Home/Enter | `native.ui.radar_island_selection_*` | Inspector identity protections retain their separate evidence. |

Initial regressions detected an imperceptible sweep on small island glyphs; the
existing sweep now scales with tile width. A two-context test initially lacked
its second external context and was corrected. Refresh assertions also needed to
wait for the presented projection rather than only the request-completion flag;
both styles passed three repeat executions after that correction. These are not
claims that every initial failure represented a production defect.

The final scoped report is `radar-island-regressions-verified.txt`, under
`~/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked/`.
It includes the existing glyph, highlight/focus, empty-state, large-population,
reopen, offscreen-animation, reduced-motion, theme, quantity-filter and metric
hover cases. It is not a full-suite or coverage-gate run.

Real local Kubernetes evidence uses the owned `podlord-visual-run-6f5ycj` cluster:
`radar-island-real-health.txt` proves yellow Pending PVC and red CrashLoop/failure
presentation through the public UI; `radar-island-real-metrics.txt` proves real
CPU/Memory values, request/limit markers and bounded hover layout. Its 38 dark/
light theme captures are in `radar-island-real-themes/`; the inspected contact
sheet is `radar-island-themes-contact-sheet.png`. These captures precede the final
keyboard-identity guard, whose evidence is the subsequent scoped regression run.

`radar-island-csharp-cpp.png` shows complete actual macOS windows, C# left and
C++ right, uniformly scaled without cropping or altering UI. The native snapshot
contains 1,838 resources (1,758 healthy, 39 warnings, 41 errors); C# shows its
256-row default and 1,416 cached resources. This establishes visible spatial
presentation on the same API, not matching coverage, full-layout equivalence,
all-theme C#/C++ parity or release readiness. Image hashes and capture scope are
in `radar-island-desktop-manifest.json`.

The desktop runner completed successfully and removed its owned container,
volumes, apps and private profiles. A replacement app was accidentally launched
without the private-profile argument; its sole local test source/session were
verified and removed individually, retaining existing preferences. Future
replacement capture processes must remain attached to their owner and have their
explicit `--profile` verified before window binding. No production cluster or
shared image was changed. Native source removal still has no public CLI; this
cleanup used the verified, immutable, task-owned source record only.

The public navigation benchmark records actual resource counts and 50 keyboard-
to-frame samples in `radar-island-navigation-final.txt`. Do not describe its
1,001-resource scenario as a 5,000-resource measurement. The 50 ms p95 target,
5,000-resource CPU/RSS evidence, integrated legacy table/radar layout, durable
camera restoration, all-theme paired comparisons and platform release gates
remain separate acceptance work.

Final island verification on 2026-10-05: all 127 scoped cases passed in 77.80 s.
The subsequent isolated Release/software/offscreen navigation run used 1,001
resources and 50 presented-frame samples: p50 44.284583 ms, p95 58.214583 ms,
maximum 60.077000 ms. The 50 ms p95 target is not met; no performance-gain or
release-ready claim follows from these results. Other render backends, 5,000
resources and CPU/RSS are not established by this run.

## Which radar background water and terrain colors are verified?

The native Radar now retains the reference water pattern and speed controls rather than only the resource island. This increment covers water, terrain colors, alarm color precedence and lifecycle. It does not establish complete application or platform parity.

| Behavior | Public boundary and evidence | Test or scenario | Result |
| --- | --- | --- | --- |
| Animated water is actually rendered; no additional requests | UI frame pixels and external Kubernetes request count | `radar_water_motion` | Basic and Fusion passed |
| Background remains unchanged outside an Event tile when that Event is filtered out | UI frame pixels before and after public filter changes | `radar_water_event_tile` | Follow-up verification recorded below |
| Eleven known/custom resource kinds use the reference terrain palette | Rendered tile-center pixels through external discovery and resource-list responses | `radar_water_color_Node`, `Namespace`, `ConfigMap`, `Secret`, `PersistentVolumeClaim`, `Deployment`, `Pod`, `Service`, `Ingress`, `Event`, `Widget` | Basic and Fusion passed |
| Hidden/minimized Radar, reduced motion, disabled water and zero speed stop playback | Public window/navigation/settings actions; visible water pixel stability and unchanged request count | `radar_water_hidden`, `minimized`, `reduced`, `disabled`, `zero` | Basic and Fusion passed |
| Water settings survive a profile reload; reopening resumes animation | Appearance controls, public reload and rendered frames | `radar_water_settings`, `reopen` | Basic and Fusion passed |
| Keyboard camera interaction defers water animation, then resumes it | Keyboard input and rendered frame pixels | `radar_water_interaction` | Basic and Fusion passed |
| Disabled default alarm rules remain disabled after reload | Public rule editing and reload | `radar_water_rules_disabled` | Basic and Fusion passed |
| Water configuration rejects malformed, fractional and out-of-range values without rewriting the profile | Settings-store public load/save | `native.settings.water_*` | Eleven scenarios passed |
| Existing alarms, navigation, filters, glyphs and settings retain their tested behavior | Existing public UI/API/model boundaries | Scoped Radar/settings/glyph/metric selection | 255/255 passed in 128.15 seconds |
| Pending storage is yellow; failed/CrashLoop Pods are red; filtering does not conceal session alarms | Real owned local Kubernetes API, native UI and cached search | `alert_ui_test real_health` | Passed |
| CPU/Memory hover, request/limit markers and all theme variants render | Real owned local Kubernetes API and inspected native Radar | `alert_ui_test real_metrics` | Passed; 38 theme captures saved |

The initially missing-water regression failed before implementation. Pixel tests also exposed an actual persistence defect: retaining `enabled` as a mutable JSON reference made disabled built-in alarm rules reload as enabled. The decoder now retains a value before canonical comparison; the public reload regression covers the fix.

Lifecycle tests do not mistake unrelated whole-window frames for water animation. Visible paused water must remain pixel-identical over 1.4 seconds, playback must be disabled, and request counts must remain unchanged. Hidden/minimized cases exercise the public lifecycle state and request boundary; they are not a whole-application zero-CPU measurement. Production water uses one owned, stopped timer with no resource-model dependency. Paint performs no resource traversal, network or storage operations.

### What is the measured navigation cost?

Release build, offscreen Qt software rendering, Basic controls, 1,001 resources, 50 measured keyboard-to-`frameSwapped` samples after warm-up. Measurements were sequential, after closing the comparison applications and owned Kubernetes container.

| Configuration | p50 | p95 | Maximum | p95 including screenshot |
| --- | --- | --- | --- | --- |
| Prior island increment, no water | 44.285 ms | 58.215 ms | 60.077 ms | See prior evidence |
| Current water disabled | 39.078 ms | 47.953 ms | 49.226 ms | 48.444 ms |
| Current water enabled | 38.557 ms | 48.515 ms | 49.048 ms | 48.996 ms |

The current measured cases meet the 50 ms p95 navigation target. This is not a 5,000-resource, Metal, mobile, idle-CPU or installed-application memory result. Whole test-process peak RSS was 182,452,224 bytes with water disabled and 180,060,160 bytes with water enabled; these numbers include the test boundary/server and must not be presented as application-only memory use.

### Where are the rendered comparison and logs?

Evidence is retained under `/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked/`:

- `radar-water-csharp-cpp.png`: C# left, C++ right; full windows uniformly resized with no UI alteration.
- `radar-water-desktop-manifest.json`: source-image hashes, dimensions and comparison limitations.
- `radar-water-real-themes-contact.png` and `radar-water-real-themes/`: 38 real-Kubernetes theme captures.
- `radar-water-regressions-complete.txt`: final 255-case scoped run.
- `radar-water-real-health.txt` and `radar-water-real-metrics.txt`: real local Kubernetes checks.
- `radar-water-navigation-off.txt` and `radar-water-navigation-on.txt`: isolated navigation and whole test-process resource measurements.

The desktop screenshots show 1,412 reference resources and 1,836 native resources. Discovery coverage, viewport, camera, activity and animation phase differ; the images demonstrate the actual rendered implementations, not identical-frame or complete feature parity. The desktop runner reached its timeout rather than receiving its completion marker; its exit is not reported as an E2E pass. The independent real-Kubernetes health/metrics checks passed. Owned applications, the exact test container and its volumes were removed; the shared pinned image was retained.

### What remains outside this evidence?

Full product-suite and coverage gates were not rerun for this bounded Radar increment. The last full-suite coverage record is not replaced by these scoped results. No new claim is made about mobile deployment, accessibility certification, complete C#/C++ visual parity, a 5,000-resource performance budget or whole-application idle CPU. Reference terrain and glyph polarity were retained, invented land corridors removed, and tiny glyphs suppressed below the reference nine-pixel threshold. Water preferences reuse the existing settings store; schema version 5 loads versions 1–4, while rollback to an older executable requires its prior profile backup.

The additional `native.alert*` regression selection passed 91/91 cases (partially overlapping the 255-case selection). Its output is `radar-water-alert-regressions.txt`; counts are not added together as unique cases. Final cleanup inspection found no owned comparison process, test container, matching volume or temporary visual-run profile remaining.

## Which corrected scroll and background-only water behavior is verified?

The user clarified the intended controls: mouse-wheel and trackpad scroll zoom around the pointer without modifiers; dragging pans. The native trackpad-pan special case is removed. The background water remains, while Event-local waves, their model binding, model subscriptions and resource traversal are deleted rather than disabled behind a setting.

| Behavior | Public entrypoint | Test | Result |
| --- | --- | --- | --- |
| Trackpad pixel scrolling zooms without panning or requesting data | Wheel event delivered to the rendered window | `radar_island_trackpad_zoom` | Basic and Fusion passed |
| Mouse-wheel scrolling zooms in and out around the pointer | Wheel events delivered to the rendered window | `radar_island_zoom`, `radar_island_zoom_out` | Basic and Fusion passed |
| Horizontal-only scroll neither pans nor changes zoom | Wheel event delivered to the rendered window | `radar_island_scroll_horizontal` | Basic and Fusion passed |
| Drag pans without inspection; clicking the resource still inspects | Mouse press/move/release and click through the window | `radar_island_drag` | Basic and Fusion passed |
| An Event is a colored tile, with no surrounding waves | Isolated rendered Event; public filter replaces it while outer pixels remain unchanged | `radar_water_event_tile` | Basic and Fusion passed |
| Background animation, lifecycle, settings, terrain colors, camera retention and filtered positions remain functional | Existing rendered UI/settings/input boundaries | `native.ui.radar_(water|island)` selection | Final run 68/68 passed in 103.16 seconds |

The native application and UI-test executable were rebuilt. Evidence files are `radar-background-only-build.txt`, `radar-background-only-final-build.txt` and `radar-background-only-tests-final.txt` in the retained desktop-evidence directory above. The initial negative Event-pixel check incorrectly included neighboring resource removal; isolating the Event before comparison removed that test-driver ambiguity. Older water screenshots and measurements above predate this clarification and are historical evidence, not new screenshots or benchmarks of the background-only version. No real-cluster stack or GUI comparison process was started for this follow-up; tests use the existing explicitly external Kubernetes HTTP boundary and clean up their own profiles and servers.

## Which reference-shaped shell and Ports functions are implemented?

Bounded increment, 2026-10-05: Resources/Events/Ports/Settings primary navigation,
cache-derived header cards, one persistent Radar/filter sidebar, a narrow-window
drawer, wrapping Settings sections and actual session-owned forward tasks.
[ADR 0033](../adr/0033-native-reference-shell-and-cache-summary.md) owns the decision;
SHL-001 through SHL-006 in the operational spec own observable requirements.

| Behavior | Public entrypoint | Test/scenario | Evidence |
| --- | --- | --- | --- |
| Sidebar stays mounted across Events/Ports without new API reads | Rendered window navigation | `shell_sidebar` | Basic/Fusion pass |
| Settings/Alerts and Resources are reachable; old Radar route is rejected | Window navigation and Workspace public API | `shell_navigation` | Basic/Fusion pass |
| Narrow windows expose the same Radar through a closable drawer | Sidebar button and Escape at 360 px | `shell_narrow` | Basic/Fusion pass |
| Missing header measurements remain unavailable, not measured zero | Rendered header labels | `shell_metric_missing` | Basic/Fusion pass |
| Header folding distinguishes unknown/zero/partial/stale/overflow and does not double-count Node/Pod or PV/PVC | Public canonical metric summary | `native.metrics.summary_*` | Ten cases pass |
| Ports search/copy/invalid copy/inspect/stop affect only the real owned task | Public window controls after loopback TCP/WebSocket data transfer | `ports_search`, `copy`, `invalid_copy`, `inspect`, `stop` | Ten Basic/Fusion cases pass |
| Settings selectors remain reachable next to an inspector; all shipped palettes/intensities retain behavior | Settings navigation and actual selector controls | `native.appearance.*` | Entire appearance selection passed after navigation repair |
| Narrow field pickers and Radar metric hover follow the real drawer controls | Window input at 680/720 px | `native.field_filter.narrow_*`, `metric_radar_gauges_narrow` | Final responsive regression selection passed |
| Sync settings retain log-limit validation, reload and eviction behavior | Settings/Sync dialog and actual logs UI | `native.logs.invalid_*`, `limit`, `restore`, `eviction` | Final responsive selection passed |

Final responsive selection: 20/20 passed in 21.63 seconds. The intermediate
153-case selection passed 142 cases and exposed eleven remaining navigation/
responsive failures; all eleven were corrected and rerun. These selections are
not added together as unique tests. Full-suite final results are recorded below.

### What did the real Kubernetes desktop comparison establish?

The owned local K3s stack seeded 1,051 actual ConfigMaps, Secrets, workloads,
Services, accounts, storage claims and Pods, plus controller-created resources.
Both applications connected to the same isolated API. The seed harness originally
raced creation of default ServiceAccounts; its direct test Pods now use the
explicitly created test ServiceAccount. The successful replacement stack reached
its ready Pod/deployment gates, then completed its capture/cleanup marker with
exit zero. Exact test container/volumes, both reference processes and private
profiles were removed; the shared pinned image was retained.

Evidence in `/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked/`:

- `shell-csharp-cpp.png`: C# left, C++ right, actual complete Resources windows,
  uniformly resized without editing UI pixels.
- `shell-desktop-manifest.json`: original image hashes, dimensions and limitations.
- `shell-parity-desktop-final.txt`: real Kubernetes readiness and runner evidence.
- `shell-parity-complete-tests.txt`: final full native suite output.
- `shell-parity-last-regressions.txt` and `shell-parity-responsive-final.txt`:
  intermediate and final targeted regressions.

Native capture was during initial loading with 1,274 cached resources; the later
reference frame had 1,412 resources with its default 256-row limit. Camera,
discovery coverage, alarm phase, freshness, table order and typography differ.
The images demonstrate the shared shell direction, not finished visual parity.
Events/Ports/Settings desktop screenshots were not established in this capture;
mislabelled stale screenshots were discarded. Public automated tests cover their
executed journeys but do not replace those missing visual comparisons.

### What functionality and release evidence remain open?

At this shell increment, Problems/Activity shortcuts and named presets were
open; their subsequent scoped implementation is recorded below. Complete inline
Settings forms, reference table order/compact presentation, Ports sorting/column controls and
open-URL semantics, detached-window comparison, complete paired theme/view
screenshots, platform packaging/installation, coverage gates and final CPU/RSS/
5,000-resource interaction evidence remain open. Disabled actions explicitly
report their missing implementation. Settings Search is disabled rather than
focusing a hidden resource search. The roadmap sequences the remaining functions;
this increment does not declare the application release-ready.

### What is the shared-shell Radar navigation measurement?

After stopping the owned Kubernetes stack and comparison apps, the final
Release/software/offscreen/Basic test boundary used 1,001 cached resources and 50
keyboard-to-presented-frame samples. `shell-parity-navigation-benchmark.txt`
records p50 7.385334 ms, p95 12.483167 ms, maximum 12.856083 ms and p95 including
screenshot 12.739875 ms. The visible compact Radar is smaller than in earlier
measurements, so these are not equal-viewport performance comparisons. The
measured case meets the 50 ms p95 budget; Metal, mobile, 5,000-resource input,
whole-app idle CPU and packaged-runtime memory remain unestablished.

Peak RSS of the entire test process was 151,896,064 bytes, including the external
HTTP boundary and test harness. This is not installed-application-only memory.
The initial benchmark selection matched no tests and was replaced by the actual
public-window executable run; its empty result is not performance evidence.

### What is the final full-suite result for this increment?

The rebuilt native suite passed **1,926/1,926** cases in **420.21 seconds**, with
independent profiles/boundaries and eight test jobs. The prior full run passed
1,925 cases; its remaining Radar refresh-layout test captured a baseline before
the newly selected surface presented a frame. Waiting for that public frame
corrected the measurement; five consecutive repetitions passed in 32.61 seconds
before the final complete run. This was a test-driver correction, not weaker
layout assertions or a production timing workaround.

After the full run, disabled Settings controls gained accessibility descriptions
for their missing functionality. The final executable was rebuilt and its shell
selection rerun; that scoped result is retained in
`shell-parity-accessibility-tests.txt`. No further functionality changed. The
full-suite result does not establish code coverage percentages, unchanged C#
behavior, all-theme screenshot parity, installed mobile builds or release
readiness. Those gates remain explicitly open.

Final accessibility-description build succeeded; its eight Basic/Fusion shell
cases passed. Final cleanup inspection found no owned comparison application,
test container, matching volume or visual-run private profile remaining.

## What proves silent loading, Problems/Activity and native saved filters?

The 2026-10-05 increment implements LOAD-001-004 and FLT-014-017 in the
[operational specification](podlord-operational-spec.md).
[ADR 0034](../adr/0034-silent-sync-baseline-and-bounded-reads.md) owns the
collection baseline, concurrency and persistence decisions. Rendering and filter
changes consume the accepted session snapshot; they do not initiate reads.

| Behavior/scenario | Public entrypoint | Executable evidence | Remaining coverage gap |
| --- | --- | --- | --- |
| Initial collections do not produce fresh highlights, sound or automatic focus; partial loading progress is monotonic and completes at one | Imported context, Workspace signals and rendered QML | `native.field_filter.loading_silent_Basic` / `_Fusion` | Every partial-discovery/error/auth combination and physical reduced-motion presentation are not established by these two cases |
| Problems/Activity are exclusive; reset, field conjunction and session switching retain the right membership | Real QML checkboxes, field filter and session activation | `native.field_filter.mode_problems_*`, `mode_activity_*`, `mode_reset_*`, `mode_field_*`, `mode_session_*` | Full legacy membership comparison, every activity status and expiry boundary remain separate acceptance work |
| Presets load, explicitly overwrite, delete and survive restart without changing another session's filters | QML controls and independently reopened Workspace | `native.field_filter.preset_load_*`, `preset_overwrite_*`, `preset_delete_*`, `preset_restart_*` | Native Rename success was exercised on the actual desktop; invalid Rename recovery is not covered by an automated UI scenario |
| Preset catalog has a protected empty default, validates names/fields/modes, handles repeat/conflict/lock and retains unsupported or malformed data | ViewStateStore public load/save methods with private real files | `view_state_test` scenarios `preset_missing`, `preset_default_delete`, `preset_default_replace`, `preset_invalid_mode`, `preset_unknown_field`, `preset_case_collision`, `preset_empty_name`, `preset_large`, `preset_save`, `preset_repeat`, `preset_conflict`, `preset_busy`, `preset_private`, `preset_symlink`, `preset_malformed`, `preset_future`, `preset_oversized` | Legacy-preset import and old-version rollback remain product migration work; the native session-view document now writes version 3 |
| Independent reads overlap in at most four slots; all admissions retain minimum spacing and configured request limits | Real ResourceClient HTTP ingress against an external Kubernetes boundary fake | `native.read_overlap.parallel`, `limit`, `coalesce`, `auth`, `backoff`; `native.field_filter.loading_parallel_*` | Production-cluster throughput is deliberately untested; no claim of a new jitter algorithm |
| A delayed older list or inspector read cannot overwrite a newer accepted read; stale YAML cannot become editable | Public refresh, inspect and document/status entrypoints | `native.read_overlap.late_list`, `late_detail` | Other write/read races retain their separate scenarios and evidence |
| Existing post-load alerts, native themes and shell routes still execute | Real product runtime and rendered QML | Full native suite, including `native.alert_ui.*`, `native.appearance.*`, `native.ui.shell*` | Component totals do not establish paired theme approval, coverage gates or packaged releases |

The mode-problems and loading-parallel regressions failed before implementation.
The first broad run had 118 failures: 116 theme checks exposed an invalid QML
foreground color property; two negative alert scenarios still assumed that the
initial cache population emitted a fresh-resource alarm. The color property was
corrected and those obsolete startup expectations reconciled with LOAD-001.

The rebuilt targeted lane passed **163/163 in 41.68 s**. The final complete
native suite passed **1,975/1,975 in 438.89 s**, with eight independent CTest
jobs. This is executed scenario evidence, not a new line/branch coverage result.
Logs are under
`/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-04-unlocked/`:
`filter-loading-final-focused.txt`, `filter-loading-all-tests.txt` (the earlier
failure) and `filter-loading-all-tests-final.txt` (the final passing suite).
The external HTTP fakes only replace Kubernetes at the network boundary; cache,
scheduler, stores, alert evaluation and QML use the real implementation.

### What was checked against real Kubernetes and the reference desktop?

The disposable local Kubernetes runner verified 1,051 owned resources, including
512 ConfigMaps, 256 Secrets, 64 Deployments, 32 each of StatefulSets, DaemonSets,
CronJobs and PVCs, 64 Services, 16 ServiceAccounts, eight Jobs and three seeded
Pods. Controller-created resources and system objects are additional real data.
The native cached-field UI scenario passed against this real API; its runner
exited zero and cleaned up its owned container, volumes and profiles.

A separate desktop run used independently built C# and C++ applications, separate
private profiles and the same disposable cluster. Problems and Activity were
operated visibly; a native Activity preset was saved and then renamed to
`Recent activity`. The reference saved the corresponding named Activity preset.
Actual frames were placed side by side at their original pixel sizes, C# left
and C++ right. No UI pixels were edited or resized.

Evidence is retained under
`/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-05-filter-loading/`:

- `problems-csharp-cpp.png`, `activity-csharp-cpp.png`, `preset-csharp-cpp.png`.
- `native-loading-final.png` and the earlier `native-loading-start.png`: actual
  loading frames; automated progress assertions are the timing authority.
- `native-filter-presets.json`: the real private native catalog after Rename,
  without kubeconfig credentials.
- `comparison-manifest.json`: source hashes, image dimensions, observed counts,
  boundaries and limitations.
- `real-kubernetes-final.txt` and `desktop-final.txt`: successful real dependency
  and desktop runner outcomes, including scoped cleanup.

The reference Problems frame has 69/1,417 resources; native has 81/1,839.
The reference Activity frame is limited to 256 rows; native shows its current
1,839 recent resources without a row Limit. Snapshot time, discovery/alias
coverage, table order, typography, metrics and camera differ. These are genuine
comparison findings, not proof of identical membership or finished visual parity.
The health column grows from the bottom during collection work and has critical
red above warning yellow above healthy green. During periodic loading the static
resource map remains usable while alert effects are suppressed.

The first desktop review exceeded its own timer and cleaned up. One mislabeled
stale Problems image was discarded; the final rerun completed normally. The
native macOS accessibility tree lost its child elements during that rerun, so
visible-coordinate operations and inspected rendered frames were used. This
assistive-technology observation remains an acceptance gap despite passing QML
control tests. Both final runners removed their exact owned processes,
containers, volumes and private profiles; the existing shared pinned image was
retained. No shared or production cluster was used.

### Which requirements are still not complete?

Age, UID, row Limit, legacy preset import, the missing-restart compatibility
choice, exact Problems membership, full paired theme/view screenshots, reference
column order/typography and complete inline Settings remain open. Invalid Rename
recovery needs a public UI regression. The compact metric header now uses the
same typed quantity data with short units and keeps full values in its tooltip;
this is not equivalent metric aggregation across applications. Coverage gates,
final performance/RSS/CPU results and target packaging/installation remain release
gates. This increment does not declare visual parity or release readiness.

## What Proves Filtered Terrain And The Inline Settings Increment?

| Behavior | Public entrypoint | Test / evidence | Remaining gap |
| --- | --- | --- | --- |
| Excluded radar resources remain grey, stable and noninteractive | Real QML filter, framebuffer, click | `native.ui.radar_island_filter_basic/fusion` | Paired desktop capture pending |
| Native Settings sections use real existing capabilities | Real Settings navigation and controls | `native.ui.settings_inline_*_Basic/Fusion` | Localization, restoration toggle, source rename/removal, About actions |
| Sync retention saves and restores, rejects zero MB | Real inline form, new workspace | `settings_inline_save`, `settings_inline_invalid` | None for these paths |
| Diagnostics snapshot is explicit and credential-free | QML Refresh, public diagnostics snapshot | `native.ui.diagnostics_snapshot`, `native.ui.diagnostics_redaction` | Request audit failure, cancellation, bounds and sorting/copy coverage |
| Reference order/widths, saved layout retained | `TableLayoutStore.load/save` | `native.table_layout.reference_defaults/reference_saved` | Port-forward action column |
| Problems covers StatefulSet readiness, Job outcome, Pod lifecycle | HTTP boundary through real Workspace/cache/filter | `native.ui.problems_reference_*` | Time-aligned shared-kind comparison against real C# output |

This increment does not establish whole-application visual or release parity. Test execution and paired desktop evidence are recorded after their actual completion.

### What Does The 2026-10-05 Visual-Parity Run Actually Prove?

- Native build succeeded. The final complete CTest run passed 2010/2010 tests with `-j8` in 446.37 seconds. The focused regression selection passed 50/50 in 23.40 seconds. This increment adds 35 public-boundary cases; obsolete column-position and unpresented-frame assumptions in existing tests were corrected.
- Full-suite output: `/Users/yuna/.local/share/podlord-comparison/native-builds/session-core/visual-parity-all-tests-final.txt`. Focused output: `visual-parity-focused-final.txt` in the same build directory. Earlier failed exploratory output remains separate and is not the final result.
- Real desktop evidence: `/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-05-native-visual-parity`. Both apps used the same disposable local Kubernetes API with 1051 explicitly created resources: 512 ConfigMaps, 256 Secrets, 64 Deployments, 32 each StatefulSets/DaemonSets/CronJobs/PVCs, 64 Services, 16 ServiceAccounts, 8 Jobs, and 3 Pods. Kubernetes subsequently created additional system and runtime objects.
- Eleven original-pixel paired images cover Resources, Problems, and all nine Settings sections. Each `compare-*.png` places C# on the left and C++ on the right without resizing or changing either screenshot's content. Images were inspected. These are manual desktop observations, not automated assertions of pixel parity. They cover Sirocco Command dark, not every theme or platform.
- The native Problems capture visibly preserves grey cached terrain behind matching tiles. Native Problems plus Kind=`"Pod"` produced two matching error Pods. The reference Kind dropdown could not be operated reliably during this run, so this narrowed case has no completed paired desktop assertion.
- Aggregate counts must not be treated as parity evidence: native discovery includes more kinds than the reference, and both apps captured different snapshot times while Kubernetes generated warning Events. The Problems images show 76/1834 native and 74/1412 reference. Exact identity-set comparison for the same immutable snapshot remains open.
- The lifecycle runner completed successfully and removed its owned apps, container, volumes, kubeconfig, and private profiles. The pre-existing pinned k3s image was retained. No production or shared cluster was used.
- No line/branch coverage percentages, new CPU/RSS measurements, or complete release-readiness conclusion are established by this run.

| Remaining comparison gap | Evidence / next acceptance boundary |
| --- | --- |
| Settings tab treatment and compact controls | Appearance and Sync pairs still show different tab chrome, row spacing, and input widths; compare keyboard focus and narrow layouts after further presentation changes. |
| Language selection | Appearance reference has a language selector; native has no equivalent localized-label workflow yet. |
| Source rename/removal | Sources reference has editable/removable snapshots; native import and metadata display do not establish those capabilities. |
| Diagnostics presentation | Native shows a bounded snapshot list, not reference-like sortable/copyable audit and memory tables. Failure/cancellation/retention-bound public cases remain necessary. |
| Optional workspace restoration | Reference exposes a preference; native currently documents enabled restoration rather than a user-controlled toggle. |
| About actions and branding | Support/project/update actions and branding remain visually and functionally different. |
| Alert editor layout | The same three built-in rules are present, but list/editor arrangement and initial selection differ substantially. |
| Radar presentation | Filter dimming is proven; viewport framing, water tint, and sidebar chrome still visibly differ. |
| Table presentation | Resource defaults and semantic colors are covered; port-forward action-column equivalence and complete reference row/layout comparison are not closed. |
| Problems aggregate equivalence | Compare resource identity sets under the same recorded API responses; changing live totals alone do not prove or disprove the classifier. |

## What Proves Deterministic C# And C++ Radar Geometry?

The reference exporter is `tests/Podlord.RadarParity`. It imports an owned local
kubeconfig through AppState, warms the real KubernetesResourceService, and opens
and refreshes the actual MainWindowViewModel through public entrypoints. Its
oracle is the public RadarBlocks collection, not a copied geometry formula.
Only resource identities, topology metadata and expected terrain are exported;
credentials, response bodies and Secret values are absent.

Native consumes that exact immutable projection through ResourceTable.publish,
ResourceFilter and RadarIsland. The durable, compact
`native/tests/data/radar-reference.json` is a recorded real-cache projection,
not invented demo data. It contains 1412 resources, including twelve Namespaces,
1400 reference resource blocks and fourteen decorations.

| Behavior | Public entrypoint | Test | Remaining gap |
| --- | --- | --- | --- |
| Every reference resource has identical world coordinates and terrain | Public cache models and radar tile roles | `radar-reference-reference` | Other snapshots, multiple clusters and unlisted reference kinds |
| Enumeration and descending table order preserve geometry | ResourceTable.publish and ResourceFilter.sort | `radar-reference-reordered` | None for the recorded projection |
| Exact Pod filtering and reset preserve the full topology | ResourceFilter.filter and visible tiles | `radar-reference-filtered` | Existing grey-terrain framebuffer tests supplement this case |
| Real Namespaces remain selectable without displacing reference resources | RadarIsland.selectResource/currentIndex | `radar-reference-namespaces` | Inspector transport is outside this immutable renderer comparison |
| Pan, zoom and reset retain world geometry | RadarIsland.setViewPose/pan/resetView | `radar-reference-pose` | Physical trackpad and keyboard interaction remain separate UI evidence |
| Rendered resource and decoration centers match reference colors | Actual ResourceRadar.qml window framebuffer | `radar-reference-render-check`, `radar-reference-render-fusion` | Borders, glyphs, animation and every-theme pixel equivalence |

The complete `radar-parity` mode of `scripts/test-native-visual-kubernetes.sh`
passed against a newly created local k3s stack. Its final snapshot contained
1410 real resources: 1398 reference resource blocks, twelve additional selectable
native Namespaces and fourteen reference decorations. Five geometry/selection
scenarios and the actual renderer capture passed. The original native archive,
linked with its matching header, returned an explicit coordinate mismatch;
crashes and setup failures are not accepted as regression evidence. The initial
exploratory runner failed, was corrected, and is not the successful final run.

Evidence directory:
`/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-05-radar-determinism`.
`real-run-final.log`, `before.log`, `reference.json`, `reference-tests-final.log`,
`csharp-radar.png`, `cpp-radar.png` and `paired-radar.png` retain the actual results.
The focused durable selection passed 7/7 in 3.36 seconds.

The pair contains actual isolated C# RadarBlockLayer and native ResourceRadar
renderers, not installed whole-window screenshots. Both have a 1200 by 1200
viewport, pan zero, zoom one, stable alerts and a common background. Water is
disabled to isolate terrain; the native empty-profile label is suppressed only
in this renderer harness. The native capture crops the original window to the
exact radar viewport; the pair does not scale either frame. Images were inspected.

The successful runner removed its owned cluster, volumes, temporary kubeconfig
and private profile. It retained the pre-existing pinned image. No production or
shared cluster was accessed. This closes the recorded-input geometry comparison,
not whole-application visual parity, water/alert parity, performance/RSS evidence,
coverage gates or release readiness.

### Which Existing Radar Assertions Changed, And Did The Whole Suite Pass?

- `radar_island_layout_basic/fusion` now verifies nonoverlapping rendered cells rather than requiring neighbors to have different Y coordinates. Horizontal neighbors are valid reference terrain.
- `radar_water_event_tile_basic/fusion` excludes the actual Event and Widget cell rectangles when checking unchanged surrounding water. Both cells legitimately change between active and grey terrain during the filter switch; their arbitrary separation is not a water contract.
- `radar_island_session_basic/fusion` verifies the saved camera in the actual radar after session restoration, then uses Reset to verify the original rendered geometry. A panned resource may be outside the viewport and correctly have no foreground delegate.
- These six public-window cases each passed five consecutive runs: 30/30 executions in 134.40 seconds. No production geometry was distorted to satisfy the obsolete assertions.
- The first whole-suite run passed 2014/2017 and exposed those three assertion failures. Its `all-tests-final.log` is historical despite its filename; `ui-failures-replay.log` retains the targeted failing replay.
- The final complete run passed 2017/2017 in 446.91 seconds, including the seven new reference cases. The authoritative final output is `all-tests-after-regressions.log`; `ui-regressions-final.log` records the repeated cases. C# reference and native application builds succeeded.
- `manifest.json` records snapshot/frame and original comparison-build hashes. Temporary original archives, headers, comparison executables and composition scripts were removed after their hashes and explicit mismatch evidence were retained. Normal native and reference build outputs remain available for subsequent comparisons.

## What Proves The Native Settings And Alert Layout Increment?

The source-of-truth comparison is the actual reference Alerts DataGrid, action
bar and selected-rule editor in MainWindow.axaml, supplemented by the retained
`2026-10-05-native-visual-parity/compare-settings-alerts.png` capture. Native
replaces its separate rule-button pane with that table/toolbar/editor arrangement.
The catalog, evaluation, asynchronous private store and sound player are unchanged.

| Behavior | Public entrypoint | Test | Remaining gap |
| --- | --- | --- | --- |
| Table above toolbar/editor, first rule selected | Actual Main.qml window and pointer controls | `native.alert_ui.reference_layout_Basic/Fusion` | Exact populated old/new framebuffer comparison |
| Narrow viewport remains scrollable | Actual 720 by 720 window | `reference_layout_narrow_Basic/Fusion` | Physical touch/trackpad and smaller mobile viewports |
| Light theme applies to the same controls | Workspace appearance action and actual frame | `reference_layout_light_Basic/Fusion` | Every-theme contrast and appearance review |
| Built-in fields locked, activation accessible | Actual editor and action controls | `reference_locked_Basic/Fusion` | Reference default description wording |
| Keyboard selection | Pointer focus, Up/Down/Home/End key events | `reference_keyboard_Basic/Fusion` | Assistive-technology inspection |
| Built-in sound preview available unless muted | Existing preference action and visible controls | `reference_sound_preview_Basic/Fusion` | This case checks reachability, not audible playback |
| Activation persists and can be reversed | Actual table toggle and reopened Workspace | `reference_toggle_Basic/Fusion` | Real populated alarm effects remain in existing engine/UI tests |
| Failed activation retains catalog/selection | Actual table toggle against owned locked store | `reference_toggle_failure_Basic/Fusion` | None for the locked-store failure |
| ASC/DESC/NONE preserves identity/catalog | Actual header and row clicks | `reference_sort_Basic/Fusion` | Active-count ties and every sortable column need additional cases |
| Explicit local copy, oversized rejection | Right-click menu, real Qt clipboard | `reference_copy_Basic/Fusion` | Long hover preview has renderer implementation, no dedicated new assertion |
| Group-specific AND and OR/group deletion | Actual criterion buttons, typed values, Save | `reference_groups_Basic/Fusion` | Matcher field/example-label presentation differs from reference |
| Criterion removal persists remaining expression | Actual remove button and Save | `reference_remove_criterion_Basic/Fusion` | None for this scenario |
| Last group/criterion cannot be removed | Actual enabled states | `reference_group_guard_Basic/Fusion` | None for this scenario |
| Failed save keeps draft; unlocked retry saves it | Actual Save and owned file lock | `reference_failed_draft_Basic/Fusion` | Duplicate/delete failure selection should receive dedicated cases |

All 28 added Basic/Fusion cases passed five consecutive runs, 140/140 executions,
in 16.28 seconds. The initially intermittent group interaction exposed a test
click on a partially clipped, still-scrolling control: the public wheel/input
helper now reveals the complete control and waits for scrolling to stop.
The existing repeat runner also recorded 20 successful group runs and 20
successful keyboard runs during diagnosis; only `reference-final.log` is the
final repeated selection. Earlier focused/repeated logs include failures and
are not final gates. No pre-change executable layout replay is claimed.

Evidence root:
`/Users/yuna/.local/share/podlord-comparison/desktop-evidence/2026-10-05-alert-layout`.
`native-alerts-layout.png`, `native-alerts-layout_narrow.png` and
`native-alerts-layout_light.png` are actual native whole-window framebuffers,
visually inspected. Their profile is empty and isolated: these are layout/theme
captures, not Kubernetes data-parity or full release evidence. Their reference
comparison capture has a populated profile and must not be used for numerical
or pixel equality. Native default descriptions, exact matcher/action labels,
column resize/pin/visibility, zoom preview, sound-source action and broader
Settings capability gaps remain open. Derived table updates are suppressed
while the Alerts view is hidden; there are no new UI refresh timers.

### Which Final Gates Passed For This Increment?

- The complete native suite passed **2045/2045** in **595.47 seconds**. `all-tests.log` and `all-tests-status` record that gate. Production code did not change afterwards; only the real-Kubernetes test's selection/focus assumptions and failure diagnostics were corrected.
- The public cache benchmark with 5000 Pods and 30 rendered tab changes measured first frame 5689 us, p50 7226 us, p95 8203 us and maximum 8525 us. This is an offscreen/software-frame benchmark under this test host, not an installed-device CPU/RSS or every-view performance claim.
- `kubernetes-e2e-verified.log` is the successful local **native-e2e** run. It created 1051 explicitly seeded real resources, including 512 ConfigMaps and 256 Secrets, plus real workload/controller output and owned port-forward/deletion targets. The actual UI port-forward Pod/Service cases, delete/race cases, inspector history, cache search, whole-session alarm/filter case, measured Metrics/Radar case, health highlights and cached field-filter case all passed.
- The initial and second Kubernetes runs failed at the Metrics/Radar test and remain historical evidence. The diagnostic frame showed a correctly culled Pod outside the viewport. The test now filters the exact Pod kind/namespace/name and uses the real Radar focus button plus Home key before expecting its delegate. Health targets use the same explicit input workflow; theme loops reacquire live delegates instead of retaining a potentially replaced pointer. No production viewport-culling behavior was weakened.
- The runner removed all three owned stacks, volumes, temporary kubeconfigs, copied binaries and private profiles on both failure and success. Exact-label checks confirmed containers `podlord-visual-run-ezduvv`, `podlord-visual-run-k7c11g` and `podlord-visual-run-do2saq` were absent. The pre-existing pinned k3s image was retained; unrelated Docker resources were untouched.
- Successful real-Kubernetes logs, executable hashes, Metrics/Radar theme captures and health frames are under `kubernetes-verified/`. These supplement the three empty-profile Settings framebuffers; they do not establish exact populated C#/native Settings appearance or release readiness.

## What Does The 2026-10-05 Native Release Audit Prove?

The [capability inventory](legacy-capability-inventory.md#native-release-assessment-on-2026-10-05)
now gives a native disposition and remaining gap for every LEG-001 through
LEG-037 function. Missing menus are blockers, not completed lower-level tests.
[ADR 0035](../adr/0035-native-preflight-gates-before-release.md) owns the native
CI/package/startup gate. The added workflows have no hosted execution evidence
until actually run; they do not publish native release assets.

| Behavior | Public entrypoint / executable check | Outcome and remaining evidence |
| --- | --- | --- |
| Full native review baseline | `scripts/test-native.sh`; all 2045 CTest cases, eight jobs | First fresh review run: 2044 pass, search-restore Basic fails; 455.15 s. Coverage was not produced because the old runner aborted immediately. Both styles failed in repeated reproduction. |
| Corrected full native gate | `scripts/test-native.sh`; all 2045 CTest cases, eight jobs | 2045/2045 pass in 447.47 s. Coverage 6811/7037 lines (96.79%) passes 95%; 6242/7752 branch outcomes (80.52%) fails 90%. Runner exits 1 without weakening the gate. |
| Cache-only search versus cold startup sync | `native.ui.filter_search_restore_Basic/Fusion` | Regression checks zero new calls before restart, then waits for restored filter and three loaded/one visible resources. Restart has its own client/cache and is allowed to synchronize. Twenty executions per style pass: 40/40 in 124.59 s; corrected full suite also passes. |
| Report coverage without hiding failed tests | `scripts/test-native.sh` | Runner retains the test failure exit, reports executed source coverage before raw-profile cleanup, and includes read-overlap/radar-reference binaries. Thresholds remain 95% line / 90% branch; failed coverage cannot pass the gate. |
| Production macOS renderer | `application_startup_test PACKAGE_EXECUTABLE SCENARIO` with `PODLORD_TEST_QPA_PLATFORM=cocoa` | Seven packaged Cocoa cases pass: normal/ambient-config/ambient-exec startup, profile-file/symlink rejection, explicit override and empty-profile rejection. Seven offscreen component startup cases also pass. The deliberately omitted offscreen package plugin is not a product dependency requirement. |
| Native package bytes, static paths and signature | `scripts/check-native-macos-package.sh` | arm64/macOS 27.0: ZIP 36,524,314 bytes; installed regular files 98,726,138 bytes; dependency checker and local ad-hoc signature pass. No clean-device/notarization/license pass follows. |
| Current C# public suite | `scripts/test.sh --no-restore` | 458 passed, zero failed/skipped: Core 91, Layout 69, App 214, Kubernetes 84. Coverage 6657/7011 lines (94.95%) fails 95%; 2715/3236 branches (83.90%) passes only its currently configured 80% gate. |
| Native hosted CI and release dependency | `actionlint .github/workflows/ci.yml .github/workflows/release.yml .github/workflows/native.yml`; reusable native preflight | Checks both Mac architectures using maintained local test/package entrypoints; release test depends on native preflight. Syntax validation is not hosted runner/device execution. Windows/Linux/mobile and actual native release assets remain open. |

Workflow validation and POSIX shell syntax pass. No Developer ID distribution
identity was found; only a local development identity is available. No public
release/tag/push or signing-credential changes were performed. Owned legacy test
containers are absent after runner cleanup. The preserved reference was untouched.

Coverage gaps are not waived. Prioritize public failure/merge/lifecycle scenarios
in `resource_apply.cpp` (54.17% branches), `workspace_field_filters.cpp` (59.89%),
`resource_delete.cpp` (61.11%), `port_forward_transport.cpp` (63.89%),
`resource_port_forward.cpp` (66.35%) and `workspace_apply.cpp` (66.47%). No
uncovered path is declared unreachable without a separate source/behavior audit.

Retained local review evidence:
`/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-05-review-package`,
`release-evidence/legacy-release-review.log`,
`release-evidence/filter-restore-repeated.log`, and
`native-builds/native-release-review.log` under the same comparison root.
Post-correction logs use `native-release-review-corrected.log`,
`filter-restore-corrected.log`, `native-package-startup.log` and
`native-workflow-lint.log` under `release-evidence`. These are evidence outputs,
not claims of a pass before their entrypoint succeeds.

The legacy runner used private profile/kubeconfig directories and owned-cluster
cleanup. No production/shared cluster or preserved baseline was changed.

### Wie werden die ergänzten Source- und Session-Aktionen geprüft?

2026-10-05: `source_input_test` verwendet echte private Stores und den tatsächlichen Qt-Workspace. Die benannten `native.management.*`-Fälle laufen mit Basic und Fusion; externe Kubeconfigs und Home-Verzeichnis sind pro Prozess temporär. Keine Produktions- oder gemeinsam genutzten Cluster.

| Verhalten | Öffentlicher Einstieg | Benannte Fälle | Verbleibende Grenze |
| --- | --- | --- | --- |
| Paste speichern, abbrechen, Escape, leere Eingabe | Settings → Sources → Paste-Dialog | `source_manage_paste`, `paste_empty`, `paste_cancel`, `paste_escape` | Desktop-Bildvergleich folgt separat |
| Ungültiges YAML/Origin, Store gesperrt, Entwurf erhalten | Paste-Dialog | `source_manage_paste_invalid`, `paste_origin_invalid`, `paste_busy` | Kein Credential-Plugin wird ausgeführt |
| Deduplikation und geänderte Inhalte | Paste-Dialog | `source_manage_paste_repeat`, `paste_changed` | Keine Umleitung bestehender Sessions |
| Größenbegrenzung | Öffentliche Store-Import-API | `source_manage_paste_limit`, `file_limit` | Grenze ergänzt UI-Tests, kein riesiger Tastatur-Test |
| Expliziter Home-Import | Settings → Sources → Home-Import | `source_manage_home` | Isoliertes Home, keine Benutzerdateien |
| Datei-Refresh, Wiederholung, Änderung, fehlender Origin | Settings → Sources → Refresh | `source_manage_refresh`, `refresh_repeat`, `refresh_changed`, `refresh_missing` | Tatsächliche Dateiänderung/-entfernung |
| Snapshot, normalisierte Namespaces, anderer Kontext, keine Duplikate | Settings → Workspace → Session-Manager | `session_manage_namespaces`, `unchanged`, `context`, `repeat` | Sicherheitsstufe/Farbe/Icon noch offen |
| Abbruch, Escape, schmale Oberfläche | Session-Manager | `session_manage_cancel`, `escape`, `narrow` | Noch kein vollständiger Theme-/Gerätematrix-Beleg |
| Ungültige Namespace-Eingabe, Store gesperrt, Entwurf erhalten | Session-Manager | `session_manage_invalid_namespace`, `busy` | Originalsession bleibt unverändert |
| Unabhängige benannte/unbenannte Kopie | Session-Manager | `session_manage_duplicate`, `duplicate_unnamed` | Keine Nutzungshistorie, keine ungespeicherten Felder |
| Fehlende Session/fehlender Kontext | Öffentliche Workspace-Aktion | `session_manage_missing`, `invalid_context` | Abwehrprüfung für veraltete Aufrufer |
| Wiederherstellung geschlossener Snapshots | Öffentlicher Workspace-Neustart | `session_manage_restart` | Aktiver YAML-/Forward-Erhalt braucht zusätzliche Regression |

Die Tabelle ist eine Abdeckungszuordnung, kein pauschaler Release-Pass. Ausführungsergebnisse und weiter offene Release-Gates werden separat berichtet.

### Welche cachebasierten Diagnosepfade sind zugeordnet?

`native.guidance.*` läuft mit Basic und Fusion. Die direkte öffentliche Diagnosefunktion ergänzt vier Tests über die tatsächliche UI und den echten Scheduler/Detail-Cache gegen einen ausschließlich externen Kubernetes-HTTP-Testserver.

| Verhalten | Öffentlicher Einstieg | Fälle | Grenze |
| --- | --- | --- | --- |
| Pull-/Erstellungsfehler, Backoff, aktuelles OOM | `resourceGuidance(document)` | `image_backoff`, `image_pull`, `image_invalid`, `crash`, `config_error`, `create_error`, `oom` | Keine automatische Root-Cause-Behauptung |
| Historisches OOM, inzwischen laufender Container | Dieselbe Funktion | `previous_oom`, `previous_running` | Gelbe historische, nicht aktuelle Fehlermeldung |
| Init-/Ephemeral-Container, mehrfacher/duplizierter Status, Ausgabelimit | Dieselbe Funktion | `initial_container`, `ephemeral_container`, `multi`, `duplicate`, `bounded` | Bis zu acht Hinweise |
| Eviction/Scheduling, Node-Readiness/-Pressure, Claim- und Deployment-Zustände | Dieselbe Funktion | `evicted`, `unschedulable`, `scheduled`, `node_*`, `pvc_*`, `deployment_*` | Nur konkrete passende API-/Status-Evidenz |
| Unbekannt, Custom Kind, fehlend, fehlerhaft, falsche Typen, Secret | Dieselbe Funktion | `unknown`, `custom_kind`, `missing`, `malformed`, `wrong_status_type`, `unnamed_container`, `secret`, `empty` | Keine Gesundheitsbehauptung, keine Secret-/Message-Kopie |
| Öffnen, Hover, Einklappen ohne Requests | Inspector → Overview → Diagnose | `ui_expand` | Kein externer Browseraufruf im Test |
| Geänderte aktuelle Daten | Inspector → Refresh detail | `ui_refresh` | Tatsächlicher GET durch Scheduler |
| API-Lesefehler bei bestehendem Detail-Cache | Derselbe UI-Einstieg | `ui_failure` | Lokales HTTP 503, sichtbarer Lesefehler, alte Hinweise bleiben klar Cache |
| Aktive Session und YAML-Entwurf beim Snapshot erhalten | Editor und öffentliche Session-Aktion | `ui_session_snapshot` | Keine zusätzliche Clusterabfrage und kein Write |

Der früher offene aktive YAML-Erhalt des Session-Pakets ist damit als ausführbarer Pfad ergänzt; der konkrete Testlauf entscheidet über den Nachweis. Aktive Port-Forward-Erhaltung, echte Desktop-Bilder und Release-Gates bleiben gesondert offen.

## Native Capability Increments, 2026-10-06

| Behavior/scenario | Public entrypoint | Executable evidence | Remaining gap |
| --- | --- | --- | --- |
| Cached duration syntax, range/exact semantics, unavailable/future/invalid timestamps, invalid input/recovery and UID text/search/sort | ResourceTable/ResourceFilter public model API | `native.age_uid.*` (45 cases); included in 341/341 scoped regression, 57.07 s | Whole-device rendering and real API filtering proof are not inferred from model checks. |
| Actual Age/UID picker, keyboard expressions, preset load/save and session restart | Real Qt window and field-filter dialog with external Kubernetes HTTP boundary | `native.field_filter.age_*`, `native.field_filter.uid_*`, Basic/Fusion (24 cases); included in 341/341 scoped regression | Legacy row-limit controls and legacy preset-schema import remain open. |
| Retain prior column order/width/visibility/pins and append hidden UID; atomic save and malformed-record retention | TableLayoutStore public load/save | `native.age_uid.layout_read`, `layout_save`, `layout_invalid`; existing layout suite included in 341/341 | Native layout schema 3 is not a C# persisted-store migration. |
| HTTP/HTTPS browser handoff, repeat, invalid URL-shaped token, closed task and another session | Qt Ports window and Workspace public actions; real local TCP/WebSocket forward; external browser handler only | `native.forward.ports_open_*`, Basic/Fusion (12 cases); complete forward regression 108/108, 43.80 s | Actual browser/server TLS compatibility and table/window-transfer parity remain open. |
| Project/support and shipped sound-source links, local license display/copy/close/narrow viewport and idle silence | Real Qt About window; external browser handoff only | `native.about.*`, Basic/Fusion; verification in progress | Complete reference content and Qt/third-party notice/source distribution remain open. |

The preceding complete native gate run passed 2195/2195 tests (909.24 s), with 96.87% line coverage and 80.74% branch coverage. The 90% branch gate still fails. These numbers precede the Age/UID, browser and About increments and must not be reported as their final full-suite evidence. The local Kubernetes API/Secret/log/exec-credential-plugin E2E run passed and cleaned its owned container/volumes. It did not test an embedded container terminal. Desktop comparison is currently blocked by the locked Mac; offscreen Qt checks do not replace C#/C++ Cocoa comparisons.

### Release verification follow-through, 2026-10-06

| Behavior | Public entrypoint | Executable checks | Evidence and remaining gap |
| --- | --- | --- | --- |
| Current native behavior regression | CTest and actual Qt application boundaries | 2,304 registered native cases | 2,304/2,304 passed in 474.88 seconds; five subsequent CLI metadata cases also pass. This first full run did not record an isolated complete coverage profile; a separate instrumented full run is required. |
| About on narrow windows | Real Qt Quick window resized to 680 x 480 | `native.about.license_narrow_Basic`, `native.about.license_narrow_Fusion`, About/settings/port regression group | 72/72 scoped checks pass. The License action remains reachable in the compact header and its actual viewport stays inside the resized scene. Screenshots are offscreen/software evidence, not C#/C++ Cocoa parity proof. |
| CLI metadata without ambient authentication or profile mutation | Actual app process with isolated home and ambient exec kubeconfig | `native.application.startup_help`, `startup_version`, `startup_help_invalid_profile`, `startup_version_invalid_profile`, `startup_unknown_option` | All twelve native startup cases pass. Five metadata cases also pass against the packaged executable using Cocoa. The version is checked against the build version and CI against the bundle metadata. |
| Dependency closure stays within the packaged application | `scripts/check-native-macos-dependencies.sh` against real application copies | `scripts/test-native-macos-dependencies.cjs`: executable/loader/rpath traversal, external file/directory/runtime-plugin symlink, internal alias, missing/corrupt runtime and path validation | The external-library and runtime-plugin cases failed before their shared boundary guards. Test copies preserve relative symlinks verbatim; otherwise the copy operation itself changes the artifact dependency boundary. Final complete package-boundary verification is pending. |
| Coverage gates cannot silently accept lower branch coverage | `scripts/check-coverage.py` command line | Six cases in `scripts/test-coverage-gates.py`: exact gates, lower lines, lower branches, former 80% threshold, full coverage, absent reports | 6/6 pass after restoring the specified 95% line / 90% branch gates. Existing legacy coverage exclusions remain visible debt, not evidence of complete UI coverage. |
| Real Kubernetes and cleanup | `scripts/test-native-kubernetes.sh` | Real TLS/client certificate, detail/YAML/Secret, unchanged drafts, confirmed credential exec, multi-container logs, JSON Patch/reconciliation/Secret remasking | Repeated successfully for the current implementation. Owned container `podlord-native-run-uezy4x` was removed by the cleanup owner; private run data is removed and the shared pinned image is retained. Credential exec is not an embedded container terminal. |

The current macOS arm64 package preflight measured 36,554,820 ZIP bytes and
98,794,676 regular-file installed bytes. Ad-hoc signature verification and the
original dependency preflight pass; the strengthened boundary checks are an
additional gate. Developer ID/notarization, complete dependency notices/source
obligations, clean-device and other-platform proof, visual reference comparisons,
missing capabilities, and the 90% branch gate still prevent release readiness.
The desktop remains locked; native app inspection cannot be replaced by the
passing CLI or software-renderer checks.

Package-boundary completion: all 16 checks pass against the current real package
and its isolated mutated copies in 21.62 seconds. This includes external
runtime-plugin links and preserved internal aliases. These checks are now part
of native preflight CI; CI execution itself has not been observed locally.
Coverage aggregation includes the new guidance, Age/UID, and About test programs
and passes profile filenames through LLVM's supported input-file manifest;
coverage-run cleanup removes that owned manifest as well as the raw profiles.

### What did the complete isolated native coverage run establish?

The 2,309-case run completed with 2,309 passes in 473.19 seconds using a private LLVM profile directory. Production coverage was 96.85% lines and 80.86% branches: the 95% line gate passed, the 90% branch gate failed. These results predate the mutation-safety changes below and must not be represented as their full-suite verification. The native gate remains unchanged. The owned raw-profile directory and input manifest were removed after merging the retained evidence profile.

Public CLI benchmarks from that run measured process-startup-plus-JSON-output p95 at 13.565 ms for 16 sessions and 18.781 ms for 16 source snapshots, each with 50 samples. These are not GUI frame, scrolling, Metal rendering, or mobile measurements.

Evidence: `native-2309-full-coverage-tests.log`, `native-2309-coverage.txt`, `native-2309-session-benchmark.json`, and `native-2309-source-benchmark.json` in the local comparison release-evidence directory.

### Which mutation safety regressions are covered through the native UI?

| Scenario | Public entrypoint | Test | Result and remaining gaps |
|---|---|---|---|
| A valid-looking 404 body arrives with an incomplete declared transfer | Delete confirmation and subsequent inspection | `native.delete.lost_notfound_truncated_Basic` and Fusion counterpart | Failed before the shared response-framing fix; cached resource now remains present and uncertain, with no second DELETE. |
| An acknowledged write is followed by a replacement object at the same API path | YAML preview, confirmation, fresh detail publication | `native.yaml_apply.ui_ack_recreated_*`, `native.yaml_apply.ui_ack_recreated_equal_*` | Four cases failed before the identity guard. The original draft is now retained even if replacement values match the requested change. |
| Write receipts and read-back objects have invalid identity, shape, status, or framing | Real workspace, inspector and mutation queue against a local external HTTP boundary | `native.delete.lost_*`, `native.yaml_apply.ui_read_*`, `native.yaml_apply.ui_receipt_*` | 130 deletion cases passed; the combined mutation subset passed 222/222 in 85.88 seconds. Auth remains manual, writes are not retried, and malformed evidence cannot clear a draft. |
| A write receipt has a wrong resource name or namespace | YAML preview and confirmation | `native.yaml_apply.ui_receipt_name_*`, `native.yaml_apply.ui_receipt_namespace_*` | Four further Basic/Fusion cases passed without production changes: existing change-observation validation already rejects these acknowledgements. |

Complete chunked and automatically decompressed gzip 404 responses remain valid absence evidence; truncated chunked and declared-length responses do not. Test servers replace only the external Kubernetes boundary; the workspace, queue, request client and QML surfaces are real. The full native suite, real Kubernetes rerun and fresh packaged artifact after these fixes remain separate acceptance gates.

### Do the coverage and package checkers reject misleading evidence?

The coverage CLI has 14 passing public-process scenarios, including exact 95%/90% thresholds, below-gate results, missing reports, empty/excluded-only reports, malformed XML, missing line numbers, negative hit counts and invalid branch totals. Invalid reports fail explicitly rather than producing a traceback or a false 100% result.

The macOS package-boundary checker has 16 passing scenarios against private copies of the actual deployed bundle. Escaping executable/loader/rpath dependencies and external plugin/file/directory symlinks are rejected; legitimate relative aliases inside the bundle remain supported. Private copied bundles are removed after each scenario. This does not prove notarization, corresponding-source compliance, installation on a clean device, or visual parity.

### Did the mutation-safe native build pass the real Kubernetes boundary?

The repeated local K3s run passed real TLS/client-certificate discovery, Inspector YAML/values and masked Secrets, non-mutating local draft checks, explicitly confirmed credential execution, all-container log selection, confirmed JSON Patch, concurrent-change reconciliation, entered Secret write/read-back/remasking, and preservation of unrelated original Secret bytes. The runner's cleanup verified removal of its owned cluster container and anonymous volumes; the shared pinned K3s image was retained. No production or shared cluster was used.

Evidence: `native-mutation-safe-kubernetes-e2e.log`. These are real API and Qt Quick flows using offscreen/software rendering, not C#/C++ desktop visual parity or an embedded container terminal.

### What did the release gate reruns prove after the mutation-framing fixes?

All 2,407 native tests passed in 527.90 seconds. Isolated native production coverage was 96.91% lines and 81.30% branches; the branch gate still failed. Public process benchmark p95 was 14.174 ms for sessions and 19.098 ms for source snapshots. These measurements are not GUI frame times. This full run predates the subsequent API-version admission guard and narrow-dialog fixes.

The preserved C# reference also passed all 458 tests (91 Core, 214 App, 69 Layout, 84 Kubernetes). Its coverage remains 94.95% lines and 83.90% branches, below both configured gates. Four reports and public test results were retained in the isolated test results directory; the runner performed its scoped cluster cleanup. Passing these suites does not close the native migration inventory.

Evidence: `native-2407-final-gates.log` and `csharp-release-full-gates.log`.

### Are mutation admission and cancellation checked outside the happy-path UI?

Thirty additional public `ResourceClient` scenarios use actual discovery, listing, fresh detail GETs, the YAML preparer, the native scheduler and mutation outcome signals against a minimal external HTTP boundary. They cover target UID/name/kind/namespace/API-version mismatches, empty tokens, short/reversed/incorrect guards, size limits, missing/closed/inactive/disabled/auth-suspended sessions, queued/running duplicates, cancellation ownership, and explicit rejected/uncertain read-back without another write.

The wrong API-version admission case failed before the central cached-target guard. The combined mutation subset then passed 249/249 in 98.28 seconds. Queued cancellation prevents a send; dispatched cancellation does not interrupt the original write and read-back. Exact HTTP write counts and final external resource values are asserted. These lower public-boundary checks supplement, rather than replace, the QML and real Kubernetes checks.

Evidence: `native-mutation-admission-before.log` and `native-mutation-admission-final-regression.log`. The new executable is included in the canonical coverage object list.

### Does a successful local Kubernetes run leave profiling files behind?

The repeated runner passed with profiling directed into its private run directory. Existing exit/signal cleanup removes those raw files together with the private kubeconfig and owned cluster, while named screenshots and logs remain as evidence. The six raw files from the immediately preceding owned run were removed individually; no shared directory or Docker image prune was performed.

Evidence: `native-mutation-safe-kubernetes-cleanup-e2e.log`.

### What did the completed mutation and narrow-session passes prove?

- The complete native run passed 2,437/2,437 tests in 595.12 seconds. Line coverage was 97.02%; branch coverage was 81.71%, below the unchanged 90% release gate. Evidence: `native-2437-final-gates.log` in the local release-evidence directory.
- The combined source/session management subset passed 60/60 component UI scenarios: 32 source cases and 28 session cases across Basic/Fusion. The narrow session capture now measures the actual 390x720 scene before capturing it, and the Close action uses a fixed standard dialog footer. The tests actually click Close. A dormant close-error dialog is instantiated only when needed. Evidence: `native-session-close-lazy-regression.log`; these offscreen component images do not establish mobile-platform or C#/C++ visual parity.
- Real isolated K3s deletion passed in both Basic and Fusion: ordinary deletion and replacement-UID conflict followed by explicit reselection/deletion. The existing real TLS, manual credential-provider, multi-container logs, YAML apply and masked Secret read-back scenarios also passed. Evidence: `native-mutation-delete-kubernetes-e2e.log`. The owned `podlord-native-run-39njmq` container was removed; the shared pinned K3s image was retained.

### Does the native sound catalog match the reference and play real embedded audio?

- `native.sound_catalog.reference` reads the actual C# catalog and compares all 117 entries, including IDs, names, purposes, authors, licenses, sources, music flags and adapted embedded URLs. An asset check verifies unique IDs and the embedded Ogg headers. Each sound has an independent public save/restore scenario.
- `native.audio.*` plays each of the 116 real embedded assets once and twice through Cocoa/Darwin, using zero output volume. A successful case must reach EndOfMedia with a decoded audio track and no player error; metadata or merely accepting a play request is insufficient. These checks prove backend playback completion, not human-observed audibility or another platform's decoder.
- The full-catalog/asset/persistence/audio and existing alarm subset passed 470/470 before the additional search scenarios. The new reference test failed against the three-sound implementation before the catalog was expanded. Evidence: `native-sound-catalog-before.log`, `native-full-sound-regression.log`.
- Search exercises real text entry, dropdown selection and save. Search may match name, purpose, author, license, source or music metadata. Empty results and clearing the query must preserve the chosen sound; searching or scrolling must make zero Kubernetes requests. A Basic dropdown failure exposed zero-timestamp simulated wheel events; supplying monotonic timestamps resolved the failure without adding application scrolling machinery. Evidence: `native-sound-wheel-timestamp-regression.log`; the complete suite is rerun after that correction.
- The fresh `2026-10-06-complete-sound-package` passed dependency-path/ad-hoc signing and size checks, with ZIP 37,214,254 bytes and installed regular files 99,911,620 bytes. Sixteen physical dependency-boundary tests and five packaged Cocoa metadata/startup cases passed. A temporary copy of that actual bundle ran the audio harness with its own Cocoa and Darwin multimedia plugins and completed repeated embedded playback. The temporary copy was removed. Evidence: `native-complete-sound-package-boundaries.log`, `native-complete-sound-package-startup.log`, `native-bundled-audio-runtime-final.log`.
- The package remains a local preflight artifact: Developer ID/notarization, complete redistribution/source obligations, clean-device installation, other platform builds, visual reference comparisons and the coverage gates remain separate release blockers. Only 88,380 bytes remain under the existing 100,000,000-byte installed-size ceiling.

### What did the final sound-source and grouped-credits run prove?

The initial 2,806-test run failed one Basic About source-link case: expanding the catalog had pushed that action outside the reachable viewport. The correction groups attribution by the seven actual packs and uses real scrolling before test clicks. The source-action/About subset passed 102/102, including browser handoff failure and explicit retry without automatic retry, rule persistence or Kubernetes traffic.

The final complete run passed 2,834/2,834 in 570.99 seconds. Line coverage is 97.05%; branch coverage is 81.73%, so the canonical runner still fails the unchanged 90% branch gate. Evidence: `native-sound-source-regression.log`, `native-sound-credits-final-gates.log`.

The final `2026-10-06-sound-catalog-final-package` passed 16 physical dependency-boundary tests and five isolated packaged Cocoa startup/metadata cases. ZIP size is 37,214,833 bytes; installed regular files remain 99,911,620 bytes. Evidence: `native-final-sound-package-boundaries.log`. These are local package preflight results, not notarization, clean-device or visual-parity approval.

### Can external credential references stall or allocate without a bound?

Thirty independent public `KubeconfigStore::importFile` / `connection` scenarios use real temporary files and isolated child processes. All four reference types cover missing, directory, empty, oversized, FIFO and broken-symlink inputs. Token files additionally cover relative/absolute paths, valid symlinks, the exact 16 MiB boundary, invalid UTF-8 and whitespace-only input. Only Unix platforms run FIFO/symlink cases. The parent bounds and kills a blocked child; all temporary files and profiling data are cleaned up.

Before the fix, eight cases failed: the four FIFO references blocked and the four oversized inputs bypassed the limit. The shared resolver now checks regular-file type, rejects oversized files and bounds the actual read, reusing the existing configuration budget. Symlinks to regular files remain supported. The combined credential, source and authentication subset passed 146/146 in 6.22 seconds. Evidence: `native-credential-files-before.log`, `native-credential-files-final-regression.log`. The new executable is included in canonical coverage; this subset does not replace the complete instrumented run or establish protection against hostile local filesystem replacement races.

### Does native cached provider authentication preserve the reference capability?

Fifteen independent scenarios use the real source store, resolver, ResourceClient, cache and scheduler against a minimal local Kubernetes HTTP boundary. Cases cover access/id token selection, explicit token/file precedence, empty/null access fallback, missing/empty tokens, malformed config/token types, invalid token characters, mixed exec/provider rejection, explicit impersonation rejection, 401 suspension and snapshot-preserving credential rotation. Successful cases assert the Authorization header on actual discovery/list requests and public cached rows. Source metadata must omit tokens. Resolution and import must make zero requests and authorize no executable.

Twelve cases failed against blanket provider rejection. After the shared resolver correction, the combined provider, credential-file, source, authentication and read-overlap subset passed 168/168 in 17.76 seconds. A 401 test observes two seconds without additional requests and checks that ordinary refresh is rejected. Existing exec authentication tests continue to pass. Evidence: `native-provider-token-before.log`, `native-provider-token-final-regression.log`. This is cached token support, not provider refresh-token exchange, physical browser authentication or a claim of every identity-provider variant.

### What is the complete credential/provider release evidence?

The complete native suite passed 2,879/2,879 after both resolver changes. Instrumented coverage is 97.14% lines and 81.88% branches; the unchanged branch gate still fails. Evidence: `native-credential-provider-final-gates.log`. The coverage gate's fourteen CLI self-tests also pass; they do not raise application coverage.

The fresh `2026-10-06-credential-provider-package` passes dependency-path/ad-hoc-signature and size checks: ZIP 37,215,610 bytes; installed regular files 99,911,796 bytes. Its sixteen physical dependency-boundary checks and five packaged Cocoa startup cases pass. A temporary copy completed repeated real embedded playback with that copy's own Cocoa/Darwin plugins; it was removed. Evidence: `native-credential-provider-package.log`, `native-credential-provider-package-boundaries.log`, `native-credential-provider-package-startup.log`, `native-credential-provider-bundled-audio-final.log`.

The real local K3s runner passed TLS/client-certificate, explicit credential-provider, logs, YAML/Secret read-back and Basic/Fusion deletion/replacement-UID scenarios. Its owned `podlord-native-run-hlyzwk` container and private profiling/configuration files were cleaned up, retaining the shared pinned image and named evidence only. Evidence: `native-credential-provider-kubernetes-e2e.log`. These results do not establish desktop reference screenshots, native Metal/idle-resource budgets, other-platform execution, Developer ID/notarization or complete distribution obligations.

### Do theme updates preserve unrelated unsaved settings input?

Six real Qt Quick scenarios (Basic/Fusion, synchronization draft, YAML-limit draft, rejected theme) failed before the fix. They enter actual controls through keyboard input, execute the public preference update against the real private store and assert both the visible draft and the unchanged unrelated saved value. Successful theme changes must also appear in the actual Appearance controls. No internal signal or preference store is faked.

Appearance notifications now update only appearance controls; they do not initialize unrelated settings inputs. The combined settings and source/session management subset passed 148/148 in 7.06 seconds. The theme-update scenarios are the six new draft checks, not an additional run of the complete theme catalog. Evidence: `native-settings-draft-before.log`, `native-settings-draft-final-regression.log`. This does not introduce persistent settings drafts or change the explicit section/open initialization behavior. The new test executable is part of canonical coverage.

### Are migrated palette commands actual UI actions rather than another backend?

Thirty-six independent Basic/Fusion Qt Quick scenarios cover the visible button, Ctrl/Meta+K, initial focus, arrows, default Enter, navigation to four destinations, case/whitespace normalization, unknown queries, disabled actions, Escape/Close, repeated opening and the actual 390x720 scene. Enabled Problems and Port-forward cases populate the real source/session stores and Workspace cache through an external local HTTP boundary. Searching/opening must not add HTTP requests; Problems toggles twice without losing the cached resource, and forward preparation opens the actual confirmation without starting a forward.

The initial 26 cases failed against the absent palette. Subsequent regressions exposed ambiguous Sources/Resources substring matching and required the test to await actual popup/layout completion and traverse visible scene items rather than logical QObject ownership. The external positive list response includes real Kubernetes list metadata; malformed test responses were corrected, not accepted by production. Source/session, settings-draft and palette cases then passed 102/102 in 6.68 seconds. Evidence: `native-command-palette-before.log`, `native-command-palette-final-regression.log`. Scoped profiles and owned temporary files are removed; optional screenshots include their style to avoid concurrent output collisions.

The palette uses the same Qt actions as the existing navigation buttons. This establishes the six migrated commands, not the still-missing Import K3D action, an embedded terminal, physical accessibility or full C#/C++ visual parity. Its executable is included in canonical coverage.

## Which HUD And Portable macOS Package Checks Passed On 2026-10-06?

| Behavior | Public boundary | Test or evidence | Remaining gap |
| --- | --- | --- | --- |
| CPU, memory, storage, pod and node cards stay visible at 320, 390 and 1440 pixels; clicking CPU opens the actual dashboard | Real Qt Quick application, mouse and rendered scene | `native.hud.layout.*`, Basic/Fusion; four narrow cases failed before replacing the horizontal strip with Qt Flow, then all six passed; the combined command/settings/source subset passed 70/70 in 4.45 seconds | Empty-profile offscreen layout is not populated C#/C++ desktop parity or physical mobile evidence |
| Complete replacement test run with official Qt SDK | Public CLI/API/UI test entrypoints | 2,927/2,927 passed in 625.41 seconds; 97.14% line and 81.91% branch coverage | 90% branch gate still fails; no gate exclusion or threshold change |
| Declared minimum OS agrees with actual runtime files, including plugins; runtime architectures match the app | Real copied macOS package and physical Mach-O metadata/dependency mutations | Six old metadata cases failed before adding the gate; all 24 dependency/platform cases now pass | The macOS-13 load-command boundary is not clean-device OS-13 execution; local Intel execution is unavailable without Rosetta |
| Official Qt/yaml toolchain installs and preserves unrelated paths | Real POSIX installer, trusted vendor archives, checksum verification; only an unavailable external package registry is simulated | Real successful installation; nine CLI input/reuse/registry-failure cases pass, including cleanup of the failed private installation | Hosted arm64/x86_64 CI execution is still required |
| Candidate package is self-contained and within unchanged budgets | Actual Release, non-instrumented app bundle | `2026-10-06-portable-arm64-package`: 28,520,490 ZIP bytes, 73,520,027 installed bytes; ad-hoc signature and dependency closure pass; twelve Cocoa startup/CLI cases pass | Developer ID, notarization, license/source/relink distribution, clean-device installation and other platforms remain open |
| Embedded OGG plays repeatedly through bundled plugins | A real audio driver inside a private copy of the actual app bundle, Cocoa | `native-portable-bundled-audio.log` and `native-portable-default-bundled-audio.log`: two successful decoded plays with Darwin and with the default FFmpeg backend | Decoder completion is not a human listening check or proof on other operating systems |

Evidence is retained under the private comparison release-evidence directory.
The native CI workflow now uses the pinned SDK installer instead of inheriting
Homebrew's host-OS runtime floor. Workflow syntax passed actionlint; it has not
been presented as a successful hosted run. The desktop remained locked during
this increment, so no new physical C#/C++ comparison screenshot is claimed.

## Which TLS And Source-Import Boundaries Passed On 2026-10-06?

| Behavior/scenario | Public entrypoint and runnable check | Evidence | Remaining gap |
| --- | --- | --- | --- |
| Trusted mutual TLS and rejection of untrusted server/client certificates | `native.tls.trusted`, `native.tls.untrusted_ca`, `native.tls.untrusted_client`; `scripts/test-native-tls.cjs` with actual package argument | Three standalone cases and three cases in a private copy of the actual portable OpenSSL package passed. | Instrumented workspace driver, not a main-window or clean-device proof. |
| Portable runtime dependency, architecture, OS floor and mandatory TLS libraries/plugin | `PODLORD_NATIVE_PACKAGE=<actual app> node --test scripts/test-native-macos-dependencies.cjs` | 27/27 actual-package mutation checks passed, including missing SSL/crypto/backend. | Older OS, Intel execution, signing and actual target-device support remain unverified. |
| Actual packaged startup and CLI metadata | `application_startup_test` against packaged executable with `PODLORD_TEST_QPA_PLATFORM=cocoa` | 12/12 passed, including ambient-input isolation and invalid-profile help/version paths. | No populated C#/C++ screen comparison. |
| Real Kubernetes TLS, fresh YAML/values, masked Secrets, local validation, confirmed exec client certificates, multi-container logs, JSON Patch/conflict reconciliation, Secret write/readback and UID-safe deletion | `scripts/test-native-kubernetes.sh` using the official SDK build | Complete portable OpenSSL run passed; Basic/Fusion deletion and replacement-confirmation checks passed; owned cluster `podlord-native-run-k9tkhf` removed. | Broader platform and installed-main-window matrices remain open. |
| Pinned SDK installation and refusal/cleanup behavior | `scripts/install-native-macos-toolchain.sh <new absolute directory>`; `scripts/test-native-macos-toolchain.cjs` | Complete Qt/yaml-cpp/OpenSSL installation from an empty destination passed; reproduced SDK removed, real manifest retained. 9/9 failure/input cases passed. | Hosted runner execution and legal/source-delivery approval are separate. |
| Readable/editable source controls at 320/390/1440 px | `native.ui.source_layout_*`, `source_input_test` | Failed-first 5/6; corrected 6/6 in Basic/Fusion. Real path editing and subsequent owned-source import are exercised. | Offscreen narrow frames are not mobile-device evidence. |
| K3D import, all four reference JSON name forms and legacy text output; repeat/change/partial exports; host-only normalization; no implicit session/API/auth; limits, timeout, invalid executable/profile/UTF-8/YAML/name and busy admission; generated refresh exclusion | `native.k3d.*`, `k3d-import-test`; only the external CLI is simulated | Missing public action reproduced first; all 41 K3D cases plus 36 palette and 6 source-layout cases passed: 83/83 in 36.25 s. Palette catalog updated from six to all seven reference commands. | This fixture boundary does not itself prove Docker/K3D or installed-app behavior. |
| Real generated config, trusted Kubernetes connection, incremental cached data and completion of discovery | `scripts/test-native-k3d-import.sh` using local Unix Docker/Colima only | Passed against a newly created K3D 5.9.0 / K3s 1.35.5 cluster. The final actual Fusion frame contains 325 resources. Only its named/tagged cluster, dependent tools container, network and volumes were removed; default kubeconfig is isolated through test-only `KUBECONFIG`. | Actual desktop/C# pairing, other devices and all populated themes remain open. |

The pre-K3D complete run passed 2,936/2,936 in 1,284.44 s: 97.37% lines and 82.06% branches. Its unchanged 95%/90% gate correctly exited nonzero. A new complete run owns the generated-source changes; do not borrow the earlier coverage result for those changes.

The portable OpenSSL candidate measured 31,315,390 ZIP bytes and 80,724,632 installed regular-file bytes before the K3D addition. Its executable SHA-256 is `bf40904764e2bfdcbd4ec43dc208886db538ff69a901406265ae546ca2281567`; ZIP SHA-256 is `2af92b4c72e54b9d8306be9106c5ca5beb52b60850b8f0e6c505b077f3fcec9c`. The Qt-only candidate is not the TLS-capable final artifact. Later packages need their own measurements.

Evidence is retained under `/Users/yuna/.local/share/podlord-comparison/release-evidence`: `native-portable-openssl-kubernetes-final.log`, `native-packaged-mutual-tls.log`, `native-portable-openssl-dependencies-final.log`, `native-portable-openssl-startup-final.log`, `native-toolchain-reproduction.log`, `native-toolchain-reproduction-inputs.txt`, `native-release-full-final.log`, `native-k3d-final.log`, `native-real-k3d-import-final.log` and `k3d-generated-source-resources.png`. Earlier failed K3D fixture setup used an unsupported label flag, a zero exported port and an over-short whole-discovery test bound; those failures are not passes or application timeout changes. The refused-cleanup cluster `podlord-import-k3d-import-wmjmlk` was explicitly removed after ownership inspection. The final test isolates default kubeconfig handling and recognizes only the exact K3D helper belonging to a tagged test cluster.

## Which Restoration And Desktop Comparison Checks Passed On 2026-10-06?

| Behavior/scenario | Public entrypoint / runnable check | Actual evidence | Remaining gap |
| --- | --- | --- | --- |
| Optional automatic tab restoration, retained catalog identity/history/configuration, explicit reopen, reenabling, ordinary reload, invalid/missing settings, locks/conflicts, repeated saves | `workspace-restore-ui-test`; `native.workspace_restore.*` | 28 Basic/Fusion scenarios passed in the official Qt SDK run. The combined settings, alert-reference and restoration subset passed 198/198 in 15.06 s. | Saved filters/sorts retention, installed-app restart pairing and concurrent-window placement ownership need distinct evidence. |
| Settings navigation at 390 px, checkbox mouse/keyboard interaction | Real QML window, pointer and key events in the restoration driver | First narrow check failed because Settings lay outside the scene at x=402. Existing header controls now wrap with native Flow; both narrow scenarios pass. | Broader header/pulse/sidebar layout regression and target-device evidence remain required. |
| Populated main surfaces, nine Settings sections, five Inspector tabs and all nineteen themes in dark/light | Installed C# baseline and native portable package, actual local K3D/K3s desktop interaction | 55 original-resolution C#-left/C++-right pairs retained under the private `desktop-evidence/2026-10-06-generated-sources/comparisons` directory. The stack seeded 1,051 real Kubernetes objects; multi-container Logs displayed real alpha/beta streams. | Different discovery catalogs and changing API snapshots are not identical-topology proof. These screenshots predate the restoration/header changes and do not establish every modal, intensity, keyboard/accessibility state or device. Actual scroll inspection exposed a blank alarm-editor region still under investigation. |
| Owned visual-stack lifecycle | `scripts/test-native-visual-kubernetes.sh desktop`, explicit completion marker | Desktop run completed successfully; both owned apps, private profiles, named cluster `podlord-visual-run-pkknll`, its containers and associated ephemeral resources were cleaned by the script. Screenshots were retained. | No unrelated resources were authorized for deletion. |
| Generated-source portable package runtime and limits | `scripts/check-native-macos-package.sh`, actual-package dependency/startup/TLS checks | Before restoration/header changes: 31,319,886 ZIP bytes; 80,742,216 installed bytes; 27 dependency/platform mutation checks, 12 Cocoa startup cases and 3 packaged TLS cases passed. Executable SHA-256 `3f47dc7083451f56d4e6fbfbbf5b50be55a8e2667301515bc2a2d4d2d7f81737`; ZIP SHA-256 `01d5b32f957a2d0e0eee9fa1a9e256e5b15d5b1f108060344af4d3e07bac09ee`. | Ad-hoc signature only. A later artifact requires its own measurements. Developer ID/notarization, runtime licensing/source delivery, actual minimum-OS/Intel and other-platform device checks remain open. |

The complete pre-restoration run passed 2,977/2,977 in 1,162.83 seconds with
97.43% line coverage and 82.26% branch coverage. The unchanged 90% branch gate
therefore still fails. This result is not a complete check of later changes.
The failed intermediate restoration run loaded an accidental duplicate QML
property; that error was corrected before the official 198-case passing run.
No intermediate failed run is presented as passing evidence.

## Which Bottom-Docked Inspector And Startup Checks Passed On 2026-10-06?

| Behavior / public boundary | Executed check | Result / limit |
| --- | --- | --- |
| Inspector opens below the workspace, leaves Radar/filter right, closes without losing filter/cache | `native.ui.dock_*`, real QML window with external Kubernetes-boundary server | Initial eight cases failed against the side-by-side layout. Ten Basic/Fusion mouse, keyboard, narrow, wide and page-switch cases pass in the corrected 174-case subset. No detached-window or geometry-persistence claim. |
| Inspector safety and compact HUD/layout | 174 inspector/shell/reference-alert/layout cases | 174/174 pass in 70.43 seconds after correcting the readonly Flow sizing assignment. Actual open/closed window frames were captured separately; they are not paired C# parity evidence. |
| Alarm editor remains operable through long/narrow forms | Four `reference_scroll*` Basic/Fusion public wheel cases | 4/4 pass. The previously observed transient blank scroll area is not reproduced by these cases; no production blank-area fix is claimed. |
| Explicit startup source import | 13 `native.application.startup_source_*` / help/version-with-source process cases | 13/13 failed before the option existed. The corrected startup subset passes 25/25 in 6.83 seconds. Includes file, file URL, folder, partial folder, missing/invalid/empty/repeated options, exec credentials, unchanged restart and changed snapshot. |
| Source isolation and immutable storage | Real application subprocess plus independent `podlord-source list` CLI; local server only at the external Kubernetes boundary | Original source bytes preserved, only the requested source retained, no automatic session/authentication/API activity. Arbitrary repeated source paths and installed-package checks are not covered. |
| Bottom-docked portable macOS arm64 bundle | `check-native-macos-package.sh`, official portable SDK, Release/testing off | ZIP 31,321,558 bytes; installed logical regular-file sum 80,742,424 bytes. Dependency preflight passes. Executable SHA256 `e5a367a3861ff3ee06e3e0ba8008cc9108a2010dd0bc69604593cd036d68fdc6`; ZIP `889f06f5f89ad411a6b4f6faeb79c210c6c47b99453541776dd669eb038de897`. This package predates the startup-import addition, is ad-hoc signed, and does not establish licensing, notarization, clean-device runtime or whole-product parity. |

Evidence is retained under the comparison workspace's `release-evidence`:
`native-inspector-shell-corrected.log`, `native-bottom-docked-inspector.png`,
`native-bottom-docked-inspector.png.closed.png`,
`native-startup-import-first-fail.log`, `native-startup-import-corrected.log`,
`native-bottom-docked-package.log` and `2026-10-06-bottom-docked-package`.
The complete bottom-docked run passed 3,014/3,017 checks in 606.02 seconds.
Three narrow-layout checks clicked a Drawer target while it was still outside
the window during its opening transition. The existing shared pointer-input
helper now waits for the real target to enter the window; the affected checks
and port-forward subset passed 39/39. This is a test-input correction, not a
claimed production layout fix. The subsequent subtraction of redundant local
Drawer-opening code still requires a focused rerun. Coverage measured 97.45%
lines and 82.31% branches: the unchanged 95%/90% gate still fails.

The startup-import package passed dependency preflight: ZIP 31,322,650 bytes,
installed regular-file sum 80,742,424 bytes; executable SHA256
`ef441e294bf1c6d54ece6c41fd8a344f27cfd2a3adfbcd4cccc0deb93459c714`, ZIP
`1cd84e11136241b9c4b0d796cb9d6edd82c029c9d4eb9c8086b6906689795c15`.
All 25 startup scenarios passed against that actual bundle using Cocoa on the
complete repeat (`native-installed-startup-cocoa-repeat.log`). The first run
failed unchanged re-import once; the cause is not established. Neither startup
run proves populated UI parity, real cluster access or release signing.

Four owned visual Kubernetes runs reached the real cluster but failed at the
first native API discovery with a TLS failure. Each removed its own container
and ephemeral profile. A direct public TLS-runtime probe found OpenSSL with the
configured SDK library path; shell-launched drivers instead found only Secure
Transport. The visual runner now initializes the same configured OpenSSL path
inside the running shell as the other real Kubernetes runners. A corrected real
run is required; certificate checks are not disabled and no failed run is counted
as a pass. Evidence includes `native-sdk-tls-runtime.json`,
`native-sdk-tls-loader.log` and `native-release-tls-kubernetes.log`.

## Which Table Keyboard And Runtime Checks Passed On 2026-10-06?

| Behavior / public boundary | Executed check | Result / remaining gap |
| --- | --- | --- |
| Menu and Shift+F10 open actual cell context actions; copy preserves the cache-only boundary; inspection targets the selected identity | `native.field_filter.table_keyboard_*`, real QML keyboard and pointer input; only the external Kubernetes HTTP boundary is simulated | Eight Resource cases failed first. The shared cell delegate now opens its existing menu from the keyboard. All 24 Basic/Fusion Resource, Event and pinned-column scenarios pass; Event inspection resolves the referenced Pod. Physical keyboard/platform evidence remains separate. |
| Keyboard change preserves field filters, modes, presets, selection and restart behavior | Complete field-filter subset | 140/140 pass in 85.10 seconds, including Event/pinned additions. A first corrected run failed one premature Fusion key event; the driver waits for a completed frame and actual cell focus. |
| Full startup-import and narrow-layout build | `scripts/test-native.sh`, canonical public CLI/API/UI checks | 3,030/3,030 pass in 605.41 seconds. Coverage is 97.23% lines and 82.31% branches; unchanged 95%/90% gate exits nonzero. This run predates the keyboard-menu addition. |
| Trusted mutual TLS and denial of an untrusted server or client | Public workspace driver in a private copy of the actual startup-import app bundle, Cocoa | 3/3 pass with the bundled OpenSSL backend. This is real TLS at an external local HTTPS boundary, not main-window Kubernetes or target-device proof. |
| Broad real Kubernetes workflow | Corrected visual runner; 1,051 seeded local objects | Pod/Service data forwarding, both delete cases, Inspector history, cached search, Alerts, metrics and yellow/red Radar checks pass. The final cached field scenario fails Ready 2/2. Colima reports 99% disk use and Kubernetes eviction pressure; the cause is still subject to an isolated-runtime rerun, not declared a product fix. Its owned cluster is removed. |

Evidence: `native-table-keyboard-before.log`,
`native-table-keyboard-regression-final.log`,
`native-table-keyboard-all-surfaces.log`, `native-current-full-suite.log`,
`native-current-package-mutual-tls.log`,
`native-corrected-runtime-kubernetes.log`.
The separate temporary Colima runtime does not change the user's active context,
SSH configuration, shared disk size, existing containers or volumes. Its named
profile, image and data are removed by the owning supervisor on completion or
failure. The existing default runtime's unused volumes are not authorized cleanup.

The isolated runtime rerun passed the complete real Kubernetes lane, including
Ready 2/2, eight observed CPU and memory measurements, nine observed restart
counts and 128 filtered Radar resources. It contained 1,843 cached resources.
Twenty public keyboard-input-to-completed-frame samples measured 10.25 ms p50,
13.69 ms p95 and 14.96 ms maximum with the software renderer. These are not the
required Metal/three-session/5,000-resource/5-MiB-log profile. Cluster
`podlord-visual-run-dijdkz` and temporary Colima profile
`podlord-release-owned-colima-sax9kb`, including its image/data, were removed.
Evidence: `native-isolated-colima-kubernetes.log`,
`native-isolated-colima-cleanup.log`, `native-isolated-colima-image-digests.json`
and the `2026-10-06-isolated-colima-kubernetes` directory.

Eight sequential keyboard-menu cases also passed with the actual Cocoa platform
in the Qt driver (`native-table-keyboard-cocoa.log`). The current keyboard-menu
package passes dependency/size preflight: 31,322,772 ZIP bytes and 80,742,424
installed regular-file bytes; executable SHA256
`27e5ed18ffe89d34a62a041a004eb02354d6bd86b3f61e03315396d1236902ee`, ZIP
`8bbcaf7ad43bd4ee8e52c43b02534fec0ebea4578d0d9f148ced11e50b038bd0`.
These do not close signing or all-surface installed C#/C++ comparison.

`test-native-visual-kubernetes.sh release-review` runs the complete native lane
before starting both actual applications on the same owned cluster. Both app
paths must be supplied/built before cluster creation. Native startup explicitly
imports the owned kubeconfig, and the desktop child clears host Qt/dynamic-loader
overrides so SDK libraries cannot stand in for its bundle. No session is opened
implicitly. The explicit completion marker or bounded timeout closes both apps,
removes the owned cluster/profile and preserves recorded evidence. This combined
workflow subsequently passed its complete native Kubernetes lane and exited
normally after desktop inspection. Its owned cluster `podlord-visual-run-npezfl`
and Colima profile `podlord-release-owned-colima-review-6ratc1`, including data and
image, were removed. The desktop phase did not prove navigation/parity: the
native accessibility tree became root-only after session opening, and attempted
Close, Settings and command-palette actions did not visibly change the captured
frame. The public UI-thread sample showed event-loop activity, not a demonstrated
deadlock. `native-review-desktop-outcome.txt`, the process sample and the original
`desktop-evidence/2026-10-06-keyboard-context` captures preserve this gap. Its
side-by-side image has C# left and native right with different Inspector/camera
states; it must not be cited as an equal-state comparison. Developer signing,
assistive technology, clean-device and all-platform evidence remain open.

The later complete keyboard-menu suite passes 3,054/3,054 in 719.52 seconds
(`native-keyboard-context-full-suite.log`). Coverage is 97.32% lines and 82.40%
branches. The unchanged branch gate still fails; this run predates the value-copy
increment.

### Which Inspector Copy Representations Were Exercised?

`native.field_filter.value_copy_*` drives the actual QML Values page and clipboard
against a fake only at the external Kubernetes HTTP boundary. It covers ConfigMap
binaryData and Secret data key/raw/decoded/preferred copying, empty values,
non-text binary denial, unchanged screen masking and API counts, invalid formats,
missing keys, repeated representation changes, closed Inspector scope and
keyboard menu selection. Twenty-five of the initial 28 checks failed before the
new menu existed; some also exposed premature delegate/input checks, so this is
not a claim that all initial failures were product defects.

After implementation, 28/28 focused scenarios passed. The expanded complete
field-filter suite passes 180/180 in 111.72 seconds and the existing Secret,
custom-resource and Secret YAML regression subset passes 17/17 in 15.96 seconds.
Nine sequential Cocoa/Fusion copy scenarios also pass. Initial Cocoa attempts
exposed a driver error: pointer input was sent to the main window instead of the
control's actual popup window. The driver now uses the control's public window
and observes clipboard/error completion; keyboard selection uses actual menu
highlight/focus rather than a platform-specific initial index.

Evidence: `native-value-copy-before.log`, `native-value-copy-input.log`,
`native-value-copy-field-regression.log`, `native-value-copy-secret-regression.log`,
`native-value-copy-cocoa-popup.log`. These driver results do not replace installed
main-window or every-theme comparison.

The value-copy portable package passes dependency/size/ad-hoc signature preflight
(`native-value-copy-package.log`): ZIP 31,323,904 bytes, installed regular files
80,742,440 bytes; executable SHA256
`9a2c7829d7e823f0310eb47b042104de35698aad5ceefa67a978d74fd8f5596b`, ZIP
`175463f585f31759cf5a02d557ac061c2a9f7e3abc1f091e5c1d09a2092b7a62`.
This package contains the value-copy implementation, not later alarm-preview work.

### Which evidence covers Inspector copying, alarm zoom preview and source aliases?

| Behavior | Public entrypoint / scenario | Evidence | Remaining gap |
| --- | --- | --- | --- |
| Inspector preferred/key/raw/decoded copy, empty text, binary rejection, masking, invalid format/key, closed scope and keyboard | Actual Workspace/QML Values controls; `value_copy_*`, ConfigMap and Secret | `native-value-copy-40-final.log`: 40/40 pass, 26.49 s. External Kubernetes HTTP is the only fake; clipboard, cache and UI are real. | Full Values table toolbox and populated installed comparison remain open. |
| Alarm draft zoom, visible match/fallback, minimum zoom, disabled rule, invalid/empty state, narrow/hidden sidebar, repeat/busy, late scope/cache changes, bounded regex and keyboard | Actual Workspace/QML Preview; `native.alert_ui.reference_zoom_preview_*` | `native-alert-zoom-lifecycle.log`: 36/36 pass, 18.70 s; expanded editor 104/104 pass, 26.56 s. | These CTests use offscreen/software rendering. An inherited Cocoa environment does not override their registered offscreen environment. The separate direct Cocoa attempt failed to acquire a frame; it is not a pass. |
| 5,000-resource cached draft zoom input to completed frame | Same actual Preview control, 20 input/frame samples per style | Software Basic p50 31.693 ms / p95 31.851 ms / max 32.910 ms; Fusion p50 31.717 ms / p95 31.858 ms / max 31.941 ms. No preview transport. | Not a Metal, idle-CPU, three-session, log-retention or whole-release performance result. |
| Source alias save/reset/repeat/refresh/restart/new snapshot/cancel/Escape/lock failure/oversize/keyboard/narrow viewport | Actual Settings Sources Rename dialog; `native.management.source_alias_*` | 24/24 pass, 2.80 s after the visual delegate locator correction. | Removal semantics await a decision. Filter assignment and full installed-source parity remain open. |
| Source alias CLI, original format-1 upgrade/reset, immutable YAML/identity, sibling isolation, malformed format-2 rejection and preserved damaged bytes | `podlord-source --profile PATH rename CONTEXT DISPLAY_NAME`; `native.source_alias_contract.*` | Included in `native-source-alias-chrome-corrected.log`: combined sources, management, themes and reference-alert regression 376/376 pass, 52.32 s. macOS-normalized Unicode is compared by visible equivalence. | Concurrent CLI mutations and actual installed populated-source interaction remain separate checks. |
| Native dark/light window appearance | Installed portable app, actual Settings pointer/keyboard actions | Original dark app has a white title bar; candidate has a dark title bar, switches to light and back. Original screenshots: `desktop-evidence/2026-10-06-source-alias-chrome/native-{dark-before,dark-after,light-after}.png`. | Empty workspace only; not a populated accessibility, all-theme, Windows/Linux or mobile result. The official Qt application color-scheme hint is platform dependent. |

Portable source-alias/zoom candidate: executable SHA-256
`d6691c76b27005276e679bec642de5ccaa9821e2a1774879ffae50bad3a535aa`,
ZIP SHA-256 `0306d892ce70eff9bd4b257e906880ac6a88e7ed4a394b3b9d2dff8231405801`,
31,333,847 download bytes and 80,759,896 installed regular-file logical bytes.
`native-source-alias-zoom-package.log` proves portable dependency paths, declared
size bounds and local ad-hoc signing, not Developer ID, notarization, clean-device
startup or release readiness. The subsequent whole-native run is tracked separately;
the preceding complete run remains 3,054 passes with 97.32% line / 82.40% branch
coverage, below the unchanged 90% branch gate.

### What did the populated source-alias candidate actually prove?

- `native-source-alias-zoom-full-corrected.log`: 3,176/3,176 tests pass;
  97.34% line and 82.59% branch coverage. The required 90% branch gate fails.
  This run predates accessibility-lifecycle and alarm-description additions.
- `native-source-alias-zoom-real-review.log`: real local Kubernetes API/UI lanes
  pass, including forwarding, deletion races, Inspector history, filters,
  alarms, metrics and Radar. Both installed applications were also inspected.
  Exact owned Colima profile `podlord-rel-zm-t8qtan`, cluster, containers,
  images and private runtime profiles were removed by the harness.
- Actual native Sources Rename accepts and saves `Local release cluster` while
  canonical context `default` and session `Unnamed` remain unchanged.
- Installed populated native macOS accessibility exposes only the window/menu,
  whereas reference controls remain accessible. Pointer/keyboard actions can
  complete; a main-thread sample does not establish a deadlock. This is an open
  assistive-technology blocker, not a passing component-test substitute.
- `native-accessibility-lifecycle-offscreen.log`: 8/8 public accessibility-tree
  checks pass under Basic/Fusion software rendering. Direct Cocoa empty and
  populated checks pass; Settings fails to obtain a rendered input frame and
  the subsequent dialog case was not run. Native occlusion/exposure remains
  unresolved; there is no complete Cocoa pass.
- Reference count 1,420 versus native 1,844 is not itself a defect: additional
  native discovered kinds are explicitly allowed. Different cameras and cache
  projections do not prove deterministic geometry parity. Typography, spacing,
  table arrangement, all-theme and all-menu comparison remain incomplete.
- Observed native RSS 307,504 KiB and sampled physical footprint about 623 MiB
  are observations, not the agreed three-session/5,000-total-resource benchmark.
- Unmodified side-by-side pixels, C# left/native right, are preserved as
  `desktop-evidence/2026-10-06-source-alias-zoom-real/resources-csharp-left-cpp-right.png`
  and `sources-csharp-left-cpp-right.png`. These show the current differences,
  not release parity. The candidate predates corrected alarm descriptions.
- `native-alert-descriptions-red.log`: five of seven new store scenarios fail
  before implementation. After correction, all alert-store and reference editor
  regressions pass, 145/145 (`native-alert-descriptions-regression.log`). Public
  profile read/save checks cover canonical descriptions, old generic metadata,
  unchanged read bytes, retained activation/custom rules and locked-field denial.
  The full suite, coverage and portable candidate have not yet been regenerated.

### Which diagnostic investigation shortcuts are covered?

`native.guidance.ui_open_events`, `ui_open_yaml`, `ui_open_logs`,
`ui_action_keyboard`, `ui_actions_restore`, `ui_docs` and `ui_docs_failure`
drive the actual Inspector QML controls in Basic and Fusion. The external
Kubernetes HTTP endpoint and browser handoff are the only replaced boundaries.
Tests assert cached navigation without transport, actual container-scoped log
response rendering, return to expanded guidance, keyboard activation and visible
browser failure followed by explicit retry. No Kubernetes write is permitted.

Twelve of fourteen new scenarios fail before the controls/error feedback exist;
the two existing documentation-success scenarios already pass. After
implementation all guidance scenarios pass, 104/104 in 17.77 seconds
(`native-guidance-actions-regression.log`). These are offscreen/software checks,
not installed macOS accessibility, physical browser, mobile or release evidence.
The expanded regression run additionally drives Node MemoryPressure diagnostics,
verifies the absence of the Pod-only log action, and navigates Events/YAML in a
680-pixel window. Guidance plus the complete field-filter suite passes 370/370
in 168.19 seconds (`native-filter-guidance-regression.log`). Full installed local-
cluster investigation remains a separate gap.

### Which cached filter and preset rejection paths are covered?

`native.field_filter.contract_*` adds 41 independent public Workspace scenarios
per style: absent session, unsupported field/mode, repeated/reset edits, sorted
picker values, selection/copy rejection, retained clipboard, exact-case matching,
custom selected values, stale session scope, preset name validation/collisions,
missing/default rename/delete, repeated rename and concurrent save/reload/rename/
delete rejection. Each case uses the real cache, scheduler and private stores;
only the external Kubernetes HTTP endpoint is simulated. Metadata actions must
not fetch Kubernetes or lose accepted filters/presets.

The first run passes 80/82. The two failures were incorrect test expectations:
an invalid filter expression is retained as an editable draft with an error,
while preset saving is rejected. The tests were corrected to the shipped public
contract, not production code. All 82 pass in the subsequent 370-case regression.
No coverage percentage is inferred from this focused run; the 90% release gate
remains unchanged and requires a new complete instrumented run.

### What does the isolated installed accessibility probe establish?

The diagnostic-actions candidate passes dependency/size/ad-hoc package preflight:
ZIP 31,334,636 bytes, installed regular files 80,759,896 bytes; executable SHA256
`d7dcd00d5251cb065f1d8342c85ee1dd850ccba652835cc7b71880bf1f2220a2`, ZIP SHA256
`25039bc1f3b37849b76a3c50db75958edfdb5b6078611367fe199e955e06ac2b`.
This candidate predates the subsequent removal of duplicate appearance controls.

`native-accessibility-isolated-outcome.txt` records actual Cocoa desktop controls
with a fake only at the external Kubernetes HTTP boundary: six then 1,844 Pods,
Settings, Appearance and a modal open/close retain accessible controls. The large
tree has 449 lines. Application switching does not reproduce the earlier root-only
tree. This does not close the mixed-kind real-cluster accessibility blocker.

An occluded-window capture shows the previous Resources frame despite the current
Appearance accessibility tree; explicit Raise refreshes the actual image. Such a
cached frame is not measured UI latency. The attempted theme keyboard selection
does not prove a successful variant change. RSS 238,928 KiB and CPU 0.2% are one
observation, not the agreed performance profile. The bounded probe closes its
app/server and removes its owned private configuration/scripts; no cluster,
container or image is created.

### Does removing the duplicate Appearance editor retain its capabilities?

The redundant dialog, its button/signal/route and duplicated selector state are
removed completely. `native.appearance.*` now drives the primary inline Settings
selectors. It still checks all 19 palettes, both variants and three intensities,
atomic persistence, restart, policy preservation, lock/conflict rollback and
leaving unchanged settings. Radar tests use Graphics controls and the native
water slider's supported Left-key interaction rather than an unsupported Home
key. No palette, effect or saved setting was removed.

`native-settings-single-surface-corrected.log`: all related 236/236 tests pass in
96.90 seconds. The preceding run passed 234/236; its two failures were the
unsupported slider key in the updated test driver, not a retained production
Home-key feature. The isolated build keeps the concurrently running instrumented
baseline's executables unchanged.

`native-settings-single-surface-cocoa.log`: six direct Cocoa/Fusion scenarios
pass: empty/populated accessibility, Settings, command dialog, water-zero and
water-disabled. The public pointer test helper raises and activates its owned
Cocoa window before requiring a rendered input frame, because obscured windows
may stop rendering. This is test-input exposure, not forced production rendering
or a software-backend fallback. It resolves the preceding component-frame
failure; it does not by itself resolve the earlier installed mixed-kind external
accessibility failure. The portable candidate must be rebuilt for this change.

### What Does The Current Full Native Test Baseline Establish?

`native-diagnostic-actions-full.log` completed with 3,291/3,291 passing tests
in 1,430.06 seconds. Instrumented native source coverage is 97.49% lines and
83.23% branches. The 90% branch gate fails; this is not a release pass. This
baseline includes cached diagnostic actions and field/preset contracts, but
predates removal of the duplicate Settings editors and the Ports table increment.

### Does The Sole Inline Settings Editor Retain Sync And YAML Behavior?

The synchronization dialog, button, signal, routing and duplicate fields are
removed. Existing inline Sync and Privacy controls retain read-policy, log
retention and YAML-limit owners. Public QML tests cover save/reopen, unchanged
navigation, invalid input, cross-workspace revision conflicts, file-lock
contention and 360-pixel layouts. Failed saves retain both accepted settings and
editable input. Existing log retention tests return explicitly to Resources.

The first subset passed 330/334: four new lock scenarios had omitted creation
of their private profile directory. Correcting that test setup, not production
locking, yields 334/334 passing in 64.71 seconds. Evidence:
`native-sync-single-surface-regression.log`,
`native-sync-single-surface-corrected.log`. The packaging preflight passes in
`native-settings-single-editor-package.log`; it predates the following
Service-default fix and Ports table and is not the latest complete implementation.

### Which Port-Forward Service And Stream Failures Are Verified?

The real Workspace/QML/store/scheduler/transport sends actual HTTP, WebSocket
and TCP traffic. Only the external Kubernetes API is simulated. Named/numeric
and omitted target ports, valid pagination, invalid/repeated continuations,
selector types/values/keys, resource identity/deletion, candidate Pod identity,
phase/labels, list metadata/items, invalid numeric/named/UDP targets and missing
Service ports have separate public UI scenarios. Empty messages, short port
headers, reversed headers, initial header payload and empty data payload also
exercise real channel parsing, binary echo, rejection and local-port cleanup.

The first expanded run passes 172/174 and reproduces the omitted-targetPort bug
in both styles. A non-const JSON subscript inserted null instead of observing a
missing value. A value-only read restores the Kubernetes default-to-Service-port
behavior. The corrected run passes 174/174 in 134.72 seconds. Evidence:
`native-forward-resolution-regression.log`,
`native-forward-resolution-corrected.log`. No unsupported stream protocol,
automatic auth retry or external dependency is added. Actual-cluster and
installed-app evidence remain separate from this external-boundary run.

### Which Ports Table Capabilities Are Under Verification?

Resources, Events and Ports reuse the actual snapshot model, Qt proxy, shared
virtualized grid, native Actions, column dialog and plain-text overflow popup.
Forward tokens keep rows distinct without changing transport ownership. Search
and sort use session navigation state; global layouts use the existing atomic
store. There is no retained legacy Ports list or redundant Inspect button.

`native.forward.ports_table.*` exercises numeric three-state sorting, context
copy, keyboard inspection, identity colors, narrow layout, hide/pin/order/width,
restart without restarting forwards, locks and revision conflicts. All 24
initial Basic/Fusion cases fail against the absent shared table in
`native-ports-table-red.log`. The post-implementation run is not yet recorded
as passing. Layout-store cases additionally cover read-only version-3 extension,
explicit version-4 save, unsupported versions/types and invalid/missing current
layouts. Installed-app screenshots, refreshed-data position stability and the
whole accepted performance profile still require distinct evidence.

The expanded Ports/table/view regression passes 387/387 in 306.15 seconds:
`native-ports-table-final-regression.log`. It includes the omitted-targetPort
fix, shared table/actions, profile revision/lock behavior, legacy saved-view
restoration and read-only layout migration. Two width regressions fail first
in `native-ports-default-layout-red.log`: the Status column was outside the
normal desktop viewport. Model-owned defaults now fit all seven columns without
rewriting saved user layouts. Keyboard/context URL/copy/stop, tooltip and
session-filter restoration have distinct Basic/Fusion cases.

Earlier failures are not hidden: the 299-case first implementation run exposed
use-after-destruction when ResourceClient teardown notified an already destroyed
proxy. Native backtrace `native-ports-table-crash-backtrace.txt` identifies that
exact path; Workspace disconnects the client before member destruction. The
850-case follow-up exposed a legacy test dereferencing the removed Ports-list
selector (`native-shell-sidebar-crash-backtrace.txt`), a saved-view schema
boundary error and a conflict test incorrectly counting another workspace's
legitimate startup requests as column-edit requests. Production saved views
retain their two-table schema; only column layouts explicitly include Ports.
The conflict test settles that independently owned startup before observing
column edits. There is no compatibility list or hidden old selector.

The actual-cluster lane in `2026-10-06-ports-table-real` exits zero. It exercises
1,051 labeled objects, including 512 ConfigMaps and 256 Secrets, plus forwarding
and deletion fixtures, through real Kubernetes HTTP/WebSocket/TCP traffic.
Pod/Service forwards, delete/recreation race, inspector history, cached search,
radar health/metrics and cached field filters pass. The outer trap deletes its
owned `podlord-ports-k0xoze` Colima VM and runtime data after the lane removes
`podlord-visual-run-ecls3t` and owned volumes/profiles. No task-owned test image
remains; the pre-existing default VM is untouched. See
`native-ports-table-real-outcome.txt` for snapshot and renderer limitations.

The interrupted whole-suite attempt is not a full baseline. Its scheduler was
paused while active test processes finished normally, then interrupted before
restarting after the saved-view schema correction. The native test runner now
owns a private TMPDIR below its fresh coverage-run directory and recursively
removes that directory without following links. The subsequent complete
3,425-case run passed 3,423 cases in 1,398.54 seconds. Its failures were the narrow
Fusion alarm zoom preview and Basic alarm-group typing, not the removed Ports
selector. That snapshot predates the batched cache publication and bounded
log-preview changes. Nine direct Ports Cocoa/Fusion cases subsequently passed.
Line coverage was 97.65%, branch coverage 83.68%; the unchanged 90% branch gate
remains open. See `native-ports-table-current-full.log` and
`native-ports-table-final-cocoa.log`.

### Which cache publication and log-preview changes are verified?

Resources, Events and Ports keep the same model owner and persistent row
identities. Publication groups contiguous removals and insertions and combines
changed-row notifications without resetting the table. A ranking/profile commit
for the already active session no longer republishes its unchanged cache after
the immediate session activation already did so.

The 854-case forward/log/table/filter/alarm regression passed 853 cases. Its
remaining narrow Fusion preview failure reproduced alone. Pointer diagnostics
showed that the driver classified clipping before the resized layout completed:
the control ended at y=777 outside a viewport ending at y=648. The shared public
input driver now awaits the resize frame before classifying clipping. The
previously failing case then passed 12 consecutive executions in 25.58 seconds.
This is a driver correction, not a special-case production layout or a complete
post-change suite result.

Log display previews are bounded to 1,024 UTF-16 code units without splitting a
surrogate pair. Full cached entries remain available through Open, double-click,
Return/Enter and copy. Closing the entry dialog clears its hidden text. Eight
Basic/Fusion cases exercise long ASCII, Unicode, keyboard opening and exact
clipboard contents through the actual log UI and external HTTP boundary. Their
failing-first run passed 1/8; all eight passed in the expanded regression. The
external boundary alone is simulated; log ingestion, cache, model and QML are
real.

### What does the three-session performance protocol establish?

`scripts/measure-native-performance.sh` owns an external local HTTP API, three
private sessions and its profile/process cleanup. The real UI receives 5,000
total objects: 1,500 Pods, 2,334 ConfigMaps and 1,166 Secrets, with one visible log
and the default 5 MB retention. Each action class has five warmups and thirty
retained input-to-frame samples, followed by ten retained steady batches and
sixty seconds of nonsynchronizing idle. The RSS growth allowance is 5 MB.
These are runtime preflights, not installed startup or UI work per frame.

The isolated pre-batching Cocoa run measured filter p95 17.762 ms, sort p95
38.175 ms, cached inspector p95 17.943 ms and cached session-tab p95 162.445 ms.
The first three met their individual budgets; tabs failed 50 ms. RSS reached
301,662,208 bytes, failing 250 MB, while warm growth of 1,900,544 bytes passed.
Idle CPU was 3.038% of one core across 62.616 nonsynchronizing seconds, failing
2%. Evidence: `2026-10-06-three-session-performance-red`.

The post-batching foreground run measured filter p95 15.699 ms, sort p95
48.843 ms, cached inspector p95 15.666 ms and tabs p95 82.460 ms. Tabs still
failed. That run then stopped at a frame-driver failure before memory/idle
measurement; it is not a complete profile result. The driver now accepts an
already observed frame rather than waiting blindly for another. It requires an
exposed, active Cocoa window before timed actions and invalidates idle evidence
when foreground exposure is lost. Optional process inspection happens only
after measured actions and idle. Evidence:
`2026-10-06-three-session-performance-foreground`.

### Are bundled dependency notices reachable without network activity?

About reuses the existing read-only license dialog. Twelve new Basic/Fusion
cases first failed, then the complete 48-case About set passed in 11.15 seconds.
Cases cover original supplier text, exact copy, Escape cleanup, keyboard reopen,
680x480 layout and reopening Podlord's own license without stale notices. No
browser or Kubernetes request is triggered. Evidence:
`native-dependency-notices-red.log`, `native-notices-final-regression.log`.

The package script now takes macdeployqt and the original supplier SPDX files
from the actual configured Qt SDK and preserves license texts before signing.
Fresh artifact execution remains necessary. Corresponding sources, all
transitive notices, replacement/relinking, Developer ID/notarization and other
platforms remain distinct release gates, not inferred from this UI result.

## What Did The Latest Native Regression And Package Runs Prove?

The 2026-10-06 native regression snapshot includes batched cache publication,
bounded log previews, dependency notices, and the corrected pointer/keyboard
input drivers. All 3,445 registered tests passed in 740.71 seconds. Measured
line coverage is 97.65%; branch coverage is 83.69%. The test command therefore
failed the existing 90% branch gate, not an executed test. Passing component
paths does not establish installed-app visual parity or hardware performance.
The execution log is `native-cache-notices-current-full.log` in the local
release evidence directory.

The subsequent macOS arm64 notices package passed its dependency,
minimum-OS, and ad-hoc signing checks. The ZIP is 31,545,235 bytes and the
installed bundle is 84,637,934 bytes. Its ZIP SHA-256 is
`17c8f22437422190dbef7ef6de1b81928c83c4e30d4937be212cefe5872f7fd4`;
the application binary SHA-256 is
`3cfe3caab8b3996d5d62b834cd8715bb6c710d9754433a4976b147189f2e0983`.
This supersedes the preceding pending package execution note. The artifact
is `2026-10-06-native-notices-package`; the log is
`native-notices-package.log`. Neither ad-hoc signing nor supplier SPDX
metadata establishes notarization, complete license obligations, or release
readiness.

## What Did The Unlocked Desktop Performance Run Establish?

The 2026-10-06 Cocoa/Qt 6.11.2 hardware run completed with three sessions,
5,000 cached resources, and three retained log histories at the configured
5 MB per-history limit. Each interaction class used five warmups followed
by 30 retained action-to-frame samples. The measured p95 values were:
filter 33.052 ms (100 ms gate), sort 51.780 ms (100 ms gate), cached session
tab 122.190 ms (50 ms gate), and cached inspector 43.214 ms (50 ms gate).
The tab maximum was 142.355 ms, also exceeding the 100 ms stall gate.

Maximum steady RSS was 302,645,248 bytes, failing the 250 MB gate. Warm
memory growth was 2,129,920 bytes, within the 5 MB tolerance. During the
60,817 ms nonsynchronizing idle interval, CPU was 1.2634% of one core,
passing the 2% gate; foreground visibility was not lost. This supersedes
the earlier incomplete post-publication hardware runs, not their historical
observations. The overall performance command failed because the tab and
absolute memory gates remain open.

Evidence is `2026-10-06-three-session-performance-unlocked` and
`native-three-session-performance-unlocked.log` in the local release
evidence directory. The external local HTTP API is the simulated boundary;
Workspace, cache publication, QML, and the platform render loop are real.
The runner owns and cleans its API/application processes and temporary
profiles. This run does not establish installed-process startup, GUI work
per frame, cross-platform behavior, or release readiness.

## What Do The Further Native Performance Changes Establish?

The native log cache now retains UTF-8 bodies and validates/slices response
lines without decoding and splitting the whole response into UTF-16 strings.
Display previews decode a bounded prefix; explicit full-entry copy/open remain
exact, including Unicode, initial/body BOM, empty bodies and CRLF. Radar
collision placement retains the reference's deterministic candidate order
without sorting each perimeter. Radar tiles batch notifications. Settings load
on first use and remain retained afterward to preserve drafts. Three derived
terrain entries are cached with exact scope/topology signatures, including UID
recreation; no model indexes or credential data are retained in that cache.

Public tests passed: 172 initial log/radar cases; 306 expanded lazy-settings,
commands and radar cases; 119 terrain cases; all nine C# coordinate-reference
and terrain replay/recreation cases; and 525 final notification cases including
actual numeric filter UI scenarios. These are increment-specific executed
paths, not a fresh complete-suite or installed-release proof. Logs are
`native-private-performance-regression.log`,
`native-private-radar-batch-regression.log`,
`native-private-terrain-regression.log`, `native-private-terrain-cases.log`,
and `native-private-final-notifications-regression.log` in local release evidence.

A complete Metal hardware run before the terrain cache and narrowed table
notifications measured tab p95 98.481 ms and maximum steady RSS 276,807,680
bytes. Filter p95 42.803 ms and inspector p95 43.055 ms passed; 60,495 ms idle
CPU was 1.21810% of one core with foreground retained. Tab and absolute RSS
still failed their 50 ms and 250,000,000-byte limits. Evidence:
`2026-10-06-private-release-batched` and
`native-private-release-batched.log`.

The subsequent 1024-square atlas experiment measured tab p95 81.669 ms and
RSS 254,443,520 bytes, both still over their limits; idle visibility was lost
after 55,482 ms, so it provides no valid idle pass. The same documented atlas
dimensions now default through one shared app/benchmark initializer, preserving
explicit Qt environment overrides. The later default-configuration run verified
Metal/Apple M5 Max/1024-square atlas, measured tab p95 85.335 ms and RSS
274,694,144 bytes, and lost foreground after 6,021 ms. Filter p95 34.716 ms and
inspector p95 31.671 ms passed. It does not establish a stable memory gain or
an idle pass. Evidence: `2026-10-06-native-private-default-graphics` and
`native-private-default-graphics-performance.log`. Performance remains open.

## Which Boundary Tests The Confirmed Context Cascade?

`native.source_removal.*` invokes the real Workspace and source/session stores;
the only simulated service is an external HTTP Kubernetes boundary denying
authentication. The initial `background_Basic` scenario failed because the
confirmation action was absent. The first implementation passed local catalog
contracts but its eight pointer/keyboard cases failed on delegate lookup; the
test now reuses the existing visual-delegate locator and real scrolling helper.
The corrected initial 30-case Basic/Fusion set passed. Expanded schema and
public-input cases plus the existing source/session suite still require the
next run; do not infer their result from those 30 paths.

| Behavior | Public boundary | Test | Remaining evidence |
| --- | --- | --- | --- |
| Confirm, cancel, active/background and no-session context | Workspace and rendered Main/Settings | `native.source_removal.active_*`, `background_*`, `empty_*`, `cancel_*`, `ui_*`, `keyboard_*`, `narrow_*`, `cancel_ui_*` | Passed; Cocoa confirmation frame retained. Complete installed-view parity remains separate. |
| Exact-membership confirmation and failed atomic commit | Workspace with real source/session writer locks | `native.source_removal.conflict_*`, `locked_*`, `source_locked_*`, `corrupt_*` | Passed. Wider concurrent-process evidence remains separate. |
| Strict external confirmation and exclusion catalog | Public source/store APIs and real persisted documents | `native.source_removal.contract_*`, `schema_*`, `snapshot_removed_*` | Passed in the final full run. |
| Restart, explicit reimport and final context removal | New Workspace, source resolution and persisted catalog | `native.source_removal.restart_*`, `reimport_*`, `remove_all_*`, `rename_other_*`, `rename_other_noop_*` | Passed; unrelated rename metadata also excludes removed contexts. |
| Closing live forward ownership after context removal | Rendered Settings confirmation after actual local TCP/WebSocket traffic | `native.forward.context_removal_*`; real `port-forward-ui-test real_context_removal` and `real_context_draft` | Passed against both external-boundary service and owned real Kubernetes. |

## What Passed For The Current Private macOS Artifact?

The final 2026-10-06 native run passed all 3,525 registered tests in 709.91
seconds. Line coverage is 97.66%; branch coverage is 83.81%. The command exits
unsuccessfully because the required 90% branch gate remains unmet, not because
an executed test failed. Evidence: `native-private-context-cascade-final-full.log`.
This supersedes the interrupted preceding run, which exposed an outdated test
navigation assumption after Settings became lazy-loaded. The shared Alerts
test driver now opens Settings when its target does not exist yet, rather than
requiring eager product UI. Its 191-case Alerts/startup subset also passed.

The 68 context-removal cases cover the confirmed workflow and strict public
input/catalog boundaries. The source/session/forward management subset passed
624 cases in 95.29 seconds. Evidence: `native-source-removal-final.log`.

The final owned Kubernetes lane passed Pod and Service forwarding, context
cascade port release, owned YAML-draft protection, actual resource deletion and
UID race rejection, inspector history, cache-only search/field filters, Radar
metrics, default alarms and yellow Pending-PVC/red failed-Pod presentation.
Its seed contains 1,051 actual resources, including 512 ConfigMaps and 256
Secrets. The metrics workflow saved 38 native theme/variant frames. Evidence:
`2026-10-06-native-context-cascade-final-kubernetes`, run
`podlord-visual-run-vnyxw6`. The earlier `yb3iwp` context-cascade cases passed,
but that complete lane failed on the now-corrected Alerts test navigation; it
must not be represented as a complete pass. Both runs removed their owned
containers, volumes and Colima profiles; final cleanup also removed owned VM
runtime data. The existing default/addon-showcase profiles were untouched.

The Cocoa confirmation frame is `native-source-removal-confirmation-cocoa.png`.
It is actual Main/Settings rendered by the native test driver; its only simulated
service is external HTTP authentication failure. The actual Kubernetes lane's
confirmation frames retain the populated real-resource Radar. Neither is an
all-view C#/C++ comparison or a clean-device proof.

All 232 audio playback/reuse cases passed in 46.19 seconds using QCoreApplication
and Qt's current FFmpeg backend, without GUI application initialization. The
initial QCoreApplication experiment with the explicitly selected Darwin backend
timed out and was stopped; it is not successful evidence. The current checks
exercise native QMediaPlayer/QAudioOutput completion and real embedded assets,
not a pretend decoder. Evidence: `native-audio-current-backend.log`.

The private macOS arm64 package passed dependency closure, ad-hoc signature
verification and both size limits: ZIP 31,559,876 bytes; installed bundle
84,672,350 bytes. ZIP SHA-256:
`611978e19e7ba233dc23b5581b7557fb82fde7b75e3092440cf7223666e2b370`.
Application binary SHA-256:
`f4e10e564bb8e5f6694980e4c82fcf5ca38953cbf2ef467ea06f89b2fce82bb4`.
Artifact: `2026-10-06-native-private-context-cascade-package`. Hash labels are
relative to that artifact, not deleted packaging work directories, and the
retained `installed-artifact.sha256` checked both files successfully.

All 30 public CLI/startup cases passed against that packaged executable through
Cocoa with SDK/plugin/loader overrides removed. The package intentionally
contains Cocoa, not the offscreen test platform; the initial offscreen attempt
was invalid for this artifact. Both startup drivers now honor the existing
explicit test-platform selection. Evidence:
`native-private-installed-cocoa-startup.log`. This tests the actual packaged
process on this host, not a clean device or every installed workflow.

Developer ID/notarization is deferred by the private-build decision in ADR 0035.
Complete capability/visual parity, the interactive terminal, other device targets,
remaining coverage and hardware-performance gates are not closed by these runs.

## What Does The Interactive Native Terminal Actually Prove?

The inspector terminal is an implemented native VT/PTY capability, not the
command palette, a command-output text panel or a kubectl subprocess.
[ADR 0042](../adr/0042-native-container-terminal.md) owns protocol, dependency,
identity, lifecycle and resource bounds.

| Behavior | Public boundary and executable | Evidence and limit |
| --- | --- | --- |
| Explicit Pod/container/shell selection and fresh identity check | Main QML, Connect; `native.terminal.connect`, invalid input, completed, recreated and removed-container cases | Actual product runtime with an explicitly fake external Kubernetes HTTP/WebSocket boundary; no internal transport stub. |
| VT output, Unicode, alternate screen, keyboard/interrupt, resize and bounded history | `native.terminal.input`, `interrupt`, `ansi`, `alternate`, `unicode`, `resize`, `history` | Native parser and rendered Qt Quick surface, not a simulated product terminal. |
| Exit success/failure, malformed/chunked status and TCP EOF after authoritative status | `native.terminal.status_*` | EOF regression reproduced failing-first; a successful Kubernetes exit status must not become a generic TLS/authorization error. |
| Do not steal shell shortcuts | `native.terminal.shortcut_input` | Control-K initially opened the application palette and failed both styles; fixed at the focused native surface's shortcut boundary. |
| Paste cancellation, confirmation and immutable clipboard snapshot | `native.terminal.paste_cancel`, `paste_confirm`, `paste_snapshot`, `clipboard_isolation` | No remote OSC clipboard mutation; unreviewed clipboard replacement is not transmitted. |
| Session ownership, close, explicit reconnect, hidden inspector output and session switch | `native.terminal.session_close`, `session_switch`, `reconnect`, `hidden`, `repeat` | Actual session store/client ownership; no automatic shell recreation. |
| Authentication, permission, rate limit, redirect, protocol and malformed/oversized stream failure | Target/upgrade error cases, `protocol`, `bad_channel`, `text_frame`, `oversized` | Explicit failure without automatic retry; this is not production identity-provider evidence. |
| Interactive shell, PTY size, vi file read-back and Control-C | `scripts/test-native-visual-kubernetes.sh native-terminal-e2e`, `real_shell`, `real_vi`, `real_interrupt` | Passed against actual K3S v1.35.5-k3s1 with 1,051 seeded resources and a two-container Pod, through actual Cocoa/Metal Qt windows. |

The successful real run is `podlord-visual-run-u3qq3x` under
`/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-06-native-interactive-terminal-cocoa`.
Its three originals are `*-terminal-real_shell.png`, `*-terminal-real_vi.png`
and `*-terminal-real_interrupt.png`. The vi check writes through interactive
editor input and independently reads the file after returning to the shell;
seeing echoed commands alone cannot pass it. Escape and the subsequent vi
command are separate keyboard actions, allowing vi's normal escape timeout.
All three shells exited with an authoritative successful Kubernetes status.
The lane deleted its labelled container, volumes and private run data. Its
owned Colima profile and VM data were removed; default/addon-showcase stayed
untouched. Earlier real attempts exposed exit-status handling and vi keyboard
sequence timing and are not represented as complete passes.

All 136 terminal/About style cases passed in 39.20 seconds before the final
zero-size geometry/closed-terminal shortcut guards. The subsequent integrated
terminal/About/forward/log/inspector-navigation/context-removal check passed
516 cases in 172.71 seconds (`native-terminal-integrated-regression.log`). This
is affected-path evidence, not a new complete coverage report. Other devices,
physical assistive technology, terminal reconnect across process restart and
an installed packaged-terminal workflow are not established by these runs.

## Can Native Table Search Move Between Cached Matches Without Filtering?

The shared Resources/Events/Ports table now has an explicit Find bar with
next/previous matching-row selection and wrap-around. It reuses the existing
bounded ResourceFilter parser and Qt proxy model; it does not create an API
read, open the inspector or change the resource/Radar filter. Closed/hidden
find surfaces detach their matching model instead of maintaining another
hidden refresh loop. Control/Command-F opens the active table's Find bar,
Enter advances, and Escape closes it.

The initial 22 Resources scenarios passed in 10.89 seconds across Basic/Fusion:
next/previous/wrap, Enter, keyboard open/close, empty/no match, invalid regex,
valid regex and narrow layout. Their external-boundary driver also asserts
unchanged table rows, session filter, inspector target and request count.
Evidence: `native-table-find-ui.log`. Events and Ports now have their own public
Find checks, including unchanged request count and forward ownership. All-view
paired C#/C++ visual evidence remains a separate gate.

## What Is Proven For Inspector Tables And Terminal Paste?

Inspector Events and Links reuse the Resources/Events/Ports grid. Their public
tests cover typed ASC/DESC/NONE sorting, session restart, copying, Find, keyboard
and context-menu inspection, hide/pin/width/default layouts and narrow windows
in Basic and Fusion. Link context-menu inspection initially opened the wrong
endpoint in both styles; its shared-menu fix preserves the row identity for
copy and opens the clicked endpoint for inspection. Events remain UID-bound.

Layout-store tests cover version-5 defaults and read-only older-file upgrades,
unknown tables, invalid/current-incomplete records and atomic save. View-store
tests cover version-4 auxiliary defaults and the corresponding failure cases.
Port filter/sort restoration does not recreate forwards. Twelve focused
restart/migration/scope checks passed in 8.17 seconds before the combined run.

Terminal paste has six additional public scenarios: empty clipboard, oversized
ASCII, oversized Unicode, valid 65,536-byte ASCII, valid 65,535-byte Unicode and
bracketed paste. All twelve style cases first failed against the prior runtime:
the dialog allowed invalid input and accepted paste emitted per-character
frames. Invalid input now has visible error feedback and disabled confirmation,
with no truncation or remote input. Valid paste preserves every byte in one
bounded stdin frame; bracketed-paste delimiters are retained.

The combined terminal/inspector/table-layout/view-state/forward/Find regression
passed all 333 cases in 78.31 seconds. Evidence:
`native-terminal-inspector-shared-regression.log`; failing-first paste evidence:
`native-terminal-paste-before.log`. These results use actual native QML and
stores with an explicitly fake external Kubernetes boundary. They do not prove
Values/Alerts/Diagnostics table parity, all-theme paired screenshots, other
devices, an installed-package terminal workflow or complete release readiness.

## What Does The Current Terminal And Inspector Evidence Prove?

The 2026-10-06 integrated regression run passed 833/833 cases in 279.15 seconds after rebuilding all test targets. It covers terminal interaction, inspector Events/Links tables, search restoration, table layouts, session view state, and About notices. This is a scoped regression result, not proof of complete product parity or coverage gates.

| Behavior | Public entrypoint | Executable evidence | Remaining gap |
| --- | --- | --- | --- |
| Physical Control keys, GUI shortcuts, safe Copy/Paste and keyboard escape to local controls | Native terminal UI in Basic and Fusion styles | `container-terminal-ui-test`, including `copy_no_selection`, `gui_modifier` and expanded terminal cases | Other operating systems and device keyboards |
| Responsive expanded terminal and on-screen keys | Native inspector and terminal controls | Integrated terminal UI cases; real Kubernetes `real_vi` and `real_touch_interrupt` | Current installed bundle's visible terminal workflow |
| Interactive shell, vi file editing, physical and on-screen Ctrl+C | Native UI connected to an owned local Kubernetes API | Four real terminal scenarios passed against K3S; vi output checked independently and test files removed | This latest real run used offscreen/software rendering because the desktop was locked |
| Search and sorting survive restart | Resources, Events, Ports and inspector table controls | Integrated session view-state and restart tests | Full saved-filter import parity |
| Pointer access to About notices after scrolling | Native public UI | Basic/Fusion narrow-view tests, including 20 repetitions per style | Full theme/view screenshot matrix |
| Private package opens without SDK runtime overrides | Actual packaged macOS executable | 30/30 CLI and Cocoa startup scenarios passed | Clean-machine installation and installed real-cluster terminal workflow |

The earlier 3,710-case run had three failures: two obsolete search-state schema expectations and an About pointer click while a synthetic wheel gesture remained open. The schema expectations now derive column identifiers from the current public models. The shared test input boundary now ends its wheel gesture before pointer activation; production About behavior was not changed to accommodate that test failure. The affected paths passed in the integrated run. A new complete suite is still required before claiming a current all-tests result.

Private artifact: `2026-10-06-native-terminal-private-package/podlord-native.app` under the local release-evidence directory. The ZIP is 31,648,407 bytes and the installed bundle is 84,910,763 bytes. Dependency-path and ad-hoc signature checks passed. Executable SHA-256: `1cdce2eb8cdb73f35b9d53fba3dc325acd1dbb146c41b9a6a9210db850f7ca95`; ZIP SHA-256: `fe6b247be77f6c9a2007939771eeb1a40c0d938582ee3163fe9d300c76794026`. Developer-ID signing and notarization are intentionally deferred for this private artifact.

Evidence logs: `native-terminal-final-integrated.log`, `native-terminal-expanded-real-kubernetes.log`, `native-terminal-private-package.log`, and `native-terminal-private-installed-startup.log`. The latest four real terminal screenshots are in `2026-10-06-native-terminal-expanded-real-kubernetes`. They are native terminal evidence, not current C#/C++ comparison pairs. Both ephemeral Colima profiles created for these runs, their VM data, labelled test containers and test volumes were removed. Pre-existing environments and the frozen C# reference were retained.

Complete functional/visual parity is not established: Values/Alerts/Diagnostics table interaction, multi-window ownership, localization, complete relationship/metric variants, the current paired theme/view matrix, performance budgets and the branch-coverage gate still need evidence or implementation. The previous branch-coverage result was below its required 90% gate.

## What Does The Complete Terminal-Baseline Run Establish?

On 2026-10-06 the complete native suite passed 3,742/3,742 registered cases in 823.83 seconds. This supersedes the earlier three-failure result for the terminal/table baseline. It includes the current terminal keyboard, clipboard, inspector expansion and shared table changes. It ran before the subsequent Alarm-table keyboard increment; it is not a claim that every later source change has completed a new whole-suite run. Log: `native-terminal-complete-current-suite.log` in the local release-evidence directory.

The unchanged C# reference source also passed 429 executed cases: Core 91, App 214, headless Layout 69, and the external-boundary `FakeKubernetesBehaviorTests` 55. Ambient kubeconfig and application profiles were isolated. This was not the C# real-cluster lane, nor a current frozen-reference screenshot comparison. Logs: `native-parity-reference-Podlord.Core.Tests.log`, `native-parity-reference-Podlord.App.Tests.log`, `native-parity-reference-Podlord.App.LayoutTests.log`, and `native-parity-reference-local-api.log`. The private profile created by the headless reference run was removed after completion.

The desktop controller still reports a locked Mac. Current visible C#/C++ screenshot pairs and foreground Cocoa performance measurements therefore remain unverified; no offscreen frame is labelled as a visible installed-app comparison.

## What Is Proven After The Alarm Keyboard Increment?

The final rebuilt native suite passed 3,770/3,770 cases in 843.39 seconds on 2026-10-06. Log: `native-parity-terminal-alarm-complete-suite.log`. All targets were rebuilt before this run. This result supersedes the intermediate 704-case run with four pointer-helper failures.

| Behavior | Public entrypoint | Test case | Result |
| --- | --- | --- | --- |
| Platform Copy for all displayed alarm columns | Keyboard Tab traversal and Copy shortcut | `native.alert_ui.reference_cell_copy*` in Basic/Fusion | Passed; clipboard contains the displayed full value, rules unchanged, zero API requests |
| Context menu without a pointer | Menu and Shift+F10 | `reference_cell_menu`, `reference_cell_shift_menu` | Passed in both styles |
| Dismiss without copying | Escape from the context menu | `reference_cell_escape` | Passed; clipboard and rules unchanged |
| Retain the selected column while moving between rules | Up/Down/Home/End | `reference_cell_navigation`, `reference_cell_home`, `reference_cell_end` | Passed in both styles |
| Keep focused controls reachable horizontally | Tab, reverse Tab and window resize at 720 by 720 | `reference_cell_narrow`, `reference_cell_narrow_reverse`, `reference_cell_narrow_resize` | Passed in both styles |
| Preserve pre-existing row selection and sorting | Pointer row selection, keyboard navigation, sort headers | `reference_keyboard`, `reference_sort` | Passed after the shared pointer helper was corrected |

There are 28 new keyboard cases. The initial ten failed before cells became keyboard reachable; the narrow-focus pair and reverse-Tab pair also failed before their fixes. The existing pointer helper incorrectly required an entire intentionally wider-than-viewport row to fit. It now requires the actual click point on oversized axes while preserving full bounds checks on ordinary controls. The 32 focused old/new cases passed in 6.73 seconds before the final whole-suite run. No production table width was reduced to satisfy that helper. An inspected native offscreen frame is retained as `native-alert-keyboard-narrow-final.png`; it is not a C# comparison image or a visible installed-window capture.

The latest private package is `2026-10-06-native-terminal-alarm-private-package/podlord-native.app`. Its ZIP is 31,649,407 bytes and its installed regular-file logical sum is 84,910,763 bytes. Executable SHA-256: `758ac2d6f947b780827834252a52c415f500ebc1b9d007e6862087cebbf05027`; ZIP SHA-256: `3c47d1e72afcfe467249abfdec8d7e76f5a71695fef75831dfcb6aa25bae65dd`. Dependency-path and local ad-hoc signature checks passed. The actual new packaged executable passed all 30 CLI/Cocoa startup cases without SDK runtime overrides. Evidence: `native-terminal-alarm-private-package.log` and `native-terminal-alarm-private-installed-startup.json`. This is not Developer-ID signing, notarization, clean-machine proof or an installed terminal interaction proof.

Full functional/visual parity remains open. This increment completes keyboard access within the existing alarm table, not its remaining column toolbox or Values/Diagnostics table parity. Multi-window behavior, localization, full relationship/metric coverage, the current paired view/theme matrix, foreground performance budgets and the branch-coverage gate remain separate work. The Mac was still reported locked during the attempted visible comparison. The unchanged frozen C# reference and all evidence logs/images are retained; superseded private C++ bundles created by this task are removed.

## 2026-10-06: Which Filter, Test-Cost And Cached-Tab Gaps Are Closed?

| Behavior | Public boundary | Regression cases | Evidence / remaining gap |
|---|---|---|---|
| Sidebar field selection opens a bounded nonmodal flyout | Real Qt Quick input; external Kubernetes HTTP fake only | `native.field_filter.flyout_open_Basic`, `flyout_edge_Basic` | Passed; the full field picker still uses the same centered dialog. |
| Expressions and cached choices filter without API requests | Real Qt Quick text / keyboard input | `flyout_edit_Basic`, `flyout_keyboard_Basic`, `flyout_keyboard_Fusion` | Passed; editing retains the open nonmodal flyout. |
| Escape, outside navigation and recycled anchors close the flyout | Real Qt Quick keyboard, navigation and public ListView scrolling | `flyout_escape_Basic`, `flyout_navigation_Basic`, `flyout_anchor_scroll_Basic` | Passed; anchor scrolling was reproduced as a failing regression before the fix. |
| Narrow field selection stays within the viewport | Real Qt Quick input after resize | `flyout_narrow_Basic`, `flyout_narrow_Fusion` | Passed; centered modal selection remains reachable after the drawer closes. |
| Session changes close stale choices and retain each session's filter | Public Workspace activation plus real Qt Quick input | `flyout_session_Basic` | Passed; returning to a fresh cached session sends no new API request. |
| Rebinding only the fallback cluster does not invalidate unrelated row metadata | Public Qt table model using accepted Workspace cache rows | `contract_cluster_rebind_Basic` | Failed before the notification change; passed after it. Cluster text changes and resource identity remains intact. |
| Nondisplayed activity metadata still updates the filtered set | Public Qt table / proxy model using accepted cache rows | `contract_metadata_activity_update_Basic` | Passed; payload notifications retain the row-wide display role needed by the proxy. |
| A cluster predicate updates after a cluster-only rebind | Public Qt table / proxy model using accepted cache rows | `contract_cluster_filter_rebind_Basic` | Passed without an additional production change; protects the reduced notification path. |

The pre-subtraction filter suite passed all 275 cases in 104.19 seconds, including
all 191 rendered UI/style cases. API contracts now avoid loading QML. All 43
retained API contract cases passed after removing 41 equivalent Fusion runs
(13.62 seconds). The 208 session, source and workspace-restoration cases passed
in 30.94 seconds before the subsequent cluster-notification optimization.
Evidence: `filter-test-boundaries-suite.log`, `filter-style-subtraction-suite.log`,
`filter-cached-session-regressions.log`. The additional cluster-predicate case
passed separately (`cluster-filter-rebind-red.log`; no further fix was needed),
bringing retained API cases to 44. The post-notification behavior UI suite passed
all 345 cases in 156.25 seconds (`filter-table-notification-ui-suite.log`). These
are not a claim that every native test was
rerun after this increment.

The test inventory is 3,743 registrations and 2,808 distinct executable/argument
commands, not 3,743 distinct user journeys. Labels separate 2,826 behavior runs
from 917 retained style variants. Different environments can still make equal
commands meaningful. No behavior scenario was deleted or folded into an opaque
combined E2E case. The 41 removed runs were style copies of API contracts with no
rendered controls. The unchanged C# tests remain a reference oracle during the
migration, not a dependency of the native application. Remaining suites still
need case-by-case evidence before claiming all redundancy or obsolete coverage
has been removed.

The recorded pre-optimization foreground run measured cached-tab p95 105.73 ms,
maximum RSS 257.79 MB and idle CPU 0.694% of one core. Reusing the live connection
measured tab p95 91.46 ms, RSS 256.36 MB and idle CPU 0.697%; the 50 ms and 250 MB
gates still failed. The final cluster-notification follow-up had an exposed but
inactive window and failed the foreground precondition before measuring actions.
It is not performance clearance. Evidence directories:
`2026-10-06-foreground-performance-review`, `2026-10-06-cached-tab-performance`,
`2026-10-06-filter-increment-performance`.

A real local K3S run supplied paired C#/C++ Resources screenshots. The capture
shows remaining layout/catalog differences, not full visual parity. Attempts to
open other views through desktop control were not reliably successful and are
kept separately, not counted as proof. The current field flyout has a Cocoa PNG
from the actual QML renderer with the explicit external HTTP test boundary;
this is not an installed-app or real-cluster flyout proof. Evidence directory:
`2026-10-06-live-parity-review` (`comparison-resources.png`,
`native-field-flyout-cocoa.png`, `comparisons.json`). The owned K3S container,
volumes and both comparison processes were removed by the script's cleanup;
pre-existing Colima VMs and shared images were preserved.

Remaining work: Values/Alerts/Diagnostics table tools, full saved-filter migration
and paired filter visual evidence, multiwindow placement/ownership, localization,
the complete view/theme matrix, foreground performance gates and branch coverage.
Private-release signing/notarization remains deliberately deferred, not hidden as
an unexplained implementation blocker.

## 2026-10-07: What Do Values, Preset Import And Linux Verification Prove?

| Behavior | Public boundary | Regression cases | Evidence / gap |
|---|---|---|---|
| Values use the common virtualized table with five reference columns | Rendered inspector, pointer and keyboard input | `inspector_secret_table_*`, ConfigMap Values cases in `workspace_ui_test` | Sort, Find, full-value hover/copy, direct KEY/VALUE/RAW/DEC, reveal and persisted column controls pass in Basic/Fusion. Masked values remain the source for sorting and searching. |
| Fresh inspector data preserves the actual table viewport | Inspector refresh through the external Kubernetes HTTP boundary | Values scroll refresh case in `workspace_ui_test` | A real 78-pixel jump failed on Linux before using the table viewport instead of the enclosing control height. `values-scroll-red.log` retains that failure. |
| Explicit C# saved-filter import is bounded and atomic | Public preset store and Workspace import action | Legacy-array and import cases in `view_state_test`; rendered sidebar import contracts | Native/C# JSON import, identical repeat import, conflicting names, invalid input and file bounds pass. The source file is unchanged. A visible native file-picker journey and complete C# profile migration are not established. |
| Stay retains a YAML draft for Return, keypad Enter and Space | Real Qt Quick dialog input | Existing Return case and new independent keypad/Space cases | Linux exposed Return accepting the dialog despite focus on Stay. Explicit button key handling fixes this safety regression without a double-key test fallback. |
| Empty and virtualized table headers remain testable | Public Qt TableView/HeaderView entrypoints | Event header, repeated sort and Values pointer cases | Tests re-resolve loaded delegates after rendering instead of retaining recycled items. Empty tables do not require a nonexistent body row to locate a header. |
| Native Linux/arm64 behavior works outside macOS | Unprivileged pinned Linux container, real native executable | Same 524 affected behavior/style cases as macOS | All 524 pass on both platforms. The configured Linux x86_64/arm64 CI lanes have not yet executed remotely. |
| Native Kubernetes integration remains intact | Ephemeral local K3S API, TLS and real resources | `scripts/test-native-kubernetes.sh` | Values, Secret masking/write preservation, multi-container logs, patches and delete/UID replacement protection pass. Credential exec authentication is not evidence of an interactive container terminal. |

Final affected-suite evidence: `20261007-verified-macos.log/xml` (524/524,
90.51 seconds) and `2026-10-07-linux-values/viewport-results.log/xml` (524/524,
165.63 seconds). The real local Kubernetes run is `20261007-real-kubernetes.log`.
The owned test containers were removed; shared clusters, profiles and images were
not pruned. The visible Cocoa frame `values-current-cocoa.png` shows the actual
native Values layout using an explicit external HTTP test boundary. It is not
an installed-app, real-cluster or paired C#/C++ screenshot matrix.

The complete macOS run before the final two header-locator corrections passed
3,747/3,749 cases in 550.41 seconds with 12 isolated workers. Its two failures
were empty Event-header test lookup, not an application regression. The final
affected suite covers those corrections and the subsequent viewport fix; a
single post-correction whole-suite all-green result is not claimed. Compared
with the preceding six-worker run (1,071.10 seconds), parallelism roughly halves
test wall time. This does not establish application UI performance.

The current inventory has 3,749 registrations, 2,851 distinct executable/argument
commands, 2,869 behavior registrations and 880 retained style variants. Thirty-
seven Fusion copies of non-rendered guidance contracts were removed; their Basic
behavior cases remain. Rendered UI variants and independent scenarios were not
merged into large E2E methods. The frozen C# reference remains a migration oracle.

Remaining release gates: Alerts/Diagnostics column tools, complete saved-profile
and paired filter evidence, multiwindow ownership, translations, the full paired
view/theme matrix including terminal, foreground latency/memory budgets and
90-percent branch coverage. The previous private package predates this increment
and is not labelled current. Signing/notarization remains intentionally deferred.

## 2026-10-07: What Does The Session-Publication And Radar Follow-Up Establish?

Session-scoped Resource/Event replacement and hidden-grid detachment passed all
996 affected macOS cases in 201.64 seconds (`20261007-session-scope-ui.log/xml`).
Same-session incremental refresh and the existing unscoped cluster-rebind
contract remain covered. This result precedes the later sorting and glyph changes.

The public compact-Radar case failed first in Basic because the Reset text was
clipped; Fusion passed (`20261007-radar-controls-red.log`). After the padding fix,
all twelve compact-control, zoom, layout and glyph cases passed in both styles
(`20261007-radar-visible-glyph.log/xml`). The old glyph cases asserted existence
of an invisible object. They now zoom and focus through public controls and assert
visible Pod/Widget glyphs, including filter reuse. No behavior case was deleted.

The real local Kubernetes terminal lane passed `real_shell`, `real_vi`,
`real_interrupt` and `real_touch_interrupt`. Evidence is
`20261007-current-real-terminal.log` and four native frames in
`2026-10-07-current-real-terminal`. This lane used real WebSocket exec and PTY
streams, not credential-process authentication. Its owned cluster and volumes
were removed. The frames predate the subsequent compact-control correction;
they are not paired C#/C++ or full theme-matrix evidence.

The complete pre-change foreground baseline measured tab p95 107.66 ms, maximum
RSS 264.60 MB and nonsync idle CPU 1.254 percent of one core. The scoped-model
follow-up completed its tab class at p95 81.78 ms, but lost focus in other phases
and still exceeded the memory limit. The later sorting follow-up passed its
filter, sort and inspector classes (p95 45.34, 40.11 and 45.87 ms respectively),
but tab measurement lost foreground and RSS still reached 265.99 MB. Aborted
5/9-second idle phases are not 60-second idle results. No overall performance
clearance is claimed. Evidence directories: `2026-10-07-values-foreground-performance`,
`2026-10-07-session-scope-foreground-verified`, `2026-10-07-sort-reuse-performance`.
Foreground-loss failures now identify the actual exposure/focus precondition;
the benchmark does not discard failed samples or relax limits.

Final table lifecycle verification passed all 3,746 native macOS registrations
in 557.78 seconds with twelve isolated workers
(`20261007-stable-columns-complete.log/xml`) and all 1,285 affected Linux arm64
cases in 528.00 seconds with six workers
(`2026-10-07-linux-values/stable-columns.log/xml`). The earlier full run's four
Events Shift+F10 failures were real focus regressions: applying an unchanged
column order recycled the focused cell. The eight focused Menu/Shift+F10 cases
passed after the shared-grid correction. An older real-Kubernetes driver also
aborted inside Qt's table rebuild while typing a filter. The corrected driver
passed that same large-resource health/Radar workflow through the new focused
`native-health-e2e` lane (`20261007-stable-columns-real-health.log`). Its pending
PVC, red failure-Pod and inspector frames are retained separately from the failed
`20261007-table-lifecycle-real-review.log` lane; both owned clusters were removed.

Five overview registrations duplicated the exact Basic command and environment;
their existing Basic/Fusion registrations remain. The final inventory has 2,865
behavior registrations and 881 real style variants, with no identical
command/environment pairs (`20261007-compacted-inventory.json`). Quantity-filter
variants remain because they operate the real picker and keyboard, not just a
style-independent parser. No unrelated behavior cases were merged.

The updated private arm64 package passed physical dependency, minimum-OS,
architecture, size and ad-hoc signature checks
(`2026-10-07-stable-columns-package`). Package preflight and the health frames do
not establish a complete paired C#/C++ matrix, clean-device or other-architecture
execution. GitHub returned no runs for this working branch; hosted execution is
not inferred from the configured matrix. The earlier broad real-Kubernetes lane
passed terminal, forward, deletion, history, search and metric steps but stopped
at the old driver's health abort before desktop comparison. Its successful steps
are not relabelled as a completed release-review lane.

The final uninstrumented Release foreground run completed without losing focus
(`2026-10-07-stable-columns-performance`). Filter p95 was 44.94 ms, sort 37.76 ms,
cached inspector 32.86 ms, and 60.409 nonsync idle seconds used 1.288 percent of
one core: those classes passed. Cached-session-tab p95 was 96.50 ms with a
114.06 ms maximum; RSS reached 266.47 MB despite only 2.41 MB warm growth.
The 50 ms tab and 250 MB resident limits remain failed release gates. Neither
limits nor unsuccessful samples were removed. This driver is not an installed
startup or complete UI-work-per-frame benchmark.

## Which Alarm And Visible-Filter Regressions Passed On 2026-10-07?

| Behavior | Public boundary | Evidence | Remaining gap |
|---|---|---|---|
| Preserve a cached problem alarm while a background list response is held; clear it when the recovered resource arrives, without replaying focus | Main QML Refresh, Radar color and public alarm matches; only external Kubernetes HTTP is held | `native.alert_ui.radar_refresh_preserves_alarm`; failed before the owner correction in `20261007-background-alert-failing-first.log` | Complete installed view/theme matrix |
| Suppress matches and focus during a partial first sync | Main QML Radar and health summary | Strengthened `native.alert_ui.radar_loading_partial` | Initial-load sound and progress cases remain independently registered |
| Reject a draft preview if synchronization starts before it completes | Public draft preview and Refresh | `reference_zoom_preview_refresh_in_flight_Basic/Fusion`, renamed from the inaccurate `cache_changed` name | Actual changed-cache invalidation is a distinct condition, not proven by starting an unchanged refresh |
| Cache-filter input changes the rendered name cell without extra requests | Main QML input, TableView and natural `frameSwapped`, before/after name-cell pixels | `native.field_filter.filter_render_frame_Basic/Fusion`; additional native Cocoa/Metal/Fusion execution in `20261007-filter-render-frame-metal.log` | Large installed-package viewport and repeated lifecycle reproduction |
| Real Pending PVC and failed Pod render yellow/red after cached filtering | Real owned local Kubernetes, public inspector and Radar; no forced `window->update()` | `20261007-natural-metal-real-health.log`, exit 0; owned cluster `podlord-visual-run-k8gvt6` removed | This driver is not the installed package or a C# paired frame |

All 427 affected Mac alarm/filter registrations passed with 12 isolated workers
in 69.81 seconds (`20261007-alert-filter-public-review.log/xml`). The final Linux
ARM64 focused run passed 12/12 registrations in 6.72 seconds with six workers
(`2026-10-07-linux-values/background-alert-tests.log/xml`); its container used
`--rm`. Neither subset is reported as a new complete-suite run. The final native
inventory is 3,749 registrations: 2,867 behavior cases and 882 genuine style
variants, with no identical command/environment/working-directory invocation.

The real runner cleanup checks passed 15/15 (`20261007-real-health-cleanup-final.log`).
Their external Docker HTTP callback now reports asynchronous assertion failures
instead of leaving a CLI request unanswered. Missing-executable cases provide the
real build metadata and the already-required terminal driver. Coverage-retention
checks use the retained instrumented help executable, not the uninstrumented
performance/package binary; this proves retention, not current-product coverage.

Five original C#/C++ desktop pairs are preserved in
`2026-10-07-current-desktop-pair/index.html` under the local release-evidence
directory. The compositor's decoded-pixel check passed. These are review evidence,
not accepted parity: projections/resource totals differ, one inspector obscures
an Events capture, and the native Problems/filter captures retained an older
visible frame despite newer accessibility state. The desktop lane reached its
900-second deadline and cleaned up its owned apps/cluster/profile; it did not
complete successfully. Small Metal and real Kubernetes natural-frame checks pass,
but do not establish why that installed-desktop observation occurred.

Feature-branch pushes now invoke the existing CI/native preflight rather than
only storing a backup. The configured Linux AMD64/ARM64 and Mac Intel/ARM64 lanes
still require actual hosted results; configuration is not execution evidence.
Signing/notarization remain intentionally deferred for private distribution.
Alerts/Diagnostics column tools, complete saved-profile migration, multi-window,
localization, installed view/theme/terminal comparisons, startup/frame-work/RSS
performance and the 95% line/90% branch coverage gates remain open. The recorded
50 ms cached-tab and 250 MB RSS failures are unchanged by this alarm correction.

## Which table simplification and first hosted CI failures were checked on 2026-10-07?

- Removed the redundant per-cell row layout from the shared resource grid; retained plain-text rendering, identity colors, copy/hover behavior and action accessories. The 416 affected macOS table, filter, find, layout, values and related-resource tests passed with 12 randomized workers in 105.50 seconds. Evidence: `20261007-cell-layout-build.log` and `20261007-cell-layout-tests.log` under the local comparison evidence directory. This does not establish a latency or memory improvement.
- A fresh hosted runner has Docker but not yet k3d when the test runner performs its initial ownership cleanup. Cleanup now discovers owned clusters before requiring k3d. Empty cleanup succeeds without k3d; an owned cluster without k3d fails explicitly without deletion. Both new public-script regressions failed first and the final 15 cleanup cases passed. Remote-Docker and ownership protections remain unchanged.
- Hosted workflow run `37606214179` executed the migration branch. Linux AMD64 completed 3517 cases with three failures; Linux ARM64 completed 3517 cases with four failures. Both include pinned-namespace interaction and YAML preview; ARM64 additionally includes column-sort/copy. These remain blockers pending diagnosis.
- The macOS ARM64 runner reports unhandled background exceptions in UI/application startup and TLS tests. Packaged startup and TLS checks also fail. This is a release blocker, not an unavailable-runner waiver.
- The hosted Linux reports show 96.93% line coverage but only 82.82% AMD64 and 82.70% ARM64 branch coverage, below the required 90%. Failed-run coverage is diagnostic evidence, not an accepted coverage gate.
- The attempted current foreground performance run was invalid: the window was exposed but not active. It provides no new tab-latency or RSS result. The previous cached-tab and memory failures remain open. A currently locked desktop also prevents completing the paired image matrix.

## How are broken native defaults and asynchronous UI tests detected earlier?

- The first hosted macOS ARM64 run also aborts the direct alert-store tests with `std::bad_variant_access`, locating the background exception in unchecked built-in rule validation. Default construction now returns the existing explicit store-result type and identifies the rejected built-in rule and validation reason. This prevents the unchecked variant exception; the hosted platform's underlying validation discrepancy still needs a successful rerun before release acceptance. The unused public default-construction export was removed; stores remain the public boundary.
- The existing alert-store missing-profile and normal application-startup cases run before the full native suite. A failed preflight stops the redundant large run, preserves diagnostic coverage and fails the job. Successful preflight cases are excluded from the subsequent suite, so each registered case still executes once. Neither coverage thresholds nor behavior assertions were lowered.
- YAML preview now waits for visible keyboard focus on Cancel before testing Escape. Pinned/reordered table interactions wait for the column dialog to finish closing before focusing and copying through the public TableView cell API. No production focus or clipboard workaround was added. The Linux YAML preview failure was reproduced before this change.
- The affected macOS run passed 195 cases with 12 randomized workers in 17.98 seconds; Linux ARM64 passed 62 cases with six randomized workers in 12.82 seconds. Evidence: `20261007-hosted-root-fixes-tests.log`, its JUnit XML, `20261007-hosted-linux-root-fixes.log`, and `2026-10-07-linux-values/hosted-root-fixes.xml` in the local comparison evidence directory.
- Hosted macOS native tests use four workers instead of two. Test processes retain independent profiles, local HTTP boundaries and per-process coverage output. Foreground desktop comparisons and benchmarks remain separate from this headless parallel suite.
- The next hosted C# run got past bootstrap and executed the reference tests: 211/214 application, 69/69 layout and 84/84 Kubernetes cases passed. Its three application failures are filter/tab performance budgets on the hosted runner, not a waived reference behavior gate.

## Why did built-in alarms fail on the hosted macOS runtime?

Hosted run `37610956466` now exposes the real validation failure: the first built-in alert did not contain an array of AND criteria inside its OR-group array. The ambiguous nested `QJsonArray{QJsonArray{...}}` construction was replaced with an explicit `QJsonValue` array element in the two production default builders and every matching alarm test input. The parser remains strict; malformed groups are not normalized or accepted. This is a construction correction, not a legacy format fallback.

The 242 affected macOS alarm, store, pooled-radar and application-startup cases passed with 12 randomized workers in 28.85 seconds (`20261007-nested-alert-arrays-tests.log` and JUnit XML). The hosted rerun of this construction correction is required before closing the cross-platform blocker. An instrumented local default-store check also passed; local success alone did not reproduce or waive the hosted failure.

Before the construction correction, the explicit-result fix already allowed the hosted macOS package to pass seven Cocoa startup cases, five command-line metadata cases and all three real mutual-TLS/rejection cases, using the bundled OpenSSL backend. The alarm preflight still failed, so that run is not accepted as a working release.

The native runner now explicitly enables testing and asks CTest to fail when no matching cases exist. A deliberately unmatched preflight was executed and rejected (`20261007-empty-preflight-rejected.log`). Missing test registration must never turn into a successful readiness check.

## What Do The 2026-10-07 Filter, Terminal And Table Checks Establish?

Evidence root: `/Users/yuna/.local/share/podlord-comparison/release-evidence`.
The active migration branch remains `feat/session-tabs-detachable-windows`;
no additional migration branch is created. Signing/notarization remain deferred
for the private artifact, not substitutes for functional or performance gates.

| Behavior | Public boundary and runnable check | Executed evidence | Remaining gap |
| --- | --- | --- | --- |
| Actual reference saved-filter import | `scripts/test-filter-migration.sh BUILD FRESH_OUT`, compiling the actual C# `FilterPresetStore.Save`, importing through `Workspace.importFilterPresets` and selecting through QML ComboBox keys | 14/14 Basic/Fusion scenarios; byte-for-byte source preservation; 12 native headless screenshots in `2026-10-07-real-reference-filter-import` | Native picker, installed-process restart, display Limit and paired desktop presentation are not proved |
| Existing field/quantity/search/state behavior | Actual QML/public-store filter regression scenarios, isolated profiles and an explicit external Kubernetes HTTP boundary | 501/501, randomized 12-worker execution, `20261007-reference-filter-regressions.log/xml` | Not every menu/platform/theme and not installed-process latency |
| Retained terminal history, trim, grow, clear, Follow and selection | Actual QML terminal, external HTTP/WebSocket boundary; seven named scenarios per style | Four genuine output/trim anchor failures reproduced first; two grow-test setup failures corrected; all 146 Terminal scenarios pass in `20261007-terminal-history-after.log/xml` | Device input/accessibility and complete paired visual matrix remain independent |
| Real interactive container execution | `scripts/test-native-visual-kubernetes.sh native-terminal-e2e` on Cocoa/Metal and an owned local K3S cluster, populated with 1,051 objects | Shell, vi/read-back, interrupt and touch interrupt all pass; four current captures/logs in `2026-10-07-current-metal-terminal`; owned cluster/profile removed | Not a C#/C++ visual comparison or a mobile release |
| Cached session selection, Secret isolation, table find and filters | Actual Workspace/QML controls after removing whole-model scope resets | 230/230, 12 randomized workers, `20261007-incremental-session.log/xml`; selection explicitly cleared across contexts | Current tab latency and RSS still fail below; hosted verification remains independent |
| YAML preview default focus | Real dialog and keyboard Escape on Linux Qt 6.10, not a mocked widget | Focus trace reproduced `Cancel -> Apply`; selecting the footer's actual initial item then passed ten consecutive runs in `2026-10-07-linux-values/preview-focus-after.log/xml` | Broader current-source regression results must be recorded separately |
| One dialog decision per click | Real Cancel button and public Dialog `rejected` signal | `cancel-once-before.log` reproduced two rejections for one click; Qt's redundant footer forwarding removed | Fresh registered Basic/Fusion and lifecycle runs required before closure |

### What Do Current Performance And Coverage Measurements Say?

The complete source-`3ef5640` instrumented macOS run passed 3,749 functional
registrations, with 96.92% line coverage and 82.64% branch coverage. The 90%
branch gate still fails. Hosted Linux ARM passed 3,515/3,517 registrations and
AMD64 3,514/3,517; clipboard/focus failures remain actual failures, with branch
coverage 82.69%/82.66%. Hosted macOS arm64 reported 82.67% branches. Intel hit the
60-minute job limit before completion; it is not a compatibility pass.

Current unprofiled Cocoa/Metal, 5,000-resource results are retained separately:

| Protocol | Before incremental scope publication | After incremental scope publication | Gate |
| --- | --- | --- | --- |
| Cached tab p95 / maximum | 93.20 / 97.49 ms | 89.33 / 111.81 ms | p95 <= 50 ms, maximum <= 100 ms: fails |
| Cache filter p95 | 33.33 ms | 26.76 ms | <= 100 ms: passes |
| Sort p95 | 48.26 ms | 37.11 ms | <= 100 ms: passes |
| Cached inspector p95 | 31.58 ms | 31.68 ms | <= 50 ms: passes |
| Maximum RSS | 261,390,336 bytes | 263,569,408 bytes | <= 250,000,000 bytes: fails |
| Warm resident growth | 2,539,520 bytes | 1,523,712 bytes | <= 5,000,000 bytes: passes |
| Foreground nonsync idle | Focus lost after 11.22 s | Focus lost after 7.20 s | Required uninterrupted 60 s: neither run is valid |

Logs: `20261007-current-metal-performance.log` and
`20261007-incremental-session-metal-performance.log`. Different cold runs are not
proof of a statistically established improvement. No latency, memory or coverage
threshold has been weakened.

### Are Thousands Of Registrations Thousands Of Independent Behaviors?

The inventory before the two new dialog-cancellation registrations contained
3,763 CTest entries, 2,871 distinct command/non-style environment configurations,
and 892 additional style configurations. Of these, 889 were explicitly labeled
`style-variant`; no identical command/environment registration was found.
These are execution configurations, not a claim of 3,763 distinct product
behaviors, complete E2E coverage or absence of semantic overlap.

The style variants use actual UI-capable drivers, not duplicated QCore-only
parsing runs. Broad E2E success does not replace their malformed-input, auth,
conflict, retry or lifecycle cases. Removing those tests merely to reduce the
count would discard negative-path evidence. A case can be deleted when the same
observable behavior and failure boundary are demonstrably covered elsewhere.
No unrelated scenarios are merged into a giant test method.

Native tests already run independently in parallel: four workers in hosted CI,
twelve on this local 18-core machine; owned profiles, ports and servers isolate
them. Foreground desktop/CPU measurements cannot run concurrently with other UI
measurements. The C# App/Layout suites retain their documented shared UI-runtime
isolation. Seven local C# Release performance checks passed; CI now requests
Release configuration rather than timing unoptimized Debug code. Hosted Linux
Release outcomes are still required, and the existing budgets remain unchanged.

### Did The Focus Correction Survive Repeated Execution?

The first footer-index correction passed Linux but the 760-case macOS lifecycle
run exposed a remaining visual-focus failure: Cancel was active with a non-keyboard
focus reason. That run passed 759/760 and is retained as a failure, not rounded up.
The final correction clears the footer ListView's initial current item before
opening, then assigns keyboard focus once. Both macOS and Linux passed ten
repetitions of preview/Escape and Basic/Fusion single-cancel decisions: 30/30 on
each platform (`20261007-preview-neutral-current.log/xml` and
`2026-10-07-linux-values/preview-neutral.log/xml`).

The broader Linux focus/copy run also passed all six cases twenty times, 120/120
(`2026-10-07-linux-values/focus-final.log/xml`). The full current-source macOS
lifecycle/coverage run remains required rather than borrowing an earlier binary's
result. Temporary focus-transition tracing was removed; failure diagnostics and
the named single-cancellation regression remain.

The finalized reference-export runner again passed 14/14 with source-byte
comparison, disabled SDK telemetry/development-certificate setup and owned
profile cleanup. `2026-10-07-final-reference-filter-import` retains its original
reference export and twelve native captures. No additional checksum utility or
project dependency is needed by that runner.

### Does A Larger Bounded Test Pool Preserve Behavior?

The finalized Terminal suite passed 146/146 twice with identical registered cases,
first at four processes (130.83 s), then at six (76.08 s).
`20261007-terminal-jobs-4.log/xml` and `20261007-terminal-jobs-6.log/xml` retain the
runs. macOS CI now uses six processes, as Linux already does; the prior Intel job
completed 3,701 of its 3,747 non-preflight registrations before cancellation at
the job limit. No missing tail, package check or hosted six-worker result is
counted as passed. Cold/warm ordering also prevents treating the local difference
as a precise cross-platform speedup guarantee.

### Which checks cover the final incremental table notifications?

The unprofiled macOS arm64 build passed all 491 selected public UI/filter regressions after separating display-role notifications from row-metadata invalidation. The selection covered field, metric, resource and age filters, table find, session/cluster switches, inspector values and Secrets, column tools, YAML preview and single-decision Cancel. Execution used twelve isolated processes and randomized order; elapsed time was 110.88 seconds. Evidence: `20261007-row-notification-regressions.log` and its JUnit XML. This is not a complete-suite or coverage result.

The subsequent Cocoa/Metal measurement with 5,000 resources retained the existing gates. Filter p95 was 45.59 ms and sort p95 38.64 ms. Cached inspector p95 was 46.32 ms, maximum 65.27 ms. Cached session switching still failed: p95 78.37 ms and maximum 111.90 ms against 50/100 ms. Maximum resident memory was 259,637,248 bytes against 250,000,000; steady-state growth was 1,916,928 bytes and passed its 5,000,000-byte limit. The foreground window lost activation after 3.012 seconds, so the required uninterrupted 60-second idle CPU proof remains invalid. Evidence: `2026-10-07-row-notification-metal-performance` and `20261007-row-notification-metal-performance.log`. These measurements do not establish statistical improvements or release readiness.

### What Did The 2026-10-07 Shared-Table And Scheduler Checks Establish?

| Behavior / scenario | Public entrypoint | Test evidence | Result / remaining gap |
| --- | --- | --- | --- |
| Copy/Menu/Shift+F10 when Qt focuses a pinned table rather than a cell | Visible native grid and actual keyboard | `native.ui.columns_pin_namespace_table_copy`, `columns_pin_namespace_table_menu`, `columns_pin_namespace_table_f10`, Basic/Fusion | macOS 8/8 selected cases passed; Linux 9 selected cases repeated 20 times, 180/180 passed. Focus uses the shared current logical index. |
| Shared table/filter/Values/YAML behavior after keyboard ownership correction | Public Qt controls and external HTTP boundary | `20261007-shared-key-regressions.xml` | macOS 508/508 passed, 104.75 seconds. This is scoped regression evidence, not a full latest-source suite. |
| Concurrent pod/node metric requests | Real C# resource-service ingress and held external HTTP responses | `Resource_service_fetches_pod_and_node_metrics_in_parallel` | Deterministic rendezvous failed before the shared queue-worker correction and passed after it. No elapsed-delay assumption or internal fake is used. |
| Six bounded request workers and a held twelve-request backlog | Public list requests and request telemetry | `FakeKubernetesBehaviorTests` held-backlog regression | Held external responses prove queued work before release; cleanup drains all started requests. The selected Kubernetes project run passed 59/59 non-k3d tests with coverage. Real-cluster/hosted confirmation remains required. |
| Whole registered native suite at commit `842e01b` | Actual CLI and Qt test executables with isolated profiles | `20261007-current-full-suite.log`, fresh LLVM merge | 3765/3765 passed; lines 97.30% passed, branches 82.86% failed the 90% gate. Later changes are not covered by this historical full-suite claim. |
| Registration inventory | CTest command/environment/work-directory configurations | `20261007-current-test-inventory.json` | 3765 registrations represented 2872 non-style configurations and 890 explicitly labeled style variants. No identical full registration was found; this does not prove all scenarios are semantically unique or that legacy assertions can remain. |
| Foreground idle CPU with continued normal sync | Real Cocoa/Metal native performance window | `2026-10-07-runtime-profile/measurements.jsonl` | Valid 60,069 ms foreground interval, 1.183% of one core, foreground not lost; passed the 2% gate. Action timings sampled by a profiler are diagnostic only. |
| Memory in that process | OS RSS sampler, `vmmap`, `heap` | `2026-10-07-runtime-profile/` | RSS maximum 259,735,552 bytes failed the 250 MB gate; idle growth 3,145,728 bytes passed the 5 MB gate. OS physical footprint was separately 542 MiB and includes graphics allocations; it is not interchangeable with RSS. Root-cause/performance and installed-package gates remain open. |

The existing macOS arm64/Intel and Linux amd64/arm64 CI jobs provide real
architecture execution rather than renamed copies of one host's test result.
The current hosted run is not green. Keep independent tests concurrent where
profiles and external servers are isolated; keep foreground performance and
interactive screenshot runs exclusive. Do not merge unrelated scenarios merely
to reduce the registration count, and do not count a component or offscreen test
as an installed application, device, real-cluster or paired-image proof.

### What Does The Shared Alarm Table Verify?

The alarm rule table now uses the same native model, selection, Find, copy/context menu, three-state sorting and column editor as the other resource tables. Layout version 7 adds `alert`; reading older versions supplies defaults without rewriting the file, and saving preserves unrelated layouts. The canonical rule UUID owns editor selection, including sorting, duplicated/deleted rows and pinned columns. Match ticks update only the Active column, not rule metadata or sound labels.

- macOS: 817 shared alarm/table/filter/value/YAML regression registrations passed in 128.38 seconds with 12 workers (`20261007-shared-alarm-grid-final.log`).
- Linux: 317 alarm/layout/pinned-column registrations passed in 67.81 seconds with four workers (`2026-10-07-linux-values/shared-alarm-grid.log`). The isolated verification container removed itself; the existing latest verification image was reused.
- Ten keyboard/Tab/duplicate/delete registrations each passed 20 consecutive executions, 200 executions in 19.08 seconds (`20261007-alarm-helper-reuse.log`). The tests reuse the existing visible-item lookup rather than selecting hidden recycled delegates.
- New public checks cover valid/invalid Find; hide/show/pin/order/resize/cancel/defaults/last-visible/invalid-width/restart of alarm columns; native keyboard entry; and old-layout upgrade/read/save/failure retention. Basic/Fusion executions are style variants, not additional distinct product scenarios.
- `20261007-native-alert-table-current.png` records an actual Cocoa-rendered native window with the three real built-in definitions and a private empty profile. It verifies the current On header and shared table layout. It is not a populated Kubernetes screenshot, a C#/C++ pair, or evidence for the full view/theme matrix.

The previous whole-suite coverage figures still belong to the earlier saved revision. These scoped passes do not establish the current full-suite coverage gates, filter RowLimit parity, multiwindow, localization, the complete paired image matrix, or the failing memory/cached-tab budgets.

### What Does The Native Language Increment Establish?

The compiled UI catalog is exported through `PodlordLocalizer.Text` and its actual supported-language entrypoint, not through guessed translations or reflection. `sh scripts/export-native-locales.sh --check` regenerates it in an isolated temporary .NET 10 build and compares every byte. This check runs in reference CI. .NET is a development/comparison tool only; the native application loads its own compiled resource.

The source inventory has twenty translated languages plus English and `system`, 22 selector choices. `native.language.catalog` compares the compiled public text map and options with the exported data. System-region, Brazilian Portuguese, unsupported Portuguese regional fallback, Chinese and unsupported-language checks retain the reference resolution contract. Public settings cases cover read-only version-6 defaults, explicit version-7 upgrade, missing/type/empty/unknown/case-invalid language and fractional version input, retaining malformed bytes on both load and save.

Actual UI cases exercise every explicit language through the real settings selector in Basic and Fusion. Separate cases cover unchanged selection, rejected input, locked/conflicting saves, reopening the profile, preserving language when saving read policy, and the narrow drawer. `native.ui.language_cache_{de,ar}_{Basic,Fusion}` changes language with a populated cached session, retains every displayed resource value and session identity, and toggles the translated Problems control without any further Kubernetes request. Those four cases and the two sound-selection variants passed 20 repetitions each on macOS, 120 executions in 71.16 seconds.

- The earlier 523-registration pass had two narrow-drawer lookup failures; opening the real drawer fixed the test's hidden-control assumption. The subsequent complete selected run passed 524/524 in 35.98 seconds (`20261007-language-compact.log`). The new cache cases and latest shared pointer-boundary change have separate evidence, not an implied whole-suite pass.
- Cocoa captures `20261007-native-language-de.png` and `20261007-native-language-ar.png` show actual desktop language changes with an empty private profile. They are native-only images, not C#/C++ pairs. They predate the final mirrored-checkbox position and theme-label bindings; current captures must be used for their final visual assessment.
- The complete instrumented e65ad92 binaries ran two successful preflights plus 3,800 registrations: 3,796 passed and four session-view restoration cases failed. The failure exposed alarm layout IDs leaking into session-view schemas; `layoutSchemas` now keeps that global layout extension separate. The unchanged restoration cases pass in the scoped corrected run. That older whole run measured line coverage 97.33% and branch coverage 82.95%; the 90% branch gate remains failed.

Navigation, sidebar modes, settings section/appearance labels and resource empty/loading chrome are migrated. Remaining inspector/help/action text, the complete per-view/theme C#/C++ matrix, independent windows, current memory/cached-tab budgets and fresh whole-suite coverage remain open. A passing language selector is not a whole-product translation or release verdict.

### What Does The Final Language And Pointer-Boundary Check Prove?

- The current macOS scoped suite passes 1,019 registrations in 138.38 seconds with 12 workers: 719 behavior registrations, 295 style variants and five older unlabelled registrations. These are not 1,019 distinct product features. Evidence: `20261007-pointer-boundary-suite.log` and its JUnit report.
- The current Linux ARM64 lane passes 404 registrations in 74.49 seconds with four workers. Both sound-selection style variants additionally pass 20 consecutive executions each. The shared real-input helper now rechecks ancestor clipping after the rendered scroll frame, before sending a pointer action; no product delay or test retry hides the original offscreen click. Evidence: `2026-10-07-linux-values/scroll-boundary-suite.log` and `scroll-boundary-repeat.log`.
- Actual Cocoa captures `20261007-native-language-de-final.png` and `20261007-native-language-ar-final.png` include the final appearance labels and RTL checkbox correction. They demonstrate those native surfaces only, not C#/C++ image parity or complete translation coverage.
- The current uninstrumented foreground performance protocol fails: idle CPU 3.125% of one core (limit 2%), maximum RSS 280,641,536 bytes (limit 250,000,000), and cached-session-tab p95 69.23 ms (limit 50 ms). Warm RSS growth is 1,507,328 bytes and cached-inspector p95 is 32.78 ms, both within their limits. Foreground was retained for 60,683 ms of nonsynchronizing idle. Evidence: `2026-10-07-current-language-performance/measurements.jsonl`.
- The previous complete instrumented run used the earlier alarm-table revision, not this language increment. Its four session-view restoration failures are corrected and covered by the current scoped suites. Its 97.33% line / 82.95% branch coverage does not establish the current revision's coverage gate; branch coverage remains below 90%.
- Independent windows, deeper translated actions and help, the complete same-population reference/native image matrix, installed-artifact validation, platform lanes and performance/coverage gates remain release blockers. The deliberate uncapped native table behavior is specified by ADR 0031, not a missing row-limit feature.

### How Is One Cached Table Snapshot Published?

`native.filter.source_update_batch` drives the public resource model and its real filter proxy. A changed display value and problem metadata must arrive in one complete `dataChanged` notification, with the filtered result updated. The unchanged implementation failed first with two overlapping notifications; the shared publisher now emits the combined roles and affected range once. Existing row insertion/removal and cluster-only updates retain their distinct notifications.

The subsequent macOS filter, quantity, field, cached-language, cluster-switch and restored-view scope passes 496 registrations in 102.06 seconds with 12 workers (294 behavior registrations and 202 style variants). Evidence: `20261007-snapshot-batch-failing-first.log`, `20261007-snapshot-batch-fixed.log` and `20261007-snapshot-batch-suite.xml`. This removes redundant model work; the separate foreground performance protocol still determines whether the interaction budget is met.

The Linux ARM64 snapshot-update scope also passes 496 registrations in 279.39 seconds with four workers. The resource-model notification test remains a single behavior case, not a new style matrix. Radar sorting now compares references rather than copying seven identity strings for every comparator invocation; the deterministic reference, replay/recreation, keyboard, water and highlight scope passes 124 registrations in 62.66 seconds on macOS. Evidence: `2026-10-07-linux-values/snapshot-batch-suite.xml` and `20261007-radar-sort-suite.xml`.

The post-batching foreground protocol still fails the tab p95 budget (87.14 ms) and RSS budget (260,882,432 bytes; warm growth 2,064,384 bytes passes). Its idle interval lost foreground after 5,170 ms and cannot establish an idle CPU pass. The earlier valid idle failure remains open. Diagnostic sample/heap runs are not substituted for uninstrumented performance gates.

The current private macOS ARM64 package preflight passes dependency paths and size limits: ZIP 31,713,726 bytes, installed regular files 85,015,099 bytes. Qt supplier metadata/notices are included and local ad-hoc verification passes; Developer ID and notarization remain deliberately deferred. Evidence: `2026-10-07-current-native-package/package-evidence.txt`. This packaging result alone does not prove clean-device startup or functional/image parity.

On an owned real local Kubernetes cluster, the current field-filter driver passes exact text, AND/OR, seven CPU and seven memory observations, missing storage, Ready 2/2, eight observed restart counts and 128 filtered Radar resources. Its 20 retained public-input/software-render frames have p95 9.56 ms and max 11.43 ms over 1,836 cached records. Evidence: `20261007-desktop-direct-field-driver.log`. The separate bare development application reported a connection/TLS failure against that cluster; packaged-application behavior must be checked rather than treating the successful driver as proof of a working application.

### What Does The Table And Inspector Language Pass Prove?

- The exported reference catalog remains the sole translation source. Table context actions, column-layout Save, table-search Close/accessibility labels, inspector tabs and guidance links, value-copy accessibility labels, port actions and command-palette Close now use that catalog. This does not claim translations for native-only controls absent from the reference catalog.
- `native.language.table_tools_{en,de,ar}_{Basic,Fusion}` opens the actual column dialog and saves its layout through the public control. The original German run failed with `Table layout save action is not localized.`; the corrected cases pass.
- Existing `native.ui.language_cache_{de,ar}_{Basic,Fusion}` now opens real detail before selecting the language, verifies the rendered inspector labels, retains every displayed Kubernetes value and confirms no translation/filter-driven HTTP requests. The original inspector run failed at `overviewButton` before the QML correction. No separate inspector-language harness was added.
- Filter-value checkbox indicators and content padding respect the control's mirrored direction. Kubernetes identifiers, field expressions and transport container names remain untouched.
- macOS table/column/copy/context/language scope: 604/604 pass, 86.08 seconds with twelve workers. Inspector/language/navigation/terminal/guidance scope after the tab-label correction: 423/423 pass, 103.25 seconds, comprising 240 behavior and 183 style-variant executions.
- Linux language/restoration/discard-focus scope: 114 cases each pass three consecutive executions, 39.63 seconds with four workers. The owned verification container is removed; the latest reusable verification image remains.

### How Are Test Counts And Coverage Objects Kept Honest?

- Current CTest inventory: 3,884 executions, 2,945 labeled `behavior` and 939 labeled `style-variant`; no unlabeled cases and no global `RUN_SERIAL`/`RESOURCE_LOCK`. These are registered executions, not 3,884 distinct product features. Style variants exercise the same contract on different controls implementations.
- Five alarm-layout migration cases now have behavior labels. They remain necessary because current-session E2E workflows cannot prove loading or rejecting old stored schemas.
- Restoration tests used to inject `version: 6` into freshly saved version-7 settings while retaining the new language field. This invalid external document tested the wrong failure path. The tests now preserve the actual version written by the public store and modify only the targeted restoration preference. Direct public calls replace a reflective forwarding helper.
- The existing discard-keyboard regression waits for the exact safe-button active/visual-focus condition before sending Return, keypad Enter or Space; it no longer assumes `visible` means the popup has completed its opening transition. No delay, test retry or production focus workaround was added.
- macOS restoration/discard scope: 34 cases each pass five consecutive executions, 20.01 seconds. The original disabled-restoration case failed before the fixture correction.
- LLVM coverage executable paths are generated from CMake's configured executable targets rather than a manually duplicated shell list. Missing configured binaries fail explicitly. Coverage thresholds remain 95% lines and 90% branches.
- First full run of the metadata/table-action production changes: two preflight cases pass, then 3,874/3,882 cases pass; the eight failures are the subsequently corrected restoration fixtures. Coverage: 97.35% lines, 82.84% branches. This is not a green full-suite result and predates the final inspector labels/test corrections.
- Development feedback uses the affected public-boundary scopes above, not a full multi-platform suite for each caption. Complete runs remain mandatory release evidence. Existing external-boundary validation, auth, retry, malformed-response and migration cases are not deleted merely because a successful Kubernetes E2E exists.

### What Does The Cached Metadata And Real Terminal Evidence Prove?

- `native.filter.metadata_benchmark` requests seven public table metadata roles across 5,000 cached rows, five warmups and thirty measured samples, checking an identical nonzero checksum across all display columns. Metadata roles now return before unrelated cell/string/date preparation; no extra cache or state is added.
- Unprofiled macOS metadata benchmark p95: 4.091375 ms before, 2.192708 ms after; p50: 2.949458 versus 2.0895 ms. These are model-boundary timings, not proof that the tab, idle CPU or RSS budgets pass. Related radar/filter/sort scope: 239/239 pass, 77.94 seconds.
- Current `native-terminal-e2e` succeeds through the real locally owned Kubernetes exec boundary for shell input, vi write/read-back, keyboard interrupt and touch interrupt. Rendered QML frames are preserved for each case. This run uses the offscreen/software test driver, not an installed-package or C#/C++ picture-parity assertion. Its cluster, volumes and private profiles are removed.
- The packaged desktop comparison did load 1,833 real cached resources. Its native screenshot is retained, but the C# reference displayed a different overall cache population and the populated native macOS accessibility export lacked child controls. Keyboard Find and row inspection worked. Exact full-population picture parity and installed accessibility remain open; the comparison timeout cleaned up its owned apps/cluster/profile.
- Release blockers remain independent windows, the complete paired view/theme matrix, installed accessibility, native-only localization completeness, measured foreground CPU/RSS/tab budgets, current full coverage at 90% branches and passing hosted OS/architecture gates. The hosted reference C# clear-filter performance test also exceeds its existing budget; it has not been weakened or silently skipped.

## What Does The Final Table And Language Baseline Prove?

The 2026-10-07 `551d3b0` native baseline passes all 3,884 registered cases:
2 preflight checks and 3,882 remaining checks, the latter in 554.54 seconds with
12 workers. The inventory distinguishes 2,945 behavior cases from 939 style
variants; a variant is not an additional independent product capability.
Production coverage is 97.35% lines and 82.87% branches. The 95% line gate
passes; the 90% branch gate fails. Passing executable cases is not a passing
release gate.

The fresh private arm64 macOS package passes size, dependency and local ad-hoc
signature checks: 31,713,929 archive bytes and 85,015,099 installed-file bytes.
Executable SHA-256 is
`4e3e824de852adef1441612c477c326d4d2d8762a02edd2480359d94b7c131b0`;
archive SHA-256 is
`12074c5ae3ee5ef7b6cd9320fe5f03eb1a72748549bd78560d7853d210ad5cf0`.
These are packaging checks, not complete installed-device verification.

The subsequent desktop comparison produced no valid paired image. The Mac was
reported locked and the C# reference aborted while starting its render timer.
The owned comparison applications, Kubernetes container and volumes were
cleaned up. No security or lock-screen setting was changed. Existing unpaired
images and software-rendered terminal frames are not relabeled as picture parity.

## What Proves Shared Native Read Ownership Across Views?

The public `ResourceClient` entrypoints now accept a view identity for visibility,
focus/activity and log controls. `moveSession` transfers a binding without
closing the session. The empty identity retains the existing single-window
entrypoints. No new store, cache, framework or polling timer is introduced.

| Behavior | Public entrypoint | Test | Remaining boundary |
| --- | --- | --- | --- |
| Both visible sessions admit inspector reads | `showSession`, `inspect`, `document` | `native.read_views.inspect` | Actual independent windows |
| Both visible log targets retain all container streams | `showLogs`, `logEntries` | `native.read_views.logs` | Installed simultaneous log panes |
| Pausing one log view leaves the other queue eligible | `pauseLogs`, `selectLogContainer` | `native.read_views.pause` | Installed controls |
| Hiding one log view leaves the other queue eligible | `hideLogs`, `selectLogContainer` | `native.read_views.hide` | Window visibility wiring |
| Closing one session preserves the other visible logs and retained cache | `close`, `logEntries`, `rows` | `native.read_views.close` | Window closure wiring |
| A move preserves cached rows/logs and transfers log control | `moveSession`, `pauseLogs`, `inspect` | `native.read_views.move` | Window coordinator, drafts and selection |
| Another view cannot steal the same session | `showSession`, `inspect` | `native.read_views.conflict` | Focus the existing owner in the UI |
| Moving into an occupied view retains both originals | `moveSession`, `inspect` | `native.read_views.move_occupied` | Actual window placement |
| Moving a missing binding fails without affecting visible sessions | `moveSession`, `inspect` | `native.read_views.move_missing` | Actual window placement |
| Authentication failure does not retry or suspend unrelated credentials | `refresh`, `authenticationRequired`, `inspect` | `native.read_views.auth` | Explicit login controls |
| Visible views retain independent focus cadence | `setFocused`, `userActivity`, `synchronize` | `native.read_views.focus` | Actual per-window focus events |
| Visible views share configured request start spacing | `configure`, `requestStarted`, `showLogs` | `native.read_views.limit` | Process-wide application injection |
| Moving retains the same forward descriptors and bound local TCP ports | `startPortForward`, `moveSession`, `portForwards` | `native.read_views.forward_move` | Real post-move WebSocket traffic |
| Closing the moved session releases only its own local ports | `close`, `portForwards`, local TCP bind | `native.read_views.forward_close` | Actual detached-window closure |

Kubernetes alone is simulated by local HTTP servers; requests, queues, cache,
logs, timers and TCP listeners are real production code. The focus case controls
the external wall clock so it does not wait four minutes. Each registered case
runs in an independent process with private loopback ports and RAII cleanup;
there is no shared test profile or required ordering. These are controller-boundary
cases, not UI E2E or complete window parity. Existing read-overlap cases remain
because concurrency, coalescing, late responses, auth and rate-limit behavior are
not replaced by successful two-view cases.

On Linux arm64, all 14 new cases plus 7 existing read-overlap cases pass with
four workers in 53.43 seconds. The verification container is removed on exit;
only the existing latest verification image is retained. Local evidence is
`2026-10-07-linux-values/read-views-current.log` and its JUnit report.

The completed current macOS arm64 run passes all 3,898 registered cases in
574.75 seconds with 12 workers: 2,959 behavior cases and 939 style variants.
It rebuilds all executables, collects isolated raw profiles, merges them and
reports all production `.cpp`/`.h` sources using the generated executable
inventory. Coverage is 97.37% lines and 82.95% branches; the unchanged 90%
branch gate still fails. Evidence is `20261007-multiview-full-native.log`, its
JUnit report and `20261007-multiview-coverage.txt`. The temporary raw-profile
directory is removed on either success or failure; the unrelated existing
`default.profraw` is not read, rewritten, staged or removed.

The code review keeps one view-binding map instead of the previous singleton
session/log/focus fields. Read, write, delete, forward and terminal admission
use the same visibility predicate. No second cache, polling loop, settings file,
compatibility facade or test harness is added. The existing HTTP boundary is
reused. Cases are not collapsed merely because an E2E success path also executes
some of their code: malformed input, cancellation, concurrency, authentication,
late replies and migration remain independently diagnosable behavior targets.
The new view cases run concurrently on both operating systems.

Hosted run `37668747829` belongs to baseline `551d3b0`, not this later view
increment. Its completed C# lane passes 458 cases (91 Core, 84 Kubernetes,
69 Layout and 214 App), then fails coverage at 95.71% lines / 85.40% branches.
The completed macOS arm64 native lane reports 97.35% / 82.87%; Linux arm64
reports 97.35% / 82.94%. Their branch gates fail. The earlier C# timing failure
is historical evidence, not the failure in this run. Other lanes were still
running when inspected; the workflow is not green. Local full-suite success
does not supersede those hosted failures or establish installed visual,
accessibility, foreground CPU/RSS, tab-latency or complete device gates.

## How Does The Native Shell Remove Permanent Tool Clutter?

The 2026-10-07 shell cleanup follows SHL-008 through SHL-010 and ADR 0033.
The visible C# source keeps search behind a toggle; native now does the same
without changing the existing cached query owners. Closing search preserves
its query, with an active-query indicator on the toggle. Routine sync is still
automatic. Refresh remains an explicit action inside the workspace menu;
authentication and real recovery controls do not depend on opening search.

| Behavior | Public entrypoint | Registered evidence | Remaining gap |
| --- | --- | --- | --- |
| Reveal, focus, hide, reopen and Escape retain cached resource results without requests | Actual Main.qml window, pointer and keyboard | `native.ui.shell_search_demand_Basic/Fusion` | Physical keyboard/assistive technology |
| Resource and Event searches retain independent queries across navigation | Actual navigation and search fields; external Kubernetes HTTP boundary | `native.ui.shell_search_events_Basic/Fusion` | Full legacy/native picture comparison |
| Long session names cannot grow or wrap the footer at 320x360, 390x720 and 1440x920 | Public rename operation and actual window geometry | `native.ui.shell_footer_narrow_Basic/Fusion` | Device text scaling and native mobile runtime |
| Refresh and Commands stay reachable in the workspace menu, not a permanent text toolbar | Actual pointer/menu/Escape interactions | `native.ui.shell_tools_menu_Basic/Fusion` | Installed platform menu matrix |
| Seven primary actions fit a 320-pixel window with 44-pixel touch targets; Radar controls stay reachable in the drawer | Actual rendered buttons and pointer/scroll behavior | `native.ui.shell_touch_Basic/Fusion` | Physical touch, safe areas and on-screen keyboard |
| Filter reset is an icon-sized control and restores cached rows without requests | Actual Reset button | `native.ui.shell_reset_icon_Basic/Fusion` | Populated legacy/native all-theme matrix |

The focused run passes 19/19 checks in 7.92 seconds: six new behavior scenarios
in Basic/Fusion, six existing HUD layout checks and the compact-toolbar check.
This is ten behavior targets and nine style variants, not nineteen independent
product capabilities. Current logs, JUnit and real Qt renderings are under
`release-evidence/2026-10-07-shell-cleanup` in the local comparison workspace.
The new width/height regression exposed narrow-window navigation wrapping in
Fusion: style-specific toolbar insets still applied despite a generic zero
padding. Explicit edge insets and whole-pixel HUD widths avoid the additional
row; both styles retain a visible table row at the minimum window size.

Table Find/Columns, match navigation, session close/rename, mute, field filters
and camera/filter reset reuse the existing native glyph renderer through one
small icon-button component. No font-symbol dependency, new timer, request loop,
profile schema or second camera/filter state is added. Filters scroll below the
same retained Radar. Saved-filter reload is an explicit menu action rather than
a persistent Reload button. Public UI tests reveal the actual menus/search
before operating them; they do not bypass those interactions with private calls.

The Mac is still reported locked by desktop control. These offscreen/software
Qt images are not current C#/C++ desktop comparison, physical mobile support,
Metal/RSS/CPU acceptance or a completed release gate. The prior full-suite and
coverage evidence remains separate; this bounded UI run does not replace it.

The final related UI run passes 728/728 checks in 169.35 seconds with twelve
workers: 424 behavior targets and 304 style variants. `final.log` and `final.xml`
retain the current result. The selected set covers shell/HUD, resource and Event
queries, field filters and saved presets, table Find, source/session controls,
commands, authentication, logs, forwards, language-cache behavior and the related
radar/refresh/edit paths. It is not a new full-suite or coverage run.

Earlier failures remain in `related.log` and `before-anchor-ready.log/xml`.
Global-filter tests now reveal search through the actual toggle. The rendered
filter comparison opens search before taking its baseline, so a toolbar shift
cannot masquerade as changed resource output. The scroll regression uses a real
wheel event over the filter viewport instead of calling the former nested list's
positioning method. An anchored field popup closes when its actual viewport
moves. Input waits for the public popup `opened` state and a rendered frame;
that corrected case also passes fifteen consecutive repetitions in 31.02 seconds
(`anchor-ready.log/xml`). These repetitions are one behavior, not fifteen cases.
Port search now also exercises Escape/reopen with its retained cached query.

A fresh private arm64 release package passes dependency-path, ad-hoc integrity,
license-metadata and size checks. Evidence is in
`release-evidence/2026-10-07-shell-cleanup-package` and
`20261007-shell-cleanup-package.log`. The ZIP is 31,721,822 bytes and its installed
regular-file logical sum is 85,048,715 bytes, within the existing 50 MB/100 MB
limits. Executable SHA-256 is
`675db2c5b54fae6fece096d9c08469f01517e7981603e9cca53a51fafc30f735`;
ZIP SHA-256 is
`5ccd09c670547b92400ce9138df780828ebd3c3630c89addce8e7198dccde710`.
This does not establish installed desktop startup, current C#/C++ visual parity,
mobile release support, performance gates or notarization. No Kubernetes stack,
Docker image or container was created in this increment. Test profiles and the
package builder's temporary tree are owned and cleaned by their existing
lifecycles; the unrelated `default.profraw` remains untouched.

## Does initial health stay filled, and can files/sessions open quickly?

Requirements: LOAD-002 and SHL-011; decision: ADR-0033.

| Scenario | Public boundary | Executable evidence | Remaining gap |
| --- | --- | --- | --- |
| Passive sync, explicit rediscovery, authentication rejection and fresh cached reopen retain completed health | ResourceClient with a local external Kubernetes HTTP server | native.read_overlap.progress_auto, progress_refresh, progress_auth, progress_reopen | Actual-device rendering and slow-cluster visual matrix |
| Rendered health strip stays full throughout explicit refresh | Real QML window/menu and external HTTP boundary | native.ui.shell_health_refresh_{Basic,Fusion} | Installed desktop/theme matrix |
| Extensionless file and local file URL import by content without opening/authenticating a session | Workspace public quickImportFile entrypoint and real private store | native.ui.quick_file_extensionless_{Basic,Fusion}, quick_file_url_{Basic,Fusion} | Native OS file-picker acceptance on unlocked desktop/device |
| Malformed/missing input does nothing; owned-profile corruption remains visible | Same public entrypoint and actual files/catalog | native.ui.quick_file_invalid_{Basic,Fusion}, quick_file_missing_{Basic,Fusion}, quick_file_profile_{Basic,Fusion} | Real-device file providers |
| Invalid quick-open preserves existing context, session, filter and notices | Real populated QML workspace and private store | native.ui.shell_quick_invalid_existing_{Basic,Fusion} | None for the exercised local input path |
| Dropdown activates current/closed sessions and contexts without unnecessary reads | Actual menu clicks, session/cache owners and external HTTP request counts | native.ui.shell_quick_sessions_{Basic,Fusion}, shell_quick_reopen_{Basic,Fusion}, shell_quick_context_{Basic,Fusion} | Detached app-window UI, rather than client view-binding tests |
| Anchored dropdown and its entries fit a 320-pixel window with 44-pixel targets | Actual rendered QML window and pointer input | native.ui.shell_quick_narrow_{Basic,Fusion} | Physical mobile interaction and table layout |

Evidence: `release-evidence/2026-10-08-health-quick`. The failing-first run
(`before.log/xml`) reproduces three progress-reset failures and the missing
quick-open behavior; cached reopen already passed. Intermediate failures remain
in their logs. The final selected run (`accepted.log/xml`) passes 159/159 checks
in 40.78 seconds with twelve workers: 106 behavior targets and 53 style variants.
The increment adds 15 behavior targets and eleven additional style executions,
not 26 independent features. Related source import, source layout, view restore,
draft/tab/window guards, shell/HUD and shared-client view cases are included.
This is not a full-suite, coverage or performance-gate run.

`quick-open-phone-Basic.png` and `quick-open-phone-Fusion.png` are real Qt
software/offscreen captures using the external HTTP boundary, not screenshots
of an installed mobile app or a C#/C++ pair. The mobile table still needs a
readable identity-first presentation instead of only horizontal scrolling.
The desktop-control inventory reports a locked Mac. Device enumeration lists
only the host Mac; simulator setup requires local installer authorization.
No physical iOS/Android evidence or completed multiwindow UI is claimed.
No Kubernetes stack or container was created. Temporary test profiles clean up
through their existing owners; the unrelated `default.profraw` stays untouched.

A fresh private arm64 package passes dependency-path, local ad-hoc integrity,
notice-metadata and size preflight checks. Output:
`release-evidence/2026-10-08-health-quick-package`; log:
`20261008-health-quick-package.log`. ZIP size is 31,723,549 bytes; installed
regular-file logical sum is 85,048,843 bytes. Executable SHA-256:
`2ddd2252f865d91ff575145d3181a4eedde5856b7033de991f5b951c7b14657e`;
ZIP SHA-256:
`5a29d19bdf4120404c9737364b856700117dc9286934e0121e02f5af5bc97646`.
This does not establish installed startup, OS file-picker operation, physical
mobile support, full visual/terminal equivalence, performance or coverage gates.
Developer ID signing/notarization remains explicitly outside this private stage.

## What proves the compact landscape shell and reference footer on 2026-10-08?

| Behavior | Public boundary | Executable scenario | Remaining gap |
| --- | --- | --- | --- |
| Empty footer reports no snapshot or requests | Native QML application | `native.ui.shell_footer_empty_{Basic,Fusion}` | Installed empty-profile accessibility |
| Footer shows visible/cached counts, real request admission and second/minute/hour age | Real workspace and external HTTP boundary with controlled UTC clock | `native.ui.shell_footer_reference_{Basic,Fusion}` | Independent live telemetry comparison over long runs |
| Cached reopen retains accepted snapshot age and its session request history | Native QML and persisted sessions | `native.ui.shell_footer_reopen_{Basic,Fusion}` | Physical-device lifecycle |
| Left icon rail, docked Radar and collapsed filters at landscape breakpoints | Native QML, 600/640/800/896/1080 logical pixels | `native.ui.shell_landscape_{Basic,Fusion}` | Real touch input and safe-area/device hardware |
| Filter disclosure preserves effective Problems filtering without transport | Native QML controls and external HTTP request observation | `native.ui.shell_landscape_filters_{Basic,Fusion}` | Complete saved/field-filter visual matrix |
| Portrait/open drawer/landscape/desktop rotation retains one Radar, camera and query; no orphan modal drawer or requests | Native QML application | `native.ui.shell_landscape_rotate_{Basic,Fusion}` | Installed mobile orientation |
| Read/auth/validation/rate-limit failures remain visibly reported outside the footer | Existing native UI failure/lifecycle scenarios | `auth`, `redirect`, `malformed`, `invalid_discovery`, `invalid_list`, `repeated_page`, `rate_limit`, `forbidden`, `slow_close` | Complete release coverage/performance gates |

Evidence root: `/Users/yuna/.local/share/podlord-comparison/release-evidence`.
Layout renders in `2026-10-08-landscape-ui` are actual Qt software/offscreen
application frames against the external Kubernetes HTTP boundary, not mobile
screenshots. Six new behaviors are exercised in two control styles; style
variants are not counted as additional product features. The first attempted
build used a nonexistent test entrypoint name; the test was corrected to the
existing public filter entrypoint. An early run using the previously built test
binary is not accepted evidence.

Three disposable real Kubernetes stacks ran serially with private profiles and
owned containers/volumes. The desktop captures in `2026-10-08-landscape-desktop`
show the packaged native app and C# reference against the same second cluster,
using different Sirocco Command variants (C# dark, native light) and the same eight `visual-config-0001` ConfigMaps.
The lossless paired image preserves both originals. Native cache membership was
1832 at query capture, then 1834 after background synchronization; C# reported
1410. Those total-membership differences are not resolved or presented as full
functional parity. API/minute and sync ages legitimately differ because the
applications have independent request schedules.

C# enforces a minimum 1120-by-720 window. Its desktop capture therefore cannot be
presented as a smartphone reference. Native installed-window drag attempts did
not resize the captured window; landscape/rotation evidence remains the public
Qt application harness. The desktop package capture precedes the final
open-drawer rotation safeguard; that safeguard has separate regression evidence.
All owned stack processes, containers, volumes, credentials and temporary profiles
were cleaned by the existing driver. Pre-existing resources and `default.profraw`
were left untouched. Physical devices, complete theme/presentation equivalence,
full coverage and release performance remain unproven.

Final accepted selected run: `2026-10-08-landscape-ui/final.log` and `final.xml`,
61/61 passed in 17.86 seconds with 12 workers: 39 behavior executions and 22
control-style variants. This is not a full-suite or coverage result. The initial
rotation regression queried a Popup as a visual Item and crashed the test itself;
it was corrected to read the public QML Popup's visibility, with a checked lookup.
Secret-leak presentation checks include the new error banner, not only the footer.

The final binary was also started against the third real local cluster. Its
same-cluster C#/C++ captures are in `2026-10-08-landscape-final-desktop`, with
both applications using the dark surface and the same eight ConfigMaps. Final
captured cache counts were 1412 (C#) and 1836 (native). The native header summarizes
the filtered cache, per SHL-003, whereas the reference header retains global usage;
the screenshots do not assert that those differing summary scopes are identical.
The native final package includes the open-drawer rotation fix. Its private
packaging preflight passes dependency paths, ad-hoc integrity, notices and sizes:
ZIP 31,725,931 bytes; installed regular-file logical sum 85,065,595 bytes.
Package: `2026-10-08-landscape-final-package`; log:
`20261008-landscape-final-package.log`. Executable SHA-256:
`a851468583ad29ea7006cdd18b7b714e5f2574d32923019f4ffb90ddcecf6d84`;
ZIP SHA-256:
`39abad16535c601779720033992b9a65a1f4ba8f15ca2bb1a773069e7df8bab9`.
Developer ID signing/notarization remains outside this private stage. No complete
release, real mobile device, full picture matrix or new performance gate is claimed.

## Which empty-state, global search and appearance checks passed on 2026-10-08?

Scope: the Resources empty-state logo, cache-only global Resources/Events match
navigation, and the explicitly approved removal of theme intensity. This closes
this increment, not Alerts/Sources/About/Diagnostics presentation or whole-view
parity. The ordered remaining view/function gates live in `doc/ROADMAP.md`.

| Behavior | Public entrypoint | Executed scenario / remaining gap |
| --- | --- | --- |
| Missing empty-state branding | Actual Main.qml Resources view | `native.ui.empty_brand_{Basic,Fusion}` loads the bundled reference logo and real explanation without a request. No generated replacement asset. |
| Next/Previous and wrap | Global Resources search and row selection | `query_next`, `query_previous`, `query_wrap`, `query_keyboard` drive real buttons/Enter/Shift+Enter, match counter and selection. Inspector stays closed and the external HTTP request count stays unchanged. |
| Empty, invalid and zero-match queries | Same global search | `query_none`, `query_invalid`, `query_clear` disable navigation and show 0/0; malformed expressions retain visible error feedback. |
| Narrow navigation and filtered empty logo | Real QML window and filtered cache | `query_narrow`, `query_logo` cover 600x390 control visibility and zero-result branding. This is a headless Qt layout check, not a physical phone result. |
| Events navigation | Global Events search | `query_event` drives the current cached Event table, wraps through visible results without inspection or extra HTTP. |
| Independent table-local finder | Resources/Events table Find | Existing `native.table_find.*` passes unchanged: global filters and finder remain independent. |
| All retained appearance choices | Inline Settings Appearance controls | All 19 themes x dark/light, invalid names/variants, repeat/no-save, lock/conflict, restore and read-policy preservation pass. Palette/control-role equality is not whole-view visual parity. |
| Removed intensity | Actual Appearance view and public settings store | `native.appearance.removed_intensity`, `native.settings.appearance_upgrade_{subtle,medium,arcade}` and `appearance_removed_field`. Versions 4-7 read without rewriting; explicit save produces version 8 without the removed field and preserves unrelated policy/language/water/restoration. Current version rejects the removed field. |
| Draft and language regressions | Existing Settings and profile entrypoints | Existing `native.settings.draft.*`, `native.ui.settings_inline_*`, language catalog/old-profile/error cases pass. Appearance changes do not save/discard unrelated drafts. |

The three initial public regression checks failed before production changes:
missing logo, missing global Next and the still-present intensity control.
Evidence: `/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-08-empty-query-appearance/baseline.log`
and `baseline.xml`. An intermediate own QML separator error was corrected;
its failures are not counted as accepted behavior evidence.

Final selected run: **237/237 executions passed in 17.66 s with 12 workers**,
comprising **189 behavior executions and 48 style variants**, not 237 unique
features. Evidence: the same directory's `final.log` and `final.xml`.
The prior 114 intensity matrix executions became 38 retained theme/variant
executions; three obsolete intensity-validation executions were removed.
No full-suite, fresh coverage, CPU/RSS latency gate, all-view screenshot matrix
or real-device pass is claimed. No Kubernetes container was needed or created
for this deterministic external-HTTP-boundary/QML increment.

The same increment also passed private macOS arm64 Release package preflight:
32,383,977 ZIP bytes; 85,722,219 installed regular-file logical bytes. Bundled
load paths and ad-hoc integrity passed. Executable SHA-256:
`638b0e7572ba82bf2115ef2193964c491f39699fe6230610caf80e24b42db85e`.
ZIP SHA-256:
`60e13d496e97002ecb329c8e3b26b2e09e7ea9fa5d64a55cee4297de0f9c4e3e`.
Artifacts: `/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-08-empty-query-package`;
log: `release-evidence/20261008-empty-query-package.log` under the same comparison root.
Signing/notarization remains deliberately deferred for private use.

Actual native package and preserved C# source build were launched through
separate newly created empty profiles. No ambient kubeconfig, production
credentials, update check or external audio player was used. Original desktop
images and six lossless side-by-side pairs (Empty, Appearance and the pending
Alerts/Diagnostics/Sources/About surfaces) are in
`/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-08-empty-query-desktop`,
with `comparisons.json` and `index.html`. The native logo is visibly rendered;
the C# empty-state accessibility tree includes its logo but its captured frame
does not show it. This is not represented as exact empty-state pixel parity.
The images confirm substantial remaining Settings presentation differences;
they are before-evidence for those open increments, not completed-view passes.
Both owned desktop processes and temporary profiles were cleaned up.

Concrete remaining defects from this review: Sources import controls remain
visible above unrelated Settings sections, consuming vertical space; the native
Alerts editor exposes raw field/enum expressions instead of reference-shaped
typed controls and swatches; Diagnostics shows seven cache/policy rows but omits
actual process RSS/private memory/thread values; About lacks the reference
branding composition. The current C# reference also opens a selected default
rule's editor, so that alone is not a legacy/native behavioral discrepancy.
These observations refine the ordered view gates rather than approve dropping
any capability or fabricate native equivalents for .NET-only heap counters.

## Which Settings Controls And Diagnostics Tables Were Completed On 2026-10-08?

Scope: typed alert editing, Settings navigation, About branding/support links,
real process diagnostics and the common runtime/audit table toolbox. The owning
remaining view gates are unchanged in `doc/ROADMAP.md`; this increment does not
claim complete Settings or product parity.

| Behavior | Public entrypoint | Executed scenario / remaining gap |
| --- | --- | --- |
| Unrelated source controls in Settings | Main.qml Settings entry | Existing `settings_inline_*` scenarios require the global source-management panel to stay hidden. Sources keeps its own import controls. The real `closeSourceControls` icon also closes expanded controls in narrow Resources. |
| Section navigation | Visible Settings buttons or compact picker | Static desktop controls preserve accessible identities. Below 650 logical pixels, tests operate the actual section ComboBox by keyboard. Existing source-removal, restoration and unsaved-draft callers use that visible route. |
| Alert fields, colors and holds | Actual AlertView editor | `native.alert_ui.reference_controls_{boolean,boolean_invalid,color_status,color_custom,color_cancel,hold,narrow,locked}_{Basic,Fusion}` preserves canonical IDs, rejected expressions, custom color cancellation, built-in locks, save and reopen. Matcher help is collapsed until requested. Imported sounds and exact legacy view-entry/hold/action semantics remain separate gaps. |
| About | Real AboutView buttons and bundled image | `native.about.{liberapay,branding,branding_narrow}` passes with both control styles. All seven reference project/support destinations remain available; offline licenses and attribution tests pass. Native update/download ownership and complete presentation remain open. |
| Runtime measurements | Real Settings UI and actual OS process | `settings_inline_runtime`, `runtime_snapshot`, `runtime_keyboard` check RSS, physical/private memory, CPU seconds and threads, stable displayed values after touching 64 MiB, then explicit pointer/keyboard refresh. No synthetic runtime numbers or process-shell commands. Non-macOS measurement branches still require execution on those targets. |
| Runtime/audit tables | Shared grid controls | `settings_inline_diagnostic_{tables,sort,copy,columns,columns_restart,find,find_empty,find_keyboard,narrow,audit_sort,audit_copy,audit_find_keyboard,audit_duration}` covers actual headers, ASC/DESC/NONE, clipboard, persisted hidden/pinned columns, wrap navigation, focused Find and numeric duration sorting. Audit cases run the existing local HTTP fake only at the external Kubernetes boundary; workspace, caches, audit, store and QML are real. |
| Rejected diagnostic actions | Public Workspace entrypoints | `diagnostic_sort_{table,negative,high}` and `diagnostic_copy_{table,negative,high,empty,missing}` reject unsupported inputs without changing sorting, clipboard or request count. These supplement, not replace, the reachable UI workflows. |
| Layout migration | Public TableLayoutStore | `native.table_layout.diagnostics_upgrade_{read,save,current_missing,unknown,invalid}` checks read-only version-7 migration, explicit version-8 save and preservation of existing choices/data on failure. Existing layout conflict, locking, concurrent save and older migrations remain covered. |
| Stable snapshot during resource filtering | Visible Diagnostics audit table | `native.ui.diagnostics_snapshot` keeps the retained audit unchanged while resource filters change, then refreshes explicitly without another request. The virtualized table must be scrolled into view before its rendered row count is asserted. |

Evidence directory:
`/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-08-settings-controls`.
Initial branding, alert and source-control regressions failed before their fixes.
`diagnostic-baseline.log/xml` proves the missing shared table; after real vertical
and horizontal scrolling, `diagnostic-duration-scrolled-baseline.log/xml` proves
that request durations had no numeric sort value.

The earlier complete native run passed **3924/3924** executions in **636.11 s**
with twelve workers: 2937 behavior executions and 987 style variants, not 3924
independent features. The completed diagnostic-focused run passed **427/427**
in **27.60 s**: 263 behavior executions and 164 style variants. Logs/XML:
`full-final.*` and `diagnostic-final.*`.

The later complete suite executed **3971** registrations in **568.27 s**, with
**3970 passed and one failed** (`native.ui.diagnostics_snapshot`), recorded in
`full-diagnostic-final.log/xml`. That test still read a virtualized row count
before revealing the table; its subsequent recovery result is recorded in
`diagnostic-recovery.log/xml`. Do not relabel the failed complete run as a clean
pass. Component results do not establish coverage, physical UI or release gates.

The current private macOS arm64 Release package passed the unchanged dependency,
ad-hoc integrity and size preflight: **32400245 ZIP bytes** and **85739723 installed
regular-file logical bytes**. Package directory:
`/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-08-diagnostics-tables-package`.
Log: `release-evidence/20261008-diagnostics-tables-package.log` under the same
comparison root. Executable SHA-256:
`d08cccb07a456da73d6dc59beebb353fcdcc82809ffb673c31cfda029ed527df`.
ZIP SHA-256:
`fb7c0ef97fff303fcf4a46674b9f1baafb58fabb7880af810f86d6f4bef51d3e`.
Developer ID signing and notarization remain deliberately deferred for private use.

`native-diagnostics-qt-fusion.png` is a real two-times-scale headless Qt frame,
not a current Cocoa desktop capture or a fabricated screenshot. The preserved
C# desktop reference in `2026-10-08-empty-query-desktop` remains before-evidence.
New desktop comparisons could not run: the desktop controller reported a locked
Mac and the isolated C# process failed its native render-timer startup. The owned
native/reference launch attempt and temporary profiles were cleaned up. No
Kubernetes/Docker stack was created for this increment; no shared resources or
`default.profraw` were touched.

Remaining gates include Sources table/presentation, About update/download handling,
cache-byte telemetry, complete translations, remaining alert capabilities,
all-view/theme/terminal image evidence, physical target devices and the already
open coverage/foreground performance thresholds. Test count is not parity evidence.

The corrected snapshot test and all related Settings/table regressions subsequently
passed **429/429** executions in **25.20 s**, with twelve workers: 265 behavior
executions and 164 style variants. This recovery changes only the test's real
scroll/settling interaction; the production package above is unchanged. The
earlier full-run failure is retained as evidence rather than overwritten.

A lossless progress comparison is available in
`release-evidence/2026-10-08-diagnostics-controls-comparison` under the comparison
root: C# desktop reference left, current headless Qt Fusion right. Original pixels
and both original frames are retained. `capture-boundaries.json` declares the
unequal capture boundaries and empty-session/source-import difference. This is
neither a fresh dual-desktop pair nor evidence that the whole view is visually
identical, that memory improved by the shown numbers, or that the release is done.

## Which Alarm Lifecycles And Sources Workflows Were Completed Next?

| Behavior | Public entrypoint | Executed scenarios / boundary |
| --- | --- | --- |
| Finite alert holds | ResourceClient ingestion and Alerts rules | `native.alert_lifecycle.{once_color,once_animation,duration_color,duration_animation,duration_survives_unmatch}` checks 1350 ms once, bounded duration, no update extension and survival after unmatch. Only the external Kubernetes HTTP boundary is replaced. |
| View-entry effects | Public visible-membership publication and actual session cache | `view_entry`, `view_reentry`, `view_expiry`, `view_hidden`, `view_false`, `view_global`, `view_repeat`, `view_invalid`, `session_close` cover silent initial membership, boolean criteria, filtered view entry and whole-session matching. |
| Once editor/persistence | Actual AlertView and public rule store | Once saves/reopens without a seconds field. The focused alarm/filter run passed 527 executions in 75.17 s: 346 behavior executions and 181 style variants. `once-baseline.xml` retains the initial failure; final evidence is `alert-filter-final.log/xml`. |
| Sources table | Actual Settings section and private imported kubeconfig | `native.sources_table.{table,narrow,sort,copy_keyboard,find,find_empty,find_keyboard,columns,columns_restart}_{Basic,Fusion}` checks row-owned clusters, ASC/DESC/NONE, canonical clipboard, wrap Find and persistent hidden/pinned columns. No session is opened and no Kubernetes request is needed. |
| Sources mutations | Actual pointer/keyboard controls and confirmation dialogs | `rename`, `remove_cancel`, `remove`, `remove_all`, `refresh_changed`, `hidden`, `repeat` preserve existing source actions and cover growth from two to four contexts after actual changed-file reimport. Empty tables and hidden-section stability are explicit outcomes. |
| Rejected Sources actions | Public Workspace actions | Negative/high/action-column sort indices and invalid copy indices/identities leave sorting and clipboard unchanged. |
| Sources layout migration | Public TableLayoutStore | `source_upgrade_read/save/current_missing/unknown/invalid` checks version-9 explicit save, read-only migration and preservation on failure. Existing lock/conflict/concurrent-save cases remain applicable. |
| Virtualized row membership | Real shared TableView and source-file reimport | The changed-file workflow first aborted with stale Qt row mapping after column pinning. `sources-crash-stack-final.log` records the actual Cocoa/Qt stack. Clearing derived row reordering fixes both styles without resetting the model or relaxing the expected four contexts. |

The Sources-focused run passed 437 executions in 14.62 s with twelve workers:
302 behavior executions and 135 style variants. Final focused evidence is
`sources-membership.log/xml`. Two additional UI scenarios were then added;
their current complete-suite result must be reported separately. Earlier failed
focused runs are retained, not relabeled as passing evidence.

Evidence root:
`/Users/yuna/.local/share/podlord-comparison/release-evidence/2026-10-08-parity-desktop`.
Nine `comparison-settings-*.png` pairs retain original C#/C++ Cocoa pixels. They
compare the preserved C# app against native commit `774adc2`, before this alarm
and Sources increment, on an owned real local Kubernetes fixture with 1051
labelled resources. Different cache counts and persisted water settings mean
these are presentation evidence, not exact equal-data radar or full-theme passes.
`native-settings-sources-current.png` is a later actual Cocoa frame of the current
Sources table with two imported contexts and no opened session; it is not a
populated-cluster comparison. Both runs' owned processes/profiles were cleaned up.
The separate `2026-10-08-sources-current-comparison` directory retains a lossless
C#-left/current-C++-right Sources image and `capture-boundaries.json` explicitly
declaring those different datasets. It is progress evidence, not an equal-fixture
comparison.

The separate real Kubernetes terminal run passed `real_shell`, `real_vi`,
`real_interrupt` and `real_touch_interrupt`; see `terminal-real-e2e.log` and
`2026-10-08-alert-terminal-e2e` under the evidence root's parent. The scripts
cleaned their owned Kubernetes containers, volumes and temporary profiles.

The current Sources private macOS arm64 package passed dependency/ad-hoc integrity
and the unchanged size gates: 32406009 ZIP bytes and 85756795 installed regular-file
logical bytes. Directory: `2026-10-08-sources-table-package` under the same evidence
parent; log: `sources-package.log`. Executable SHA-256:
`513c2f27e4036ec7ed683f862421970f4d385edf21fdb650591db1c4359228a0`.
ZIP SHA-256:
`5d7fca3f8462048d56663a9f7b2c373564993876f5357d99d5b3c8eb0ff600a0`.
Signing/notarization remain deliberately deferred. Package preflight and these
scoped workflows do not close remaining camera/imported-sound, update-policy,
translation, cache-byte, coverage, performance or physical-device/image gates.

## What Did The Subsequent Complete Settings Check Establish?

The first complete run above executed 4039 registrations in 578.51 s: 4037
passed and the Basic/Fusion port-forward context-removal workflows failed. Those
tests retained a delegate/navigation assumption from the old Sources cards.
They now use the real visible section selector and reacquire the canonical
removal control after virtualized layout. The focused recovery passed both
styles in 3.39 s; `full-current.log/xml` retains the failed complete run.

| Behavior | Public entrypoint | Current executed evidence |
| --- | --- | --- |
| Cache payload estimate | Workspace Diagnostics snapshot and real shared model | `native.cache_diagnostics.{empty,missing,populated,detail,unicode,secret,repeat,filtered,refresh,rejected,close,multiple,logs}` checks zero/real byte values, UTF-8 detail size, all-session aggregation, retained closed cache, changed lists, read failure, stable filtering/reuse, logs and no Secret contents in diagnostics. The external Kubernetes HTTP boundary alone is replaced. 13/13 passed in 5.68 s; `cache-baseline.log/xml` retains the initial missing-metric failure. |
| Stable displayed bytes | Public Diagnostics refresh and model display | Empty and populated cases assert the actual grid model displays the measured byte value, without another request. The estimate excludes allocation overhead and shared projections, and is not labeled RSS. |
| Import feedback placement | Actual Main/Settings pointer routes | `native.settings_notice.{once,unrelated,failures}_{Basic,Fusion}` checks one notice in Sources, none in unrelated Settings, and the still-working failure-details dialog. All six baseline executions failed before the presentation fix. |
| Audio first use and reuse | Real embedded asset playback | Each of the 116 assets now has one named repeat scenario that decodes the first play and its replay. The separate one-play process repeated the already asserted first-play path and was removed; no asset, repeat assertion or failure handling was removed. Tests use QCoreApplication and Qt's FFmpeg backend, not terminal windows or a new GUI for each alert. Physical audible/device comparison remains separate. |

The resulting complete current native suite passed **3942/3942** registrations in
**348.81 s** with 24 workers: **2907 behavior executions and 1035 style variants**.
Evidence: `closure-full.log/xml` in `2026-10-08-parity-desktop`. These are not 3942
distinct product features. Private profiles, offscreen windows and loopback
listeners isolate this lane; actual desktop/performance comparisons retain their
separate serial lane. The changed registration set and worker count mean the
wall-time difference is workflow evidence, not a like-for-like application speed
benchmark.

The updated private macOS arm64 package also passes the unchanged size,
dependency and ad-hoc integrity gates: **32408033 ZIP bytes**, **85773307 installed
regular-file logical bytes**. Directory: `2026-10-08-settings-closure-package`;
log: `closure-package.log`. It uses the same explicit Qt 6.11.2 and portable
OpenSSL/yaml-cpp prefixes as the tested build. An incorrect default-Homebrew SDK
attempt failed before packaging and is retained as
`closure-package-wrong-sdk.log`, not counted as a passing supplier check.
Executable SHA-256:
`877cc05a5151bdaef95fa7894f131e263a69e70948e8675f700dc985ba96dc85`.
ZIP SHA-256:
`cd7e1c854f10747c3c56b58256161f3d9dc17463ae4450429942bf8b12de6801`.

The attempted current foreground benchmark lost desktop focus and stopped before
completing its protocol; see `2026-10-08-sources-foreground`. It does not replace
the prior open performance gates. A later current-desktop launch encountered a
locked Mac and was cleaned up without capturing or fabricating a new frame.
The earlier current Sources screenshot therefore predates the notice/cache
changes. Update policy, remaining action/imported-sound capabilities,
translations, coverage, full image/device comparisons and performance remain
explicit gates; complete execution of these component tests does not close them.

The unchanged C# reference sources also passed their current four test projects:
Core **91**, App **214**, Kubernetes **84** and headless Layout **69**, **458**
executions total, none skipped. Kubernetes includes the 25 real local K3D
scenarios. Evidence: `legacy-current` for Core/Layout and `legacy-recovery` for
App/Kubernetes under `2026-10-08-parity-desktop`. The initial isolated-home
Kubernetes attempt failed because Docker resolved `/var/run/docker.sock`; the
recovery explicitly selected the validated local Colima Unix socket. No
production/shared cluster or default kubeconfig was used. The App project's
initial no-restore invocation produced no test results and is not counted;
after package restore, all 214 actually executed. The fixture and wrapper
cleaned their run-owned cluster/volumes and temporary homes/profiles using
ownership labels. Reference tests prove those executed C# paths, not native
functional or visual equivalence.

## Weekly updates and shrinking search selections, 2026-10-08

| Behavior | Public entrypoint | Executable evidence | Remaining gap |
|---|---|---|---|
| Anonymous weekly checks, restart, actual running timer and manual override | `ReleaseUpdates::startAutomaticChecks`, `checkIfDue`, `checkNow` through real loopback HTTP and timer dispatch | `native.release_updates.{weekly,scheduled_weekly,manual,future,upgrade,restart,coalesce,owners,process_owners}` | Published native assets, live GitHub distribution integration |
| Safe compatible native links; incompatible C# assets rejected | HTTP metadata and explicit download/release actions | `native.release_updates.{success,legacy_asset,no_asset,older,same,handoff}` | No installer; private distribution only |
| Failure retention, malformed metadata, rate limit and bounded response | Real external-boundary HTTP responses | `native.release_updates.{preserve_failure,http_401,http_403,http_404,http_429,http_500,redirect,timeout,malformed,non_object,oversized}` plus named version/asset cases | These tests do not establish platform distribution readiness |
| Private cache and unsupported data preservation | Public update owner with temporary private profile | `native.release_updates.{idle,disabled,invalid_cache,cache_symlink,lock_directory,bad_endpoint,permissions,empty_clock,invalid_clock,invalid_current_version}` | OS/architecture runtime matrix remains separate |
| Actual About controls, busy state, narrow layout and deliberate browser handoff | Real QML, pointer/wheel input, external browser handoff capture | `native.release_updates.ui_{idle,download,busy,narrow}_{Basic,Fusion}` | New update controls not in the earlier desktop capture set |
| Search ordinal after filtering removes the selected row | Real workspace filter text and IME commit, shared resource grid | `native.table_find.filter_{shrink,ime}_{Basic,Fusion}` | Additional physical input/assistive technology matrix |

Desktop evidence at `~/.local/share/podlord-comparison/release-evidence/2026-10-08-weekly-desktop/` contains nine actual C#/C++ Settings pairs and native real Kubernetes PTY/ANSI/vi/expanded-terminal images. They use the same owned 1,051-resource fixture, but asynchronous cache population and the C# visible cap differ; these are not identical-snapshot proofs. The comparison fixture, its containers and private profiles were removed by the test owner.

The foreground performance attempts remain failures, not release passes: cached tab p95 118.71 ms, inspector p95 65.53 ms, maximum RSS 288,309,248 bytes. Focus was lost before the full 60-second idle protocol completed. No idle CPU pass follows from the partial sample. The native on-screen Esc key successfully left vi insert mode; physical Escape through desktop automation did not, and remains an input-evidence gap.

The terminal regression identified missing empty-text Ctrl+[ handling and unnegotiated CSI-u output for standard Ctrl+I/J/M. The native terminal now sends the xterm C0 bytes for letters, bracket/punctuation controls and Ctrl+Space. `native.terminal.{empty_escape,control_escape,control_tab,control_linefeed,control_enter,control_space,control_backslash,control_bracket_right,control_caret,control_underscore}_{Basic,Fusion}` drives actual QML keyboard input and verifies the remote stream bytes. Qt's physical Control mapping on macOS is used, not Command. `scripts/test-native-visual-kubernetes.sh native-terminal-e2e` additionally executes `real_control_vi` in an owned Kubernetes Pod and verifies saved-file readback and successful shell exit. This establishes actual shell compatibility, not physical desktop-automation correctness.

Execution results for this increment: the complete native lane passed 4,022/4,022 executions in 301.78 seconds with 24 workers (2,971 baseline behavior executions and 1,051 style variants, not 4,022 product features). The subsequently added running-weekly-timer case passed with the complete 57-case release-check lane; 4,023 registered cases have executable passing evidence across those runs. The unchanged C# lane was not rerun; its preceding 458-case result remains separate evidence. The five real Kubernetes terminal scenarios passed, including Ctrl+[ vi readback; the owner verified removal of `podlord-visual-run-lhlavw` and its volumes/profile.

The private arm64 package at `~/.local/share/podlord-comparison/release-evidence/2026-10-08-weekly-update-package/podlord-native.zip` passed the packaging preflight using the official Qt 6.11.2 SDK. Executable SHA-256: `69032a71d02edf4090250bf792e2ede944974663d3847df5c8413c1331d1383e`; ZIP SHA-256: `cc529fbe4ddb1ac27c4ad5f6ed1e7bb936c93ff722bb5128da1f19bc6e2629d5`. This is local ad-hoc signing, not Developer ID/notarization. It does not establish clean-device, full parity, performance or all-platform readiness. The earlier branch-coverage failure has not been remeasured or cleared by these functional runs.

### What proves native window ownership and transfer?

`native.window.*` drives the real QML shell through `WindowHost` and public
workspace actions. Each scenario has its own private profile and its own minimal
external Kubernetes HTTP/WebSocket boundary; stores, schedulers, view models,
terminal emulator, forwarding transport and window creation are real.

| Behavior | Public entrypoint | Executable scenario | Remaining boundary |
|---|---|---|---|
| Selected/background tab transfer and one placement | Tab detach icon, session activation | `active`, `background`, `focus` | Physical desktop input and C#/C++ paired images |
| Quick-open saved/existing/closed sessions and imported contexts | Actual dropdown/submenu, public context/session window actions | `menu_window`, `context_window`, `new_context_window`, `reopen`, `repeat` | Physical native menus and installed startup |
| A multi-tab detached window can split another tab | Duplicate, activate, separate-window action, scoped close | `nested` | Physical desktop |
| Unrelated editor draft stays in its window | Background-tab detach icon | `background_dirty` | Physical desktop |
| Active draft asks before transfer | Detach, discard confirmation | `dirty_stay`, `dirty_discard` | Physical desktop |
| Per-window search and shared saved filters | Workspace filter/preset controls | `filter`, `preset` | Installed legacy preset migration remains separate |
| Shared rename, request settings, radar preferences and columns | Public settings/session/layout actions | `rename`, `settings`, `columns` | Complete Settings/table visual parity |
| Shared alarm preferences without duplicate transport owners | Public alarm preference action and immediate window startup | `alerts`, `alerts_initial` | Audible playback and effect-continuity matrix |
| Context deletion and remote-window draft protection | Source removal confirmation | `source`, `source_dirty` | Installed source-management comparison |
| Cached logs belong to the detached view | Log controls, tab detach | `logs` | Physical scrolling/trackpad behavior |
| Running terminal remains connected and interactive | Terminal controls, detach, keyboard | `terminal`, `close_terminal` | Physical platform key handling |
| Same forward survives transfer; other window survives closure | Ports controls, local TCP, window close | `forward`, `close_isolation` | Actual Kubernetes transfer lane below |
| Window close, real store lock failure/retry and last-main-window restoration | Native window close, persistent catalog and real `QLockFile` | `close`, `close_locked`, `main_close`, `root_restore` | Installed restart, write failure and platform lifecycle matrix |
| Atomic scoped close, invalid/repeated/missing IDs | `SessionStore.closeSessions` | `batch_subset`, `batch_empty`, `batch_null`, `batch_duplicate`, `batch_missing` | Runtime lock/write failure matrix |
| Invalid session and missing QML resource do not create a window | Public detach and host startup | `missing`, `missing_resource` | Partial platform window-creation failures |

The Basic/Fusion executions are style variants of the same named behaviors,
not independent product features. The terminal local-cluster lane additionally
runs `window-host-test real_transfer`: 1000+ real listed resources, two sessions,
two real forwards, one real interactive shell, a remote shell environment value
that survives detachment, unchanged forward identity/port, and another working
forward after the detached window closes. Its pictures are explicitly headless
Qt renders, not physical desktop or C#/C++ parity evidence. The existing fixture
script owns and removes its cluster, volumes, copied binaries and profiles.
`native-window-e2e` selects this same real window scenario without rerunning the
five independent terminal scenarios; this is a focused test lane, not a product
mode or a weaker simulated transport.

The complete native lane passed 4,095/4,095 executions in 308.98 seconds with
24 workers: 3,008 baseline behavior executions and 1,087 style variants. These
are not 4,095 distinct product features. The existing close-with-discard tests
now assert that a closed projection releases its models/session instead of
asserting stale UI state after closure. Initial alarm preference and store-lock
failure cases passed without production changes. The subsequently added nested
window regression failed first against the single-detached-window restriction.
After removing that restriction for multi-tab windows, all 80 scoped window and
close-regression executions passed in 31.75 seconds, including both new nested
cases. The 4,097 currently registered executions have passing evidence across
these runs; there is no claim of a single complete 4,097-case run.

Real Kubernetes evidence is under
`~/.local/share/podlord-comparison/release-evidence/2026-10-09-window-input-kubernetes/`.
`podlord-visual-run-xeozfn-windows.log` records the passing same-shell/forward
transfer and close isolation; paired main/detached images are headless Qt renders.
The fixture removed its owned cluster and temporary profiles/volumes. The five
real terminal scenarios also passed in the preceding full terminal lane, whose
window step failed before the test was corrected to wait for processed shell
output and actual rendered keyboard focus. Those failed attempts are retained,
not represented as successful complete lanes.

The final private arm64 bundle passed the official Qt 6.11.2 packaging preflight
at `~/.local/share/podlord-comparison/release-evidence/2026-10-09-windows-final-private-package/`.
It is 32,454,514 ZIP bytes and 85,866,171 installed regular-file bytes, below the
50 MB/100 MB package boundaries. Executable SHA-256:
`3f17f0cf5b88d6f9af0d729b2f4cf8a1d7f47f55e3a3cba65d4f3b99ab7e9838`;
ZIP SHA-256: `f76e7efbb53c70a6ebe1290ce1f2aa1c2ed19905eefdbd1779912ea40976916f`.
This is local ad-hoc signing and a packaging preflight, not Developer ID,
notarization, clean-device or full migration/release readiness. The preceding
foreground performance and branch-coverage failures remain open.

### What proves the restored row limit and current terminal boundaries?

The 2026-10-09 increment restores the reference application's default 256-row
display cap and configurable 1..5000 limit without truncating the filtered cache,
Radar, field-value discovery or session-wide alarms. Saved views use version 6;
native presets use version 2. Older inputs are read without rewriting them and
receive the reference default. Unsupported or malformed current files remain
untouched. Rollback requires retaining the preceding profile, not down-conversion.

| Behavior | Public entrypoint | Executable evidence |
|---|---|---|
| Display cap, ordering, filters beyond the cap, complete picker/Radar, refresh and session isolation | Actual Workspace/QML table and limit control | `native.field_filter.limit_{default,ui,narrow,sort,filter,picker,reset,preset,session,refresh,radar,restart}_{Basic,Fusion}` |
| Invalid, empty, zero, negative, overflow and bounded positive limits | Public limit setter and saved-view/preset stores | `native.field_filter.limit_normalize_*`, `native.view_state.limit_*`, `native.view_state.preset_*limit*` |
| Actual C# preset export retains its limit across native import/restart | Reference `FilterPresetStore`, native preset controls and fresh Workspace | `scripts/test-filter-migration.sh`, 14 passing executions; reference export remains byte-identical |
| Cross-window activation updates durable usage instead of treating a local active tab as globally active | Public window/session activation and persistent SessionStore | `native.window.cross_focus_{Basic,Fusion}` |
| Cached island replay and metric-only updates preserve reference topology | Public Radar model/render boundary | `radar-reference-cached_scope_hit`, `radar-reference-metric_update` |
| Physical key translation, stderr, invalid exit statuses and malformed stream closures | Real QML keyboard input and Kubernetes WebSocket boundary | `native.terminal.key_*`, `status_invalid_exit_*`, `status_{array,other_failure,oversized,output_first}`, `frame_empty`, `close_bad_{size,channel}`, `stderr_frame`, each in Basic/Fusion |
| On-demand search remains cache-only and filters the Radar | Search toggle, actual field input and rendered Radar | `native.alert_ui.radar_search_on_demand`; real local Kubernetes `real_health` |

Evidence is under `~/.local/share/podlord-comparison/release-evidence/`:
`2026-10-09-row-limit-final` records 153 passing scoped executions;
`2026-10-09-reference-limit-restart.log` records the actual C# migration;
`2026-10-09-model-notification-regressions` records 547 passing notification,
filter and UI regressions; `2026-10-09-terminal-boundary-final` records 60 passing
terminal boundary executions. Basic/Fusion repetitions are style variants, not
additional product capabilities.

The complete instrumented lane passed 4,210 executions: 3,075 baseline behavior
executions and 1,135 style variants, including the two preflights. Its remaining
branch gate failed: line coverage is 97.44%, branch coverage is 83.18%, against
95%/90%. The immutable report, profile and binary manifest are in
`2026-10-09-terminal-coverage/`. The subsequently added on-demand search test
passed with all 10 scoped Radar/alert executions; this is not a claim that a
newer complete suite has run.

`2026-10-09-current-kubernetes-e2e/` records actual PTY/vi/control-key, window
transfer/forward isolation, context deletion, resource deletion/history/search
and measured-metric scenarios, plus 38 native Radar captures across all 19
themes in dark/light variants. That whole lane failed at an obsolete test that
typed into the hidden search. After correcting the test to use the actual search
toggle, `2026-10-09-health-search-kubernetes/` passed the focused health/Radar
scenario. Both fixtures removed their owned clusters and temporary profiles.
Those captures are native headless renders, not paired physical C#/C++ evidence.

The current private arm64 package is
`2026-10-09-filter-limit-private-package/podlord-native.zip`: 32,458,319 ZIP bytes,
85,882,923 installed regular-file bytes. Executable SHA-256:
`9dea8072d29921d1357d884fff46e5dc8ef4fa7311494e7c23209278b7bc6da0`;
ZIP SHA-256: `a6a030a45dac562ca398300055a1c1d5dec1c15e981d30a1e7b5de6932a6a2b3`.
This clears the packaging preflight, not full release readiness. Signing and
notarization remain deliberately deferred. Physical paired-view/theme evidence,
foreground performance, branch coverage and actual supported-device evidence
remain independent gates.

`2026-10-09-model-clean-performance/` is the subsequent clean headless/software
run: filter p95 32.02 ms, sort 55.84 ms, inspector 24.77 ms, peak RSS 193,134,592
bytes, steady growth 409,600 bytes and 60,001 ms nonsync idle at 0.733% of one
core pass. Cached tab p95 67.69 ms fails the unchanged 50 ms limit. Headless
numbers do not clear the physical foreground gate. `2026-10-09-tab-sampling/`
is deliberately profiler-perturbed diagnostic evidence, not a performance pass.

`2026-10-09-current-desktop-parity/` retains raw physical Resources images from
the current deployed native package and an isolated C# reference against the
same owned cluster. `resources-side-by-side.png` places unchanged windows next
to each other with labels only. Ordering, current resource/metric snapshots,
toolbar density and Radar coloring still differ. After switching applications,
native accessibility children became unavailable; titlebar/coordinate/keyboard
attempts did not restore reliable interaction. Only the reference Settings image
was captured. This partial comparison does not clear the paired view/theme matrix;
the fixture completion marker requests cleanup, not parity approval.

### How are sorting and Radar selection kept cheap without stale identities?

The canonical resource snapshot prepares one sort column at the explicit sort
boundary. Publishing any new snapshot invalidates those keys before model
notifications; age remains clock-dependent and is not cached. Radar retains
canonical cache indices for full-cache identities and maps only visible tiles
to the current filter/sort projection. Selected resources retain their path,
not a mutable sorted row number.

| Behavior | Public entrypoint | Test / evidence | Remaining gap |
| --- | --- | --- | --- |
| Sorting after changed values, insertions, removals, missing values, ties, column changes and numeric input | ResourceTable / ResourceFilter model API | `native.filter.sort_refresh`, `sort_insert`, `sort_remove`, `sort_missing`, `sort_ties`, `sort_changed_column`, `sort_numeric`; `2026-10-09-sort-key-regressions.xml` | Physical table interaction matrix |
| No-sort, Unicode, cluster fallback and clock-dependent age | ResourceTable / ResourceFilter model API | `native.filter.sort_none`, `sort_unicode`, `sort_cluster`, `sort_future_age` | None for these scoped paths |
| Radar selection survives sorting, is unavailable when filtered out, and disappears on deletion | RadarIsland selection and visible tile models | `radar-reference-selection_sort`, `radar-reference-selection_filter`, `radar-reference-selection_remove`; `2026-10-09-radar-index-regressions.xml` | Paired physical rendering |
| Full-cache discovery reaches table and Radar for standard and custom Kubernetes resources | Actual local Kubernetes API through native UI | `native-search-e2e` / `real_search`; `2026-10-09-discovery-search-kubernetes/` | Complete combined lane and paired physical rendering |

The sort-key scoped lane passed 119 executions; the Radar-index scoped lane
passed 155. `2026-10-09-radar-index-clean-performance/` passes the unchanged
headless/software gates: filter p95 34.45 ms, sort 43.38 ms, cached tabs 46.26 ms,
cached inspector 25.91 ms, peak RSS 192,806,912 bytes, steady growth 442,368 bytes,
and 60,002 ms nonsync idle at 0.841% of one core. This is not physical macOS
foreground or installed-startup evidence.

The subsequent physical attempt in
`2026-10-09-sort-radar-desktop-performance/` could not establish an active
foreground window. Desktop control explicitly reported a locked Mac. The
earlier accessibility/focus loss is therefore not sufficient evidence of an
application defect, and the physical performance/view/theme gates remain open.

### Which subsequent release checks have executable evidence?

The complete instrumented lane in `2026-10-09-sort-radar-complete-suite.log`
passed all 4,226 executions in 479.62 seconds including preflight: 3,091 behavior
executions and 1,135 style variants. Its immutable coverage archive is
`2026-10-09-sort-radar-coverage/`: line 97.45%, branch 83.18%. The branch gate
still fails; passing test executions are not a substitute for the 90% gate.

`2026-10-09-alert-document-boundaries/` subsequently passed all 91 scoped alert
store executions, including 49 new persisted-document rejection cases. They
exercise `AlertStore::load` and `save` through actual private files: wrong field
types, unsupported colors, identity/matcher shape, malformed JSON, non-object
roots, unknown built-ins, oversized documents and numeric type/range/integrality
boundaries. Every rejection retains the exact file bytes. The combined source
coverage in `2026-10-09-alert-document-coverage/` is line 97.45%, branch 83.44%,
still below the branch gate. These additions do not claim a second complete
suite run or change any product implementation.

`native-search-e2e` checks discovered cache rows by kind and owned name prefix,
then checks global search and Radar projection separately. Global search also
matches related-resource values, so a Service-name search is not a valid
Service-only discovery count. The focused lane passed ConfigMaps, Secrets,
Deployments, StatefulSets, DaemonSets, CronJobs, PVCs, Services, ServiceAccounts,
Jobs, Namespaces, the multi-container Pod and eight actual custom `RadarProbe`
objects. Regex, alternatives, invalid input and reset remain cache-only queries.
Its owned cluster and temporary profiles were removed.

`2026-10-09-sort-radar-kubernetes-e2e/` failed an incorrect Service-only global
search expectation; the test was corrected without changing search behavior.
The subsequent combined attempt in `2026-10-09-sort-radar-kubernetes-final/`
failed intermittently at `real_service`. Stage diagnostics and failure frames
were added, and `2026-10-09-forward-stage-kubernetes/` passed all four focused
Pod/Service/context-removal/draft-removal cases. That pass does not establish the
cause of the intermittent combined-lane failure. `native-forward-e2e` provides
the shorter real-cluster reproduction lane with the same ownership cleanup.

The fresh private arm64 package in `2026-10-09-sort-radar-private-package/`
passes dependency-path and size preflights: 32,459,011 ZIP bytes and 85,883,451
installed regular-file bytes. Executable SHA-256:
`d401f1ef38c10fb48064d6c6fc4773777ae6dc0c93621aadcd02f4236144cf0e`;
ZIP SHA-256:
`6d8fd5b4dee3d3cb1fd2b75d6121b40c55342a697f9f56c87a7c13a3eeeeea63`.
All 27 packaged dependency-boundary tests, seven Cocoa event-loop startup cases,
five CLI metadata cases and three actual mutual-TLS trust/rejection cases pass.
The package intentionally bundles Cocoa, not the headless `offscreen` plugin;
an earlier offscreen startup invocation was invalid for this package. Cocoa
startup does not establish active-foreground rendering or performance.
Signing/notarization remain deliberately deferred; physical parity, performance,
supported-device evidence and the remaining branch gate are not cleared.

`2026-10-09-kubernetes-complete-stable/` subsequently passed the entire combined
native lane against its owned local cluster: all five interactive-terminal
scenarios, window transfer and isolated forwards, all four forward/removal
scenarios, deletion and race observation, inspector history, discovery/search,
metrics and all 38 native Radar theme captures, health/Radar and cached field
filters. The fixture removed its owned container, volumes and temporary profiles.
The test script and executable hashes are retained. This is actual local API
and native headless evidence, not a paired physical C#/C++ matrix, and does not
explain the preceding intermittent Service test failure.

The subtraction review keeps sort preparation private to ResourceTable and its
sorting proxy. Its helper-only test was removed rather than exposing cache
internals as a product API. Remaining sort tests drive the public model/proxy
boundary. Existing `native.ui.markup` and `native.ui.inspector_markup` passed in
the complete lane, covering untrusted resource markup without render-time image
requests; no parallel presentation-sanitizer implementation was added.

### What do the latest physical Settings comparisons and runner corrections prove?

`2026-10-09-current-desktop-parity-final/` contains actual C# and C++ captures
for Alerts, Appearance, Diagnostics, Graphics, Privacy, Sources, Sync,
Workspace and About. Each `*-side-by-side.png` keeps both original images
unchanged, with C# left and C++ right and a separate label strip. This is one
physical palette comparison, not the complete views/themes/device matrix.
The native captures precede the compact row-limit/preset layout below. Resource
counts differed between the two independently populated caches, so these frames
do not establish resource-set equality or identical topology for different sets.

The row limit now sits beside Problems/Activity on wide sidebars and immediately
below them on narrow ones, before the field filters. The preset name, dropdown,
Save and actions share one row. `2026-10-09-limit-placement/` records four failing
layout regressions before the fix and 78 passing limit/preset/sidebar/style
executions afterwards. Limits still affect displayed table rows only, not cache,
Radar or alarm scope. The compact dropdown must retain a readable popup; the
`preset_overwrite` public UI flow now checks that its saved name fits before
selection. `2026-10-09-preset-popup/before.xml` records both styles failing with
the original 32-pixel popup.

Audio repeat tests now use the same GUI/Cocoa event loop as the application,
inside a background-only `LSUIElement` test bundle without a window. The prior
Core-only helper timed out for 116 repeat cases in the existing Homebrew build;
the GUI helper passes all 116 there and in the official SDK build. No decoder
reset or other speculative production playback change remains. The application
continues to use its owned media player directly; it does not launch a terminal
or test helper per sound. Evidence: `2026-10-09-audio-backend-boundary/` and
`2026-10-09-current-sdk-boundaries/`, respectively 118 and 168 scoped passing
executions including the two runner-argument cases.

`scripts/test-native.sh` selects its build only through
`PODLORD_NATIVE_BUILD_DIR`, never a positional argument. Extra and empty
arguments now fail explicitly before tools or profiles are touched, as tested
by `native.runner.argument_extra` and `native.runner.argument_empty`. A previous
argument-based invocation tested the existing Homebrew directory, not the
intended official SDK directory. The subsequent correctly addressed SDK run
passed all 4,274 test executions, but editing its running POSIX shell file
invalidated the post-test script stream. Its log is retained in
`2026-10-09-official-current-complete-suite.log`; that run proves those tests,
not fresh coverage. Later complete runs use an immutable adjacent script copy
and archive it with their evidence.

`2026-10-09-current-metal-performance/` is actual foreground Cocoa/Metal data,
not a headless substitute: filter p95 32.78 ms, sort p95 66.76 ms, cached-tab p95
141.11 ms, cached-inspector p95 108.28 ms, maximum RSS 276,365,312 bytes. Tabs,
inspector and the 250 MB memory gate fail. Foreground was lost after 31.19
seconds, so the 60-second idle gate is also unproven. The measurement helper
previously requested macOS window activation before every measured action;
it now activates once and rejects foreground loss instead of including
repeated OS activation work in ordinary in-app tab/filter timings. A fresh
foreground run is required before claiming any resulting gain. The acceptance
limits and complete loaded-data workload remain unchanged.

## 2026-10-09 compact tables and real packaged terminal

Evidence root: `/Users/yuna/.local/share/podlord-comparison/release-evidence/`.
These results do not declare complete visual or release acceptance.

| Behavior / requirement | Public boundary | Test / evidence | Result and remaining gap |
|---|---|---|---|
| Canonical health snapshots do not manufacture absent resource fields | Real `ResourceClient` ingress from a local Kubernetes HTTP boundary, including repeated refresh | `native.read_overlap.snapshot_fields`; `2026-10-09-canonical-cache-fields/before.txt`, `after.xml` | Failing first on an inserted `kubernetesEvent: null`; 27 scheduler/view/cache cases pass after immutable input reads. No private helper is tested. |
| Resources/Events have no unused toolbar row; find and column commands remain reachable | Rendered QML, real menu clicks and keyboard input | `native.ui.table_compact_tools_{Basic,Fusion}`, existing `native.table_find.*`, `native.ui.columns_*`; `compact-before.log`, `compact-after.xml` | Both styles originally expose a 32 px gap. All 130 scoped table/find/column/shell cases pass after moving main-table tools to the workspace menu; nested tables retain their local tools. |
| Radar identity, filtering, motion and view restoration survive index reuse | Rendered resource radar, canonical source updates and real UI input | `2026-10-09-radar-index-reuse/public-radar.xml` | 114/114 pass. Rebuilding the identity map retains the previous persistent indices until replacement; no topology or selection contract changes. Whole-UI performance gates are separate. |
| Packaged terminal is a real interactive PTY, not a command-output substitute | Fresh private Release bundle, native keyboard input, owned local K3s Pod `visual-a/visual-multi-container`, container `alpha` | `2026-10-09-compact-desktop-parity/native-terminal-{pty,vi,resize-ansi-interrupt,disconnected}.png` | Actual `/dev/pts/0`; resize from 12x121 to 46x167; vi input saved and read back; ANSI green; Ctrl-C interrupts sleep and restores prompt; shell exit disconnects. Test file removed, both comparison applications and owned cluster cleaned up. |
| Original C#/C++ image evidence | Two real packaged applications on the same owned K3s cluster, Sirocco Command dark | `2026-10-09-compact-desktop-parity/{resources,events,ports}-side-by-side.png`, `paired-frames.json` | Original full-resolution images side by side, C# left/C++ right, no crop/resize/retouch. Cache populations differ (1413/1844); these are layout evidence, not equal-corpus topology proof or the complete theme/device matrix. These captures precede the toolbar removal. |
| Private macOS package boundaries | Packaged Release executable, deployed dependencies, real startup/CLI/mutual TLS | `2026-10-09-compact-filters-private-package/`, `2026-10-09-compact-package-boundaries/` | 42/42 pass: 27 dependencies, 7 Cocoa startups, 5 CLI, 3 mutual TLS. ZIP 32,459,080 bytes; installed logical files 85,883,451 bytes. This package precedes the canonical-health and toolbar changes. Signing/notarization remain intentionally deferred. |
| Full executable suite and coverage before these final changes | Frozen native runner, official Qt 6.11.2 SDK, real public application/model/UI boundaries | `2026-10-09-sdk-final-complete/` | 4276/4276 tests pass: 3141 behavior cases plus 1135 style variants. Line coverage 97.45%; branch coverage 83.42%, below the unchanged 90% gate. A current-source full run is recorded separately in `2026-10-09-compact-tables-complete/`. |
| Foreground Mac interaction/memory gates | Real Cocoa/Metal, 5000 resources across 3 sessions, 5 MB log budget | `2026-10-09-held-foreground-metal/`, `2026-10-09-radar-reuse-metal/` | Filter/sort/inspector meet their action-to-frame limits in these runs. Cached tabs remain about 55.7 ms p95 versus 50 ms. RSS remains above 250 MB. Foreground loss invalidates the 60-second idle proof. No headless substitution or relaxed gate. |

Current private-release blockers remain branch coverage, installed/foreground
performance acceptance, complete matched-corpus view/theme evidence and the
claimed platform/device runtime matrix. Headless style variants are meaningful
compatibility cases, not additional independent product capabilities. Existing
focused and full runners retain per-case names and bounded parallel execution.

## 2026-10-09 native catalog refresh and release measurements

The shared branch's CI changes report coverage and C# performance targets without failing functional verification. A green report is not evidence that the operational performance budgets, required presentation, device support or coverage targets were met. No native runtime budget was relaxed.

The catalog refresh removed forty native alarm-editor strings and restored retired theme-intensity strings. The reference exporter now preserves native-only English and translated controls, updates reference-owned strings, and excludes the two retired intensity keys. Export followed by `--check` succeeds. The installed native app still needs no .NET SDK; this exporter belongs only to reference comparison tooling.

| Behavior / public boundary | Executed scenario or test | Outcome and evidence |
| --- | --- | --- |
| Alarm labels and English fallback in every shipped language | `native.language.alert_catalog_<language>` through `uiText` | English fails before correction (`Missing alarm label: alert.chooseColor`). All 21 language rows pass afterward. |
| Retired appearance capability stays absent | `native.language.retired_theme_catalog_<language>` through `uiText` | English fails before correction. All 21 language rows pass afterward; no theme-intensity runtime control was reintroduced. |
| Translated table tools remain reachable through the actual workspace menu | `native.language.table_tools_<en,de,ar>_<Basic,Fusion>` | Existing scenarios failed at the hidden old button; actual public menu input now opens Columns. Full language subset: 118/118, 87 behavior and 31 style variants, 4.79 seconds. |
| Pinned-cell keyboard inspection/copy | `native.field_filter.table_keyboard_pinned_<copy,inspect>_<menu,f10>_<Basic,Fusion>` | 8/8 pass through the real menu and keyboard controls, 4.73 seconds. |
| Complete pre-correction SDK run | `scripts/test-native.sh` frozen runner, source `791b0c1` | 2/2 preflight; 4,263/4,277 main tests pass in 2,006.60 seconds. All fourteen failures are the two menu-entry groups above and are corrected by their targeted reruns. This is not described as a new all-green complete run. Coverage: 97.45% lines and 83.42% branches. Benchmarks after the failed main suite were not executed; copied older benchmark files must not be treated as current results. |
| Private deployed macOS arm64 artifact | `scripts/check-native-macos-package.sh` | Dependency closure and size pass. ZIP 32,459,039 bytes; installed regular-file logical sum 85,883,451 bytes. Ad-hoc signing only; Developer ID/notarization remain explicitly deferred. |
| Real Cocoa/Metal cached presentation | `scripts/measure-native-performance.sh` with Release helper, no concurrent owned build/test | Filter p95 43.16 ms, sort 33.02 ms, cached tabs 49.82 ms, cached inspector 34.22 ms. Each measured class passes in this run; tabs have little margin. Workspace/QML construction 531.36 ms is not installed-process startup. |
| Actual nonsync foreground idle | Same public workload, 60.001 seconds | 1.62% of one core; foreground retained. Pass for this run, not inferred from earlier interrupted samples. |
| Warm runtime memory | Same public workload, ten post-warm batches | Peak RSS 275,038,208 bytes exceeds the unchanged 250 MB target. Warm growth 3,293,184 bytes remains below the protocol's 5 MB tolerance. Runtime preflight still fails because of RSS. |

Evidence is under `/Users/yuna/.local/share/podlord-comparison/release-evidence/`: `2026-10-09-language-catalog-regression` retains before/after logs and XML; `2026-10-09-compact-tables-complete` retains the failed complete run and its matching profile/report; `2026-10-09-canonical-private-package` retains the actual bundle, ZIP, hashes and package/dependency evidence; `2026-10-09-canonical-cache-metal` retains the environment, public measurements and external API audit. The performance workload is 5,000 resources total across three sessions (1,500 Pods, 2,334 ConfigMaps, 1,166 Secrets), table limit 256, configured log retention 5 MB and 97 displayed log rows. Its fake is only the external Kubernetes HTTP boundary; Workspace, caches, QML and rendering are real.

Whole-product readiness remains open: RSS, installed startup and frame-work profiling, repeated-run stability, matched-state view/theme/device presentation, and actual target-specific installation. Functional/menu corrections do not close those gates.

## 2026-10-09 comparison lifecycle and filter reset

| Behavior | Public entrypoint | Executed test | Result / remaining gap |
| --- | --- | --- | --- |
| Reject an executable that is not declared by its comparison bundle before Docker or application startup | `scripts/test-native-visual-kubernetes.sh desktop` | `desktop comparison rejects an undeclared native executable before contacting Docker`; equivalent reference lane | Both failed before the guard; both pass afterward against private copies of the actual package. All 29 dependency/boundary tests pass in 57.31 s. |
| Stop a comparison when one owned app exits, without relaunching outside its isolated profile | Same desktop script, real local K3s on Colima, actual deployed native/reference applications | `an exited real comparison app fails the review and cleans up its owned stack` | Pass, 33.02 s. Terminating the owned native process produces the explicit failure, removes its isolated run directory and leaves no owned container. This opt-in real-stack case requires `PODLORD_LEGACY_APP`; it is not silently replaced by a simulated internal stack. |
| Reset resource filters using an icon with an accessible name and keyboard activation, without Kubernetes requests | Actual field-filter dialog, shipped Workspace and Qt scene; simulated external Kubernetes HTTP boundary | Existing `native.field_filter.reset_Basic` and `reset_Fusion` | Both fail against the old wide text button and pass after reusing IconButton. No new overlapping test scenarios. |
| Preserve narrow layout, filter-mode reset, stable option snapshots and explicit cache-only option reload | Same public Qt/Workspace boundary | `native.field_filter.narrow`, `mode_reset`, `stable_picker`, `refresh_picker`, both styles | Including the reset regression, 10/10 pass in 29.49 s. The completed build includes the production executable. |

Evidence: `2026-10-09-comparison-entry-regression` retains the complete package-test log, Qt reset scope log and real comparison lifecycle log. CI coverage/performance misses are diagnostics; the unchanged 95%/90% release coverage targets and performance requirements remain independently binding, as clarified in ADR 0035.

`2026-10-09-canonical-desktop-colima` contains original empty-state, Resources, ConfigMap-filter, Alerts and inspector frames plus side-by-side Resources/Alerts images (C# left, C++ right, no scaling/retouching). The reference and native snapshots differ; native also discovers additional kinds. These images show actual presentation differences, not exact Radar or all-theme parity. Native ConfigMap filtering finds 529 cached resources. Reference foreground interaction is inconclusive while another same-name application is running; that unrelated application was not stopped. The failed initial reference launch selected an executable-marked DLL; it is not successful comparison evidence. The corrected launch used the declared `Podlord.App` executable and an isolated profile. The completed owned comparison was cleaned up.

These corrections do not close the remaining release gates: the measured approximately 274-275 MB RSS exceeds the 250 MB limit; branch coverage remains 83.42% against 90%; installed cold-start/frame timing, complete matched view/theme comparisons and other-device installation remain unproven. The later reset-icon correction is not present in the earlier deployed-window screenshots.

## 2026-10-09 integrated native input and alarm activation

| Behavior | Public entrypoint | Test/evidence | Result |
| --- | --- | --- | --- |
| All registered native behavior and style cases after input repair | CTest, Qt UI/CLI/public APIs | `2026-10-09-integrated-input-functional/ctest.log` | 4323/4323 pass, 625.97 s, 12 processes |
| Nested separate-window menu and terminal touch keys receive real input after layout | Qt window pointer input | `native.window.menu_window_*`, `native.terminal.touch_*`; `animation-frame-regression.log` | 22/22 pass; pre-repair failures retained |
| Separate-window menu and narrow terminal keys on macOS | Cocoa windows with software rendering | `menu-window-cocoa-fixed-{Basic,Fusion}.log`, `terminal-touch-cocoa-fixed.log` | 3/3 pass; not Metal performance evidence |
| Alarm activation uses distinct visible/hidden eye icons and preserves toggle persistence, failure handling, keyboard/table navigation | Qt controls and rendered glyphs | `2026-10-09-alert-activation-parity/ctest.log`, `native-alert-activation.png` | 89/89 pass; toggle and glyph regressions failed first |

The test inventory contains 3187 `behavior` labels and 1136 additional `style-variant` labels. These are executed test cases, including input/asset rows, not 4323 independent product features. The functional run used Release, the official Qt SDK, and no coverage instrumentation. It overlapped the older instrumented run, so its duration is a test execution record, not an exclusive application performance benchmark.

The integrated test-input change initially exposed a missing direct `QSignalSpy` include and 46 input failures. The shared input helper now completes scheduled ancestor layout and waits for a real `afterAnimating` frame; it fails explicitly if that frame is unavailable. This retains the removal of unconditional one-second `frameSwapped` waits. The separate-window case also checks the public menu `triggered` signal before expecting a second window.

The older instrumented run passed its 2 preflight cases and 4273/4319 remaining cases before this repair. Its archived coverage is 97.42% lines and 83.33% branches, not coverage evidence for the later helper/icon changes. CI diagnostics do not waive the 95% line / 90% branch release gates. The latest exclusive Metal memory evidence still exceeds the 250 MiB RSS gate; no memory improvement is claimed here. Complete installed C#/C++ view/theme comparison and remaining device/startup/frame evidence are still open. The previously packaged private app predates these UI changes and is not relabeled as a current release.

## Which Settings And Theme Comparisons Were Captured On 2026-10-09?

| Requirement / scenario | Public boundary | Evidence / test | Result and remaining gap |
| --- | --- | --- | --- |
| Localized alarm matcher labels, grouped actions and compact Settings controls | Actual Qt editor, persistence and selected language | `native.alert.copyWhen_<de,ja>_<Basic,Fusion>`, action-row cases, `2026-10-09-visual-ui-regression/ctest-final.log` | Earlier integrated increment passes 4331/4331 executions in 622.64 s: 3191 behavior cases and 1140 style variants. This run precedes the subsequent Event-color and inspector-chrome changes. |
| Expand attributed sound credits without losing their real source links | Actual About controls | About credits cases and seven existing attribution URL cases | Pass in the integrated run; collapsed presentation preserves attribution and explicit navigation. |
| Every shared named theme, dark and light | Deployed C# and C++ applications, real isolated K3s resource corpus on Colima | `2026-10-09-visual-themes-64992/`: 38 `comparison-theme-*.png` pairs; `comparisons.json`, `index.html` | All 19 selected theme names and both variants were observed in the actual controls. Original full-window pixels are placed side by side, C# left and C++ right, without resizing, cropping or retouching. Appearance-page comparisons are not a complete view-by-theme cross-product. |
| Every Settings section | Deployed applications, real private fixture profiles | `2026-10-09-visual-settings-updated-55478/comparison-settings-*.png` | Nine actual section pairs captured: Alerts, Appearance, Diagnostics, Graphics, Privacy, Sources, Sync, Workspace, About. The matrix records presentation differences; it does not assert pixel equality. |
| Resources, Events, Ports and ConfigMap inspector pages before the latest inspector correction | Same installed-window boundary | `2026-10-09-visual-themes-64992/comparison-workspace-*.png`, `comparison-inspector-*.png` | Three workspace and five inspector pairs. These frames exposed the wide text-action row and unstyled inspector tabs, corrected by UI-INSP-01 through UI-INSP-04. They remain before-change evidence, not screenshots of the corrected inspector. |
| Normal, Warning, unknown and empty Event types in both event tables | Real Workspace/QML; only the external Kubernetes HTTP boundary is simulated | `native.ui.{filter,inspector}_event_color_{normal,warning,unknown,empty}_{Basic,Fusion}`; `event-colors-before.log`, `event-colors-after.log` | Twelve of sixteen executions fail before the shared semantic-color correction; all sixteen pass afterward. Empty types already used ordinary text. |
| Desktop and narrow inspector header, tabs, metadata and navigation | Real Qt pointer input and visible control geometry | `native.ui.inspector_related_chrome[_narrow]_{Basic,Fusion}` | Four executions fail before the layout correction and pass afterward. These supplement rather than replace YAML, Secret, related-table, port-forward and terminal behavior cases. |
| Delete remains visually distinct from Close and unknown-resource fallback | Rendered shipped KindGlyph | `native.glyph.trash` | Fails before adding the reference Trash geometry; passes afterward. |

The named fixtures use 512 ConfigMaps, 256 Secrets, 64 Deployments, 32 StatefulSets,
32 DaemonSets, 32 CronJobs, 32 PVCs, 64 Services, 16 ServiceAccounts, 8 Jobs and
3 Pods, plus Kubernetes system resources. Native discovery includes additional
kinds; two independently created sessions/snapshots are not an exact Radar-world
coordinate proof. The local node reported image-filesystem pressure; shared disk
contents were not pruned. No application performance claim is derived from this
loaded environment. Owned comparison apps, clusters and private profiles are
removed through the existing guarded runner; the user's application is untouched.

The subsequent complete run executes all 4352 cases in 639.47 seconds: 4350
pass and two existing `native.forward.terminal_<Basic,Fusion>` cases fail.
The compact port-forward icon retained its visibility guard but lost its enabled
guard. Restoring the same authoritative `canPortForward` binding corrects this
regression without changing transport. All 224 Forward and inspector-chrome
executions pass afterward in 90.43 seconds (`forward-state-fixed.log`). The full
run is retained as a failed-before-correction record, not relabeled all-green.
The inventory contains 3202 behavior labels and 1150 additional style variants.

The final SDK-pinned private package is in `2026-10-09-visual-parity-final-6333/`:
ZIP 32,459,896 bytes, installed regular-file logical sum 85,883,595 bytes;
dependency closure and strict local ad-hoc signature validation pass. Native
executable SHA-256 is
`c0c48247415ec5a1f5ea4bf2de1252f90826a0bd6b75a23b600b43aa694bccd0`.
The initial packaging invocation omitted the established portable dependency
prefixes and pulled Homebrew libraries; its retained failure is not successful
SDK-package evidence. The corrected invocation uses the same Qt, yaml-cpp and
OpenSSL roots as the functional build. Developer ID/notarization remain deferred.

Remaining presentation gates: final inspector framebuffer verification, primary
header/table sizing and default Event layout, matched-state Radar viewport,
complete populated/empty/error/selection and narrow/device view matrix. The
previously measured RSS and branch-coverage misses remain release blockers;
these UI changes neither waive those budgets nor establish new measurements.

## What Does The 2026-10-10 Matched Desktop Increment Prove?

Evidence directories below are under the existing private comparison
`release-evidence` root. Screenshots retain the actual full-window pixels;
compositions place C# left and C++ right without retouching or rescaling.

| Behavior | Public boundary and evidence | Result / remaining gap |
| --- | --- | --- |
| Current inspector and workspace arrangement with identical selected appearance | Installed apps against isolated real K3s; `2026-10-10-visual-matched-93762/`: nine view pairs plus Appearance confirmation | Both apps visibly selected Imperial Ledger / dark. ConfigMap Overview, fresh YAML, Events, Links, Values; multi-container Pod Logs; Resources, Events and Ports captured. Values are populated, not an initial loading frame. This is presentation evidence before the water-base correction, not pixel equality or every-theme coverage. |
| Reference blue water persists when animation is disabled | Actual Settings clicks and rendered Qt pixels; `native.ui.radar_water_background[_light]_{Basic,Fusion}` | All four executions fail before correction and pass afterward. Sirocco dark and Imperial Ledger light both retain `#061621` with no water movement or additional transport. |
| Water lifecycle, terrain, navigation and retained effects | Real Qt workspace with an external HTTP-boundary fake; `water-background-after.log`, `radar-theme-regression.log` in the matched directory | Focused set: 74/74 in 54.49 s. Expanded Radar set: 118/118 in 85.40 s, comprising 64 behavior labels and 54 style variants. These are two overlapping runs, not 192 unique scenarios. |
| Shipped palette catalog and canonical reference topology | Actual appearance controls plus the separately recorded reference resource projection; `appearance-reference-regression.log` | 62/62 in 4.30 s. This covers named palettes and deterministic renderer checks, not a full installed-window cross-product or equality of independently changing sessions. |
| Corrected water in the private shipped bundle | Real isolated K3s and installed apps; `2026-10-10-radar-water-desktop/comparison-workspace-resources.png`, confirmed Sirocco dark on both sides | Blue base is visible after the correction; native Settings disablement and the resulting static workspace have separate screenshots. Different catalog, session identity and camera still prevent an exact topology claim. |
| Interactive container shell, expansion and disconnect | Real local Kubernetes Exec; `2026-10-10-visual-parity-91617/terminal-shell.png`, `terminal-expanded.png`, observed-state records | Shell command returned the explicit success marker and `aarch64`; `stty size` changed from `11 121` to `45 167` on expansion. Disconnect re-enabled Connect and disabled Disconnect. The multiline paste confirmation was exercised. This is native-only terminal evidence, not a C# embedded-terminal comparison. |

The first `2026-10-10-visual-parity-91617` nine view pairs did not explicitly
confirm matching themes. They remain structural/transport evidence only and are
not used to claim color parity. The subsequent matched run exposed the water
root cause: the reference uses `PlRadarWaterBrush`, not its theme glass color.
The native correction changes four existing bindings, with no new renderer,
timer, setting or dependency. An empty cache has no idle-water animation.

The corrected SDK-pinned private package is in
`2026-10-10-radar-water-package/`; strict ad-hoc signature and dependency closure
checks pass. Download ZIP is 32,459,896 bytes; installed regular-file logical sum
is 85,883,595 bytes. Packaging preflight is not a release-readiness certificate.
All owned comparison apps, clusters and private profiles were removed through
the existing guarded runner; the user's running app and shared images were not
touched.

Remaining observed UI gaps: native read-only YAML has no reference-style syntax
colors or line numbers; Overview metadata lacks the reference's direct field
filter buttons; empty metric-column behavior, default ordering, typography and
pane proportions still differ. Full view/theme/error/narrow/device, memory and
coverage gates remain open. No new performance claim is inferred from the
loaded local Kubernetes fixture.

The complete suite was not rerun for this four-binding correction. Its earlier
failed-before-correction record and the focused port-forward follow-up above
remain unchanged; targeted green runs do not relabel that full run all-green.

## What did the inspector and cross-platform release pass establish (2026-10-10)?

Scope: `UI-INSP-05` and `UI-INSP-06`, existing modal confirmation safety, and reproducible native preflight execution. This closes the plain YAML presentation gap, not complete release parity.

| Behavior | Public entrypoint | Executable scenario |
| --- | --- | --- |
| Exact metadata filter preserves global search and inspected resource without API reads | Inspector field filter button | `native.ui.inspector_related_filter_{Basic,Fusion}` |
| Metadata filtering works through keyboard input | Focused field filter button and Space | `native.ui.inspector_related_filter_keyboard_{Basic,Fusion}` |
| Reference syntax colors and line numbers preserve plain-text copying | Rendered YAML and Copy YAML | `native.ui.inspector_yaml_presentation_{Basic,Fusion}` |
| Line numbers track the visible portion of a long document | Actual YAML wheel scrolling | `native.ui.inspector_yaml_presentation_scroll_{Basic,Fusion}` |
| Quoted scalars, comments, numbers, keywords and block scalars retain their text and colors | Fresh-only Edit, paste, Select All and Copy | `native.ui.inspector_yaml_presentation_draft_{Basic,Fusion}` |
| Metadata outside the first viewport remains available without a fetch | Overview wheel scrolling | `native.ui.table_overview_metadata` |
| Toggling an alarm tolerates replacement of the rendered row | Actual alarm toggle and persisted catalog reload | `native.alert_ui.reference_toggle_{Basic,Fusion}` |
| Removing successive contexts waits for the old modal to close; the next action also supports the keyboard | Sources Remove, confirmation, next Remove via Space | `native.sources_table.remove_all_{Basic,Fusion}` |
| Destructive confirmation settles with Cancel focused and no DELETE sent | Resource deletion preview at narrow width | `native.delete.narrow_{Basic,Fusion}` |

Evidence root: `$HOME/.local/share/podlord-comparison/release-evidence/2026-10-10-inspector-readiness`.

- Eight new presentation/filter executions failed before implementation. The final focused candidate passed 16/16; the draft scenario was added afterward and is not counted as failing-first evidence.
- Full instrumented execution completed its two preflights and 4,364 other entries: 4,365/4,366 passed. The sole failure was the metadata test reading a virtualized off-screen row. After correcting its real scrolling interaction, the instrumented affected rerun passed 5/5. This is not a second full run of the final modal changes.
- Final affected macOS Qt 6.11.2 run: 324/324 passed in 173.60 seconds. Final affected Linux ARM Qt 6.10.2 run: 336/336 passed in 171.88 seconds. Ten formerly unstable executions additionally passed five repetitions each. These sets overlap; do not add them as distinct scenarios.
- Linux reproduction first failed 20/98, including alarm test use-after-replacement crashes. Pointer input now waits for rendered, stable coordinates; keyboard input reacquires a recycled delegate instead of assuming the original object retains its identity. Tests wait for observable popup dismissal and focus. Modal dialogs explicitly acquire focus; source removal cancellation runs when hiding begins, not in a late closed callback.
- Current inventory: 4,366 executions, 3,206 `behavior` labels and 1,160 `style-variant` labels. Three Fusion variants were misclassified and are now marked correctly. No scenario was removed solely because an E2E test exists. CTest has no global serial or resource-lock registrations in this inventory; six-way execution already works locally. macOS CI has 90 minutes because the previous Intel job exceeded its 60-minute deadline.
- Fresh native coverage: lines 97.40% (passes 95%); branches 83.43% (fails 90%). Functional CI's diagnostic coverage policy does not waive the release gate. Remaining reachable branches still require evidence or a justified exclusion.
- Installed final private package passed all twelve isolated startup/metadata checks and three mutual-TLS checks: trusted, untrusted CA and untrusted client. Startup checks also verify that ambient kubeconfigs and authentication providers do not start implicitly.
- Final macOS ARM package: `$HOME/.local/share/podlord-comparison/release-evidence/2026-10-10-inspector-package-final`. ZIP 32,470,974 bytes; installed regular-file logical sum 85,911,211 bytes. Dependency closure and local ad-hoc signature passed. Executable SHA-256 `8e99d7ba35ccdde9f85e4d7c1996ac79eaa0548c709aad7bb3c98ba7cc0d2e0d`; ZIP SHA-256 `3af241fa364528f64a5e7e76f5fbac844661dd1fdf9fe636645c1c310fdd6fec`.
- Actual C#/C++ side-by-side images are retained under `2026-10-10-inspector-desktop`: appearance, Overview and fresh YAML, plus native filter application. Both use Sirocco Command/dark and the same real kube-system ConfigMap UID. These pictures precede the final modal-only changes; they are not a complete final-build image matrix. The owned real Kubernetes fixture, isolated profiles and comparison processes were cleaned up.
- Foreground performance with 5,000 resources over three sessions and retained logs: p95 filter 39.189 ms, sort 28.557 ms, cached tab 47.391 ms, cached inspector 24.415 ms; 60-second idle 1.486% of one core. These interaction and idle limits pass. Maximum RSS 293,175,296 bytes fails the 250,000,000-byte limit; warm growth was 2,064,384 bytes. The later alternate-render-loop attempt had no active foreground and provides no valid performance result. No renderer default was changed on that basis.

Still open: the RSS and branch-coverage gates; full view/theme/empty/error/narrow image matrix; typography, table geometry and YAML serialization/reference-navigation differences; direct UI-processing-per-frame and installed-startup performance evidence; clean-device runtime/dependency and license checks; Windows and real mobile-device evidence. Developer ID signing and notarization remain intentionally deferred for private distribution. Package generation, component coverage and selected screenshots do not establish complete release readiness.

### What does the conditional threshold review establish?

The accepted direction is to revise a threshold only after meaningful optimization has been exhausted. It does not waive functional failures, reachability analysis or evidence on the delivered artifact. No threshold is changed by this pass.

| Behavior | Public entrypoint | Regression or evidence |
| --- | --- | --- |
| Moving a session waits for pending view persistence without an enabled action that rejects the click | Separate-window button and tab menu after changing the filter | `native.window.filter_{Basic,Fusion}` checks disabled-while-saving, eventual enablement and restored filter in the new window |
| Request rate is measured at actual dispatch, not delayed TCP receipt | Public `requestStarted` telemetry and external HTTP server | `native.ui.limit` retains the 120/minute setting and requires at least 500 ms between dispatched requests |
| Destructive preview keeps Cancel focused after popup setup | Narrow and normal deletion previews | `native.delete.{narrow,preview}_{Basic,Fusion}`; pointer input waits for rendered stable coordinates |
| Port controls tolerate deferred layout and recycled cells | Public pointer actions, search and clipboard | Existing `native.forward.*` scenarios use the same rendered-input owner as the other tables |
| Failed rendered Radar actions expose their actual effect, geometry and pixels | Actual Qt rendered action | `native.ui.radar_animation_*`; macOS CI retains failure images instead of weakening the pixel assertions |

Evidence is retained under `$HOME/.local/share/podlord-comparison/release-evidence/2026-10-10-threshold-review-ci` and the sibling `2026-10-10-threshold-memory-{threaded,basic}` directories. CI run `38039810172` failed 15 native executions on each macOS architecture, two on Linux x86 and one on Linux ARM; the C# Kubernetes job passed. Affected local reproductions passed 19/19 without instrumentation and 21/21 with instrumentation at scale factor 1; these runs do not override the CI failures.

Two uninterrupted foreground Metal measurements used the same unmodified 5,000-resource, three-session profile. Threaded rendering: maximum RSS 269,041,664 bytes, warm growth 2,424,832 bytes, idle 1.490% of one core, p95 filter/sort/tab/inspector 41.034/30.936/48.177/24.459 ms. Basic rendering: maximum RSS 268,828,672 bytes, warm growth 3,620,864 bytes, idle 1.537%, p95 29.661/20.286/47.723/22.782 ms. Both fail only the measured absolute RSS gate. The roughly 0.08% RSS difference does not justify changing the renderer default or establish that other optimization is exhausted.

The threaded process diagnostic recorded approximately 98.3 MiB allocated in malloc zones, 36.3 MiB allocator fragmentation, 323.4 MiB resident graphics-owned mappings and 60.4 MiB IOSurface mappings. Physical footprint (539.4 MiB) is a different metric from process RSS and must not replace it silently. These diagnostics identify allocation and graphics investigation work, not proof of a leak or a specific Qt defect. Native macOS coverage remains 97.40% lines and 83.45-83.47% branches in that CI run. No reachable branch was excluded and no release gate was lowered.

The affected candidate passed 514/514 executions on macOS Qt 6.11.2 in 245.46 seconds and 514/514 on Linux ARM Qt 6.10.2 in 233.43 seconds. macOS filter/preset/column window actions, deletion focus and port search/copy additionally passed five repetitions of fourteen entries; the six Linux window filter/preset/column entries passed five repetitions after rebuilding the final test assertions. These sets overlap and are not additional distinct scenarios. An earlier overlapping macOS run lost one subprocess while its binaries were being relinked; it is not accepted as a final-candidate result.

The final private package in `2026-10-10-window-release-final` passed dependency closure, local ad-hoc signature verification, all twelve isolated startup/metadata cases using its actual Cocoa plugin, and all three mutual-TLS cases. Its ZIP is 32,471,861 bytes and installed regular files total 85,911,451 bytes. Executable SHA-256: `a0b517e648531f5db7d98168111c05d0276e40ab440bcd84a2a7b33cad081644`; ZIP SHA-256: `3670c50aa495ab2042db620fad64781641be15d96d70662941ba1d82f2bffcd5`. The first startup attempt incorrectly selected the development-only offscreen plugin; correcting the test platform, not adding that plugin to the release, produced the packaged results. Full cross-platform CI, new failure-image evidence and the remaining release gates are still required.

The final-candidate foreground run (`2026-10-10-threshold-memory-final`) measured p95 filter/sort/tab/inspector 44.448/31.068/46.083/23.994 ms and 60-second idle 1.521% of one core, with foreground retained. Peak RSS was 268,435,456 bytes and warm growth 1,736,704 bytes. Interaction, idle and measured bounded-growth checks pass; absolute RSS still fails. The final diagnostic-only Radar/dispatch rerun passed 13/13 locally. Run `38047661045` was rejected before any job because its new diagnostic-path setting used `runner.temp` at job environment scope; the correction publishes the runtime path through `GITHUB_ENV` from the setup step. YAML parsing alone did not detect this GitHub expression-context error.

### What did the shared-table and healthy-cache pass establish on 2026-10-10?

- Shared `ResourceGrid` body text now uses normal-weight 13 px text while column headers remain bold. The two public rendered-control typography cases failed before the change (body weight 600) and passed afterward. The affected table/filter/inspector selection ran **186/186** passing executions; these are overlapping behavioral and style variants, not 186 new scenarios.
- Healthy canonical resource summaries carry the default activity/severity values. Publication changes only values that actually differ, retaining implicitly shared JSON objects for unchanged healthy rows. Problem classification, restart thresholds, activity deadlines and API scheduling remain owned by the existing implementation; no threshold was relaxed.
- The affected health/metrics/age/Radar selection passed **235/235** on macOS arm64 and **237/237** on Linux arm64, including both typography styles on Linux. All **118/118** Radar executions passed after adding setup-stage evidence. All twelve action cases also passed in the instrumented macOS coverage build, and an earlier focused run passed each action three times.
- The outline assertion now captures its exterior pixels rather than only the resource's interior selection border. It still requires a visible difference from the baseline and a stable subsequent frame. The animation target is selected by an exact cached filter; background-water disabling is checked through the existing Settings action. No production animation was disabled to satisfy a test.
- The previous full CI run `38047798595`, revision `4c3e4904cd6e06f216d42e91250e3ca414462250`, passed both Linux architectures and the C# k3d job, but failed both macOS jobs. Each macOS job had twelve action-setup failures; arm64 also failed two port UI cases and inspector scrolling, while Intel also failed three explicit-source startup cases. These full-run failures are **not** closed by the focused local results. Subsequent macOS failures retain setup screenshots and a serial reproduction log without changing the failing job's exit status.
- That arm64 report measured **97.40% line / 83.49% branch** coverage. The 95% / 90% release requirement remains unchanged; diagnostic reporting is not a coverage waiver.
- Allocation-stack recording found full-resource copies during health decoration. Its instrumentation overhead and globally inherited logging exclude that run from performance-gate evidence. QSG timing logging is likewise diagnostic, not a replacement for the uninstrumented public-boundary protocol or the complete local-frame budget.
- Nine Settings screenshot pairs were captured against an actual disposable Kubernetes cluster and composed side by side without changing their pixels. The preserved C# bundle still contained Graph navigation and theme intensity, so those images are explicitly a **historical-bundle comparison**, not current-source parity evidence. A separate C# bundle was subsequently published from the current source without replacing the user's running bundle.
- The native desktop accessibility snapshot exposed only a window after opening the loaded context despite working visible controls. This remains an accessibility/integration observation requiring reproduction, not a claim of assistive-technology parity.
- Evidence roots: `2026-10-10-table-typography`, `2026-10-10-table-typography-desktop`, `2026-10-10-allocation-diagnosis`, `2026-10-10-table-frame-profile`, `2026-10-10-current-reference` and `2026-10-10-shared-health-package` beneath the local release-evidence directory. The first desktop runner removed its owned apps, labeled Kubernetes container, volumes and private profiles. The pre-existing user app and shared Docker image were retained.

## What did the bounded-radar and workspace-typography pass establish on 2026-10-10?

| Behavior / scenario | Public entrypoint | Executable evidence | Remaining gap |
|---|---|---|---|
| Ordinary workspace/table text stays normal-weight while navigation and headers stay bold | Rendered Qt workspace, Basic and Fusion | Extended `native.filters.typography_*`; 233 macOS Settings/layout/shell checks and 48 appearance checks passed | This is not the complete image/theme/device matrix |
| Large radar does not publish every tile as an accessible button | Public Qt accessibility tree after paginated external API discovery of 1,001 resources | `native.ui.radar_accessibility_overview_{Basic,Fusion}`; all four new overview/focus cases failed before tile suppression and passed after it | Actual screen-reader operation is separate |
| Radar has an explicit named grouping, with count and navigation guidance | Public Qt accessible role/name | All four overview/focus cases failed before the grouping role and passed after it; Linux's 16 overview/animation cases passed | Packaged Cocoa grouping still needs confirmation |
| First and last map targets remain accessible without network reads | Home/End in the rendered radar and public accessible button names | `native.ui.radar_accessibility_focus_{Basic,Fusion}`; 1,001 resources retained, request count unchanged | Hover plus independent keyboard focus can expose two relevant targets |
| Existing pan/zoom, topology, water, animation and reduced-motion behavior | Rendered radar, real input and frame capture | Broader macOS selection: 130/130 passed before the grouping-only addition; corrected animation rule: 12/12 uninstrumented and 16/16 instrumented overview/animation checks passed before that addition | Full CI with the final grouping remains outstanding |
| Rule criteria remain a nested list across compilers | Public `Alerts::saveRule` and rendered animation | Prior macOS CI rejected all 12 radar-animation test rules as malformed; the new test explicitly constructs the nested QVariant; local macOS and Linux animation cases passed | The next macOS Clang 17 CI run must confirm the correction |
| Failure reproduction selects the original scenario | Real CTest registrations and recorded failed names | A disposable CTest run verified name-selected repetitions; workflow lint passed; first-run failure remains fatal | A repetition passing does not erase the initial failure |
| Current-source Settings comparison | Deployed native/reference apps against one owned K3s cluster | Nine lossless C#/C++ Settings pairs captured and inspected in `2026-10-10-accessible-current-desktop`; native screenshot executable SHA-256 `fb76856bac76e62ca2872bb69c27bc865feb6acfca5b8a52bc901372e43613f6` | These frames precede the grouping-only addition. Radar/corpus counts, tab row, form arrangement and source-table columns differ; image parity is not closed |
| Extra Resources/Events exploration | Same owned desktop run | Two additional lossless pairs retained | Native inspector was open while the reference inspector was closed: not a matched-state parity gate |
| Remaining CI failures | Full native public-boundary suite on macOS ARM/Intel and Linux x86 | Run `38051923690`: inspector scroll failed on both Macs; port copy also failed on ARM; draft-close Space failed on Linux x86. Those four registrations passed three consecutive local repetitions | Reproduction is not a root-cause fix; cross-platform full-suite reliability remains open |
| Coverage | Full instrumented suite | Latest macOS report: 97.40% lines, 83.51% branches | The mandatory 90% branch gate is not met and has not been lowered |

The owned comparison apps, cluster and volumes were cleaned up after capture;
the user's already running reference application and shared verification image
were retained. The final grouping package passed dependency/signature inspection
at 32,472,662 compressed bytes and 85,911,451 installed bytes. This establishes
local packaging only, not clean-device startup, complete parity or release readiness.

### What did the final radar, selection and 275 MB checks prove?

- `native.ui.inspector_scroll`: replacing an arbitrary 30 ms sleep with a public rendered-frame wait, viewport readiness and correctly positioned wheel input passed ten consecutive macOS executions. Adding only a timestamp was insufficient; that failed repetition is retained in `wheel-timestamp-repeat.log`. The product's scroll-restoration code was not changed.
- `native.forward.ports_*`: all 66 cases passed on macOS and each passed three times on Linux. Selecting a forward row refreshes exactly its inspector target; copy assertions now start after that explicit request and still require copying itself to send no request. The failing-first counter race and prior 70/71 batch remain historical evidence, not current failures.
- `macos-inspector-radar-ports-final.log`: the actual regex selected 246 inspector/radar/typography cases, all passed. Port cases are recorded separately in `macos-ports-final.log`; they must not be inferred from the misleading earlier log filename.
- `linux-selection-scroll-regression.log`: 18 radar-animation/accessibility/scroll/Space cases passed. The old Linux Space failure also passed three later repetitions alongside the scroll case; its remote CI outcome remains separate.
- `2026-10-10-macos-275mb-gate/measurements.jsonl`: all runtime gates passed with an explicitly reported 275,000,000-byte macOS ceiling, 261,799,936-byte peak RSS, 1,376,256-byte warm growth, and 1.4882 percent idle CPU over 60,002 ms. Filter/sort/tab/inspector p95 was 40.28/29.46/45.08/24.72 ms. This is not installed startup, per-frame work or mobile evidence; original 250 MB failures are retained.
- `2026-10-10-cocoa-radar-overview`: the packaged app's real macOS accessibility tree exposed the named 1,842-resource radar grouping. Home and End exposed ComponentStatus/controller-manager and StorageClass/local-path respectively; Enter opened that last resource. All map tiles remained rendered. This is not screen-reader certification.
- The same directory contains fresh untouched Resources/Events C#-left/C++-right composites with both inspectors closed. They replace neither the full matrix nor matched-corpus radar evidence: discovery counts differ (C# 1,411, C++ initially 1,842), table columns/order differ, and the Events capture retains a focused row-limit tooltip. Nine earlier current-source Settings pairs remain separately identified.
- The real packaged terminal ran `printf`, displayed its output, opened vi, resized its PTY, accepted Escape and the separately observed `:q!`, returned to the shell, reported `45 167` through `stty size`, and interrupted `sleep 30` with physical Control-C. Expansion/collapse retained the same stream. A too-fast combined Escape/quit automation sequence did not quit vi; its failed frame is retained and is not counted as a pass.
- Ten additional Basic/Fusion shifted-colon, exclamation, uppercase, Unicode and Alt-colon entrypoint checks passed before any production change. The pinned VT implementation already handles those inputs correctly, so no speculative encoder fix was made.
- Closing the test session removed its terminal surface and inspector, but an independent Kubernetes process listing still showed the remote `/bin/sh`. The stream-close requirement does not guarantee remote-process termination. The remote lifecycle choice remains explicitly open; no blind process-killing behavior was added.
- The desktop runner completed and removed its owned reference/native processes, K3d stack and private profiles. The original C# app and shared verification image were not cleanup targets.
- Release remains blocked by the outstanding remote-process lifecycle choice, complete public image/device parity and the mandatory branch-coverage gate. The latest downloaded macOS-arm coverage report is 97.40 percent lines and 83.48 percent branches; macOS-intel previously reported 83.51 percent branches. Neither is a 90 percent branch pass.
- `macos-terminal-final.log` and `linux-terminal-final.log`: all 226 terminal boundary cases passed per platform, including the ten shifted-input additions. These full runs preceded the subsequent truthful disconnect-caption change. Its existing `connect` cases failed 2/2 against the old overclaim before the caption fix; the focused follow-up covers connection, narrow controls, expansion return and disconnected controls.
- `native.terminal.shifted_alt_unicode_{Basic,Fusion}` failed 2/2 before the UTF-8 correction: expected `1bc384`, received `1bc4` for Alt+U+00C4. The shared keyboard sender now preserves the existing ESC-prefix semantics and uses its existing bounded send path for UTF-8; the VT dependency and Control/named-key semantics are unchanged. This is distinct from the earlier ten already-correct shifted-input cases and the vi automation timing issue.
- The corrected disconnect guidance passed all eight focused connection/narrow/expansion/disconnected-control cases on both macOS and Linux, after the two failing-first caption checks. The desktop screenshots predate this wording change and the Alt+Unicode correction.
- `macos-terminal-unicode-final.log` and `linux-terminal-unicode-final.log`: all 228 terminal cases passed on each platform after the Alt+Unicode fix and truthful disconnect guidance. The earlier 226-case runs remain identified with their original scope. No new protocol, fallback, dependency or process-termination behavior was introduced.
- `2026-10-10-terminal-unicode-real-k3d` and `terminal-unicode-real-k3d.log`: the current real local Kubernetes run passed `real_shell`, `real_vi`, `real_control_vi`, `real_interrupt`, `real_touch_interrupt`, and the terminal/isolated-forward window-transfer scenario. The owned fixture inventory contained 1,051 requested resources (512 ConfigMaps, 256 Secrets and real workload/service objects). Its shell scenarios exited explicitly. The wrapper completed and cleaned its owned K3d cluster and private profiles; this does not establish guaranteed remote termination on an arbitrary disconnect.
