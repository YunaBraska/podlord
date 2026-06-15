# Changelog

All notable changes to Podlord are documented here.

Podlord uses date-based release tags in the form `YYYY.M.D`.

## [Unreleased]

### Added

- Rule-based alerts are planned as the next major feature area.
- Release automation for cross-platform desktop archives.

### Changed

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
