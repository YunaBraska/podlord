# Changelog

All notable changes to Podlord are documented here.

Podlord uses date-based release tags in the form `YYYY.M.D`.

## [Unreleased]

### Added

- Runtime diagnostics in Settings now show cache footprint, process memory, managed heap, GC heap, UI rows, radar blocks, audit rows, request pressure, and thread count.
- Diagnostics table cells support full-value hover and right-click/long-press copy, matching the other data tables.
- Added slow-loading tab/radar/health/inspector regression coverage plus a real k3d-backed UI workflow test for multi-session loading and switching.
- Added cache-only redraw/performance regression tests for unchanged refreshes, local filters, scoped namespace caches, and tiny offscreen cache deltas.
- Added real k3d-backed UI coverage for deployment/radar/inspector rendering, limited RBAC namespace scope, and native port-forward start/stop behavior.

### Changed

- Radar and idle screensaver rendering now use custom drawn layers instead of thousands of generated controls, reducing visual churn and memory pressure.
- Radar source selector now shows only sources whose session is not already open in any tab or detached window.
- Session tab switches now restore rendered table state from cache, defer heavier secondary panels, preserve quick-search state, and skip immediate Kubernetes refreshes while the list cache is fresh.
- Open session tabs/windows now have lightweight per-session workspace view models while still sharing the same Kubernetes cache service.
- Restored session tabs now show their resource table first and defer cached graph, events, pulse, and radar restoration to a background UI pass.
- Source tab switches no longer synchronously repaint rows while restoring the tab's saved filter, and invisible graph/events panes are materialized only when opened.
- Session tabs now keep their own radar zoom/pan/size state, refresh/restore lifecycle, Kubernetes request queue, request pacing, and cache pipeline.
- Inactive session tabs start loading in the background when opened, so switching to them can attach to existing work instead of starting over.
- Active session/source tabs now use the same highlighted command styling as the main navigation.
- Session tabs no longer show the extra deterministic source color stripe beside the tab title.
- Kubeconfig source lists now sort by most recent source activity, using last opened time or last imported/updated time.
- Tab/cache restores now load cached snapshots off the UI thread and defer filter option rebuilding until after the visible table can render.
- Background refreshes now render visible rows first, then update heavier secondary views and filter option lists at background priority.
- Footer status updates are deduplicated so unchanged sync/progress text does not keep invalidating the UI.
- Hot command panels use lighter non-tiled surfaces and debug trace logging is limited to debug builds.
- Kubernetes list fan-out now allows up to six queued requests at once while still respecting pacing, backoff, hard request limits, and telemetry.
- Identical in-flight Kubernetes cache warm-ups now join one shared request sweep instead of duplicating work across tabs/windows.
- Session tab/window pipelines now share one live cache and in-flight warm-up ledger while keeping separate request pacing, so detached windows and loading tabs can reuse freshly loaded data immediately.
- Pod and node metrics are fetched in parallel during pulse enrichment.

### Fixed

- Cached update checks are now revalidated against the installed app version so upgraded users do not keep seeing a stale download button.
- Cached non-empty session restores no longer keep the startup/loading state or radar screensaver active after resources are available.
- In-flight refreshes and async cache restores from one tab no longer leak loading state or stale rows into another active tab.
- Restored session tabs now replace radar and pulse content immediately instead of briefly showing the previous tab's minimap.
- Radar auto-follow now refreshes entering viewport blocks during and after zoom so off-screen targets do not remain invisible until the next sync.
- Switching session tabs no longer restores alert state twice or restarts active-view radar/table pulse animations for rows that were already visible.
- Opening an already warm session in another tab/window reuses the shared completed sync state instead of duplicating the same Kubernetes sweep.
- Partial cached rows shown during tab restore no longer mark the session as loaded; Podlord keeps refresh state visible and continues the background sync until the session cache is actually warm.
- Session tabs with partial cache now keep the loading health bar active instead of showing a completed health state while the first sync is still running.
- First load no longer renders partial resource rows or radar blocks before the first session sync completes.
- Namespace-scoped refreshes now render rows from the scoped cache instead of reading only the broad all-namespace cache key.
- Background tab refresh failures now record diagnostics on the tab's own request pipeline instead of the currently active tab.
- Pending secondary-view restores are now cleared when the user switches tabs before the posted restore work runs.
- Disposing the app view model now stops active port-forward tasks so failed tests and closing windows do not leave local listeners behind.
- Window titles now include the currently selected session.
- Diagnostics cache rows no longer truncate important values without a readable hover/copy path.
- Radar item clicks, hover hit testing, and custom layer tests now cover the drawn radar path.
- k3d integration setup no longer tries to own built-in default service accounts and now retries node readiness while the apiserver finishes booting.

## [2026.6.19] - 2026-06-19

### Added

- About tab in Settings with a randomly rotating short text block, the project manifesto, donation links (GitHub Sponsors, Buy Me a Coffee, Ko-fi, Liberapay), star repo, and create issue shortcuts.
- Inspector summary now shows the resource creation timestamp alongside age in local time.
- Three-state column pin (auto, pinned, hidden) replaces the global auto-hide setting and is exposed through the column header context menu with a lock icon for pinned columns.
- Local time zone formatting for human timestamps with offset fallback for unknown zones.
- `.github/FUNDING.yml`, `CODE_OF_CONDUCT.md`, pull request template, and shared package metadata (`Directory.Build.props`).
- Rule-based alert editor with locked default rules, custom matcher groups, color/animation/zoom actions, and alert-specific sound selection.
- Bundled audio catalog with searchable CC0/OSS sound choices, attribution, source links, mute control, and priority queued alert playback.
- Release automation for cross-platform desktop archives.

### Changed

- Window-lifetime event handlers (`ViewModel.PropertyChanged`, pulse strip routed events, YAML/log editor pointer events, source row `PropertyChanged`) are now unsubscribed on window close.
- Pulse layer aggregation rewritten as a single pass over the row collection.
- Audit sweep: DataGrid parity (named grids, EventGrid sort, FocusedEvents `SortMemberPath`, relationships Link label), localized tooltips and menu actions, removed undefined brush keys.
- 33 trivial click handlers collapse to expression bodies and single-line dispatches.
- Release automation now starts from `main`, tests first, packages every supported runtime, then creates the date tag and GitHub release.
- Release assets now include SHA256 checksums.
- Release assets now cover Linux glibc, Linux musl, Windows, and macOS across supported x64, x86, arm, and arm64 runtimes.
- Built-in activity/problem radar behavior is represented by default alert rules so users can enable, disable, duplicate, and extend the same mechanics.
- k3d integration test bootstrap now installs pinned k3d and kubectl versions when missing.
- Public repository documentation and release packaging were cleaned up for open-source use.

## Initial Desktop Preview - 2026-06-12

### Added

- Native Avalonia desktop shell.
- App-owned kubeconfig source import and snapshot storage.
- Flat resource explorer with filters, sorting, column resizing, column ordering, and saved filters.
- Deterministic radar view with zoom, pan, selection, and optional water animation.
- Resource inspector with overview, YAML, events, links, logs, ConfigMap/Secret values, delete action, and port-forward action.
- Cache-first Kubernetes API service with request queueing, backoff, audit log, and metrics enrichment.
- Native Kubernetes port forwarding through the Kubernetes streaming API.
- k3d-backed integration tests and coverage gate.

[Unreleased]: https://github.com/YunaBraska/podlord/compare/2026.6.19...HEAD
[2026.6.19]: https://github.com/YunaBraska/podlord/releases/tag/2026.6.19
