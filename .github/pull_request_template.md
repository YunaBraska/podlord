## Summary

<!-- One paragraph: what changes, why now. -->

## Changes

<!-- Bullet list of the concrete edits. -->

## Test plan

- [ ] `dotnet test tests/Podlord.Core.Tests/Podlord.Core.Tests.csproj`
- [ ] `dotnet test tests/Podlord.App.Tests/Podlord.App.Tests.csproj`
- [ ] `dotnet test tests/Podlord.App.LayoutTests/Podlord.App.LayoutTests.csproj`
- [ ] `dotnet test tests/Podlord.Kubernetes.Tests/Podlord.Kubernetes.Tests.csproj` (skip K3d if docker not available)
- [ ] Manual smoke check on macOS / Linux / Windows where relevant.

## Checklist

- [ ] No personal kubeconfigs / cluster credentials touched by tests; only K3d.
- [ ] No emojis or AI mentions in commits or code.
- [ ] New user-facing strings flow through `PodlordLocalizer`.
- [ ] If you changed a DataGrid, the column-plaque header pattern is preserved.
