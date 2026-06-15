# Podlord

> Rule your clusters before they rule you.

[![CI](https://github.com/YunaBraska/podlord/actions/workflows/ci.yml/badge.svg)](https://github.com/YunaBraska/podlord/actions/workflows/ci.yml)
[![Release](https://github.com/YunaBraska/podlord/actions/workflows/release.yml/badge.svg)](https://github.com/YunaBraska/podlord/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Podlord is a native desktop Kubernetes operations console for people who want a fast, flat, cache-first view of their clusters instead of another namespace tree with a YAML drawer stapled to it.

It is built with C#/.NET, Avalonia UI, and a direct Kubernetes API client. Normal app operations do not depend on a preconfigured shell context and do not call `kubectl`; kubeconfigs are imported into Podlord-owned snapshots so sessions stay explicit and repeatable.

## Screenshots

The previews below use sanitized demo data.

![Podlord resource explorer](docs/screenshots/resource-explorer.svg)

![Podlord inspector and settings](docs/screenshots/inspector-settings.svg)

## Highlights

- Flat resource explorer across namespaces by default
- Multi-source kubeconfig import from home config, custom file, directory scan, pasted YAML, or generated k3d config
- Cache-first Kubernetes reads with request queueing, backoff, API audit log, and configurable hard request cap
- Sortable, resizable, reorderable, and hideable resource/event tables
- Searchable multi-select filters with exact, contains, starts-with, ends-with, regex, numeric, and duration expressions
- Radar view with deterministic resource island layout, panning, zooming, selection, activity markers, and optional water animation
- Resource inspector with overview metrics, YAML editing/apply, related events, links, logs where supported, ConfigMap/Secret key tables, delete actions, and native port forwarding where supported
- CPU and memory usage from `metrics.k8s.io`, including namespace-scoped fallback when cluster-wide pod metrics are forbidden
- Native cross-platform port forwarding through Kubernetes streaming APIs; `kubectl` is not required for the app path
- Secret values are hidden by default, redacted from YAML output, and copied only through explicit user action
- Localized application chrome with English fallback
- Disposable k3d integration tests for real Kubernetes behavior

## Install

Download the latest archive for your platform from [GitHub Releases](https://github.com/YunaBraska/podlord/releases).

Release assets are built for:

| Platform | Architectures | Asset |
|---|---:|---|
| macOS | arm64, x64 | `.app` bundle inside `.zip` |
| Linux | x64, arm64 | portable `.tar.gz` |
| Windows | x64, arm64 | portable `.zip` |

Each release also includes `SHA256SUMS` for archive verification.

macOS builds are currently unsigned. Right-click Open may be required until signing and notarization are configured.

## Run From Source

Podlord pins its SDK in [global.json](global.json). If you do not have a matching .NET SDK installed, bootstrap the local toolchain:

```sh
scripts/bootstrap-dotnet.sh
```

Run the app:

```sh
.tools/dotnet/dotnet run --project src/Podlord.App/Podlord.App.csproj
```

Start with a specific kubeconfig:

```sh
.tools/dotnet/dotnet run --project src/Podlord.App/Podlord.App.csproj -- /absolute/path/to/kubeconfig
```

The app imports kubeconfigs into its own store. The original file is not modified by normal use.

## Test

```sh
scripts/test.sh
```

The test script:

1. Ensures Docker or Colima is available.
2. Installs pinned k3d and kubectl versions into `.tools/bin` when missing.
3. Creates a disposable k3d cluster.
4. Runs the .NET test suite with coverage.
5. Enforces coverage gates.

Current gates:

- Line coverage: 95%
- Branch coverage: 80%

The k3d scenario map is documented in [doc/spec/k3d-test-map.md](doc/spec/k3d-test-map.md).

## Build Release Archives Locally

```sh
scripts/publish.sh all
scripts/publish.sh linux-x64
scripts/build-macos-app.sh osx-arm64
```

Supported runtime identifiers:

- `osx-arm64`
- `osx-x64`
- `linux-x64`
- `linux-arm64`
- `win-x64`
- `win-arm64`

## Release Flow

Merging to `main` runs the release pipeline:

1. test with the k3d-backed suite
2. create a date version like `2026.6.15`
3. build platform archives for Linux, macOS, and Windows on x64/arm64
4. update the changelog release section
5. push the release commit, create the tag, and publish the GitHub release

Manual workflow dispatch is available for dry runs or a custom date tag, but normal releases are branch-driven.

## Project Layout

```text
src/Podlord.Core        Domain model, settings, kubeconfig import, filters, health, command risk model
src/Podlord.Kubernetes  Kubernetes API adapter, auth, metrics, resource details, logs, port-forwarding
src/Podlord.App         Avalonia desktop shell, radar, tables, filters, inspector, themes
tests/                  Public-boundary behavior tests and k3d integration tests
doc/adr/                Architecture decision records
doc/spec/               Product, roadmap, and scenario notes
doc/design/             Design system notes and assets
```

## Security And Privacy

- Telemetry is disabled by default.
- Kubeconfig contents, tokens, certificates, and raw secret values are not logged.
- Imported kubeconfigs are stored as app-owned snapshots.
- Secrets are displayed as metadata/key lists first; values require explicit reveal/copy.
- Kubernetes RBAC failures are surfaced as visibility/freshness states instead of being hidden.

See [SECURITY.md](SECURITY.md) for vulnerability reporting.

## Roadmap

The next major feature area is rule-based alerting:

- user-defined alert rules over resources, fields, filters, events, and metrics
- reusable default rules that replace today’s built-in activity/problem behavior
- custom radar animations for matched rules
- optional error, warning, and info sounds
- import/exportable rule presets
- per-rule enable/disable controls

See [doc/ROADMAP.md](doc/ROADMAP.md) for the full plan.

## Contributing

Pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before sending one. The short version: keep behavior explicit, test through public boundaries, do not leak secrets, and do not bypass the Kubernetes request queue.

## License

Podlord is licensed under the [MIT License](LICENSE).
