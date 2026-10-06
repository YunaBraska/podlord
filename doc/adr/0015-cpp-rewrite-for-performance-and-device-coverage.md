# 0015 C++ Rewrite For Performance And Device Coverage

## Status

C++/Qt Quick rewrite direction, desktop/mobile platform families, and tightened performance/package-size targets accepted on 2026-10-02. A browser application is excluded. Native implementation and isolated C#/C++ desktop comparison are in progress. Module/distribution obligations, exact OS/architecture support, complete functional and visual parity, and release gates remain to be resolved and evidenced; the implementation is not release-ready.

## Context

The intended next implementation is a C++ rewrite after the specification interview, motivated by responsiveness and broad device coverage with minimal platform-specific wrappers. The current .NET/Avalonia implementation remains the existing reference described by ADR 0001; its framework-specific dependencies are not automatically retained in the replacement.

## Decision

Use RWT-001 through RWT-015 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-cpp-rewrite-direction) as the owning rewrite direction. Retain the confirmed behavior, cache-first boundaries, authentication safeguards, lifecycle rules, and executable acceptance criteria. The interview is complete; resolve any remaining dependent scope details before implementing their behavior.

Use Qt for macOS, Windows, Linux, Android, and iOS rather than create a browser application. Exclude embedded browser rendering and the previous runtime from the distributed replacement. Keep package dependencies restricted to the supported feature set. The language and toolkit choices do not prove responsiveness or authorize one wrapper per platform; performance and compactness remain measured outcomes.

## Open Choices And Evidence

- Desktop, Android, and iOS are required platform families; a browser version is excluded. Exact supported OS/architecture combinations still require a tested release matrix. Framework support does not establish that every Kubernetes authentication, file, networking, or port-forward capability works identically on every target.
- Qt Quick is accepted as the shared UI approach, with [desktop, Android, and iOS support](https://doc.qt.io/qt-6/supported-platforms.html). [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html) uses Qt's own presentation engine, not browser rendering. Keep QML thin and presentation-focused; data models, cache, Kubernetes integration, and business logic belong in C++ rather than a second application implementation in QML.
- The project currently uses the MIT license. Selected modules and distribution paths must be checked against their actual obligations; [Qt's licensing guidance](https://www.qt.io/development/open-source-lgpl-obligations) makes clear that obligations depend on the selected license and modules. No project-license change or commercial license purchase is authorized by this decision.
- The replacement sequence is confirmed in [the roadmap](../roadmap.md). Concrete migration mechanics, rollback evidence, and equivalent real public-boundary tests must be established before replacing the existing app. Do not retain the previous runtime as a permanent compatibility wrapper.

## Continuous Baseline Comparison

Confirmed on 2026-10-02 as part of question 14. Preserve a reproducible old baseline, its runnable artifact, toolchain/dependency identity, and build/run instructions. Keep old and new applications independently launchable with isolated local data and ports. The reference remains usable after the old production implementation is removed, without shipping its runtime in the replacement.

Compare semantics through real public actions and equivalent cluster preconditions, not private structure or pixel-identical screenshots. Separate mutable test resources so one run cannot change the other run's starting state. Record every discovered shipped function and its preserved, intentionally changed, approved-removed, or unresolved disposition in the existing test map.

The specification wins when the old application has a known bug. A baseline that fails an accepted requirement is evidence of an old defect, not permission to reproduce it. Unsupported old platforms must not be simulated as old releases; verify new-platform behavior against the accepted contract instead.

## Consequences

All 14 interview blocks are confirmed. Existing accepted behavior remains authoritative. Qt Quick, thin QML presentation, the tightened performance/package-size contract, and the comparison/retirement gates are confirmed; actual conformance and distribution obligations still require evidence. The working C# application establishes a retained test reference, not a requirement to ship the previous runtime in the replacement.

## What Licensing Evidence Is Required Before Platform Releases?

Verify the licenses of the exact shipped Qt modules, source and relinking obligations, signing, and distribution terms before each desktop or mobile release. No commercial Qt purchase, project-license change, or App Store compatibility is assumed. The native development toolchain currently uses Qt Core and Qt Quick components, without WebEngine. Installation of development tools is not evidence that the shipped application meets platform distribution requirements. References: [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt open-source obligations](https://www.qt.io/development/open-source-lgpl-obligations).
