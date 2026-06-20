# Changelog

All notable changes to Podlord are documented here.

Podlord uses date-based release tags in the form `YYYY.M.D`.

## [Unreleased]

### Added

- Runtime diagnostics in Settings now show cache footprint, process memory, managed heap, GC heap, UI rows, radar blocks, audit rows, request pressure, and thread count.
- Diagnostics table cells support full-value hover and right-click/long-press copy, matching the other data tables.

### Changed

- Radar and idle screensaver rendering now use custom drawn layers instead of thousands of generated controls, reducing visual churn and memory pressure.
- Footer status updates are deduplicated so unchanged sync/progress text does not keep invalidating the UI.
- Hot command panels use lighter non-tiled surfaces and debug trace logging is limited to debug builds.

### Fixed

- Diagnostics cache rows no longer truncate important values without a readable hover/copy path.
- Radar item clicks, hover hit testing, and custom layer tests now cover the drawn radar path.

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
