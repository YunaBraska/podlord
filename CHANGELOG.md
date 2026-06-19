# Changelog

All notable changes to Podlord are documented here.

Podlord uses date-based release tags in the form `YYYY.M.D`.

## [Unreleased]

### Added

- About tab in Settings with a randomly rotating short text block, the project manifesto, donation links (GitHub Sponsors, Buy Me a Coffee, Ko-fi, Liberapay), star repo, and create issue shortcuts.
- Inspector summary now shows the resource creation timestamp alongside age in local time.
- Three-state column pin (auto, pinned, hidden) replaces the global auto-hide setting and is exposed through the column header context menu with a lock icon for pinned columns.
- Local time zone formatting for human timestamps with offset fallback for unknown zones.
- `.github/FUNDING.yml`, `CODE_OF_CONDUCT.md`, pull request template, and shared package metadata (`Directory.Build.props`).
- Rule-based alerts are planned as the next major feature area.
- Release automation for cross-platform desktop archives.

### Changed

- Window-lifetime event handlers (`ViewModel.PropertyChanged`, pulse strip routed events, YAML/log editor pointer events, source row `PropertyChanged`) are now unsubscribed on window close.
- Pulse layer aggregation rewritten as a single pass over the row collection.
- Audit sweep: DataGrid parity (named grids, EventGrid sort, FocusedEvents `SortMemberPath`, relationships Link label), localized tooltips and menu actions, removed undefined brush keys.
- 33 trivial click handlers collapse to expression bodies and single-line dispatches.
- Release automation now starts from `main`, tests first, packages every supported runtime, then creates the date tag and GitHub release.
- Release assets now include SHA256 checksums.
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

[Unreleased]: https://github.com/YunaBraska/podlord/commits/main
