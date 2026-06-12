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

## Commit Style

Use concise semantic commits:

```text
feat: add resource alert rules
fix: preserve inspector selection after refresh
docs: document release packaging
test: cover namespace-scoped metrics fallback
```
