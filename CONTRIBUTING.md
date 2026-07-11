# Contributing

Thanks for helping improve Podlord.

## Development Setup

Podlord uses the .NET SDK pinned in [global.json](global.json).

```sh
scripts/bootstrap-dotnet.sh
.tools/dotnet/dotnet restore Podlord.slnx
.tools/dotnet/dotnet run --project src/Podlord.App/Podlord.App.csproj
```

Use the local SDK when available so builds match CI:

```sh
.tools/dotnet/dotnet test tests/Podlord.Core.Tests/Podlord.Core.Tests.csproj
```

## Test Suite

Run the full suite before opening a pull request:

```sh
scripts/test.sh
```

The full suite creates a disposable k3d cluster. It is intentionally slower than pure unit tests because Kubernetes behavior is the thing being proved.

Coverage gates:

- Line coverage: 95%
- Branch coverage: 80%

The coverage gate targets domain/runtime behavior. Thin Avalonia presentation adapters and native UI/audio wrappers are excluded from the numeric gate and should be covered with focused behavior or layout tests when they carry logic.

## Architecture Rules

- `Podlord.Core` owns persisted state, kubeconfig import, filters, health, and command-risk classification.
- `Podlord.Kubernetes` owns Kubernetes API access, auth, metrics, logs, request queueing, and native port forwarding.
- `Podlord.App` owns Avalonia UI state and presentation.
- UI code may request operations, but it must not bypass core or Kubernetes service boundaries.
- Never open ad-hoc Kubernetes HTTP clients outside the service layer.
- Never log kubeconfig content, tokens, certificates, or secret values.

## Pull Request Checklist

- Add or update tests for behavior changes.
- Keep UI changes keyboard-accessible and readable in dark and light themes.
- Avoid adding dependencies unless they clearly reduce risk or complexity.
- Update documentation when a user-visible workflow changes.
- Keep release artifacts, kubeconfigs, and local IDE state out of git.

## Documentation Rules

- README is the 30-second index: what Podlord is, how to start, and where to look next.
- One doc answers one question. Extend an existing table row before creating another file.
- Keep volatile values out of prose when the app, test scripts, or release workflow can print them.
- Reference docs describe current behavior. Planned work belongs in [doc/ROADMAP.md](doc/ROADMAP.md) or GitHub issues.
- Prefer deleting duplicated text over adding clarifying copies.

## Commit Style

Use concise semantic commits:

```text
feat: add resource alert rules
fix: preserve inspector selection after refresh
docs: document release packaging
test: cover namespace-scoped metrics fallback
```
