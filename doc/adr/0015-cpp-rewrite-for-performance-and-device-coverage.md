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
- The replacement sequence is confirmed in [the roadmap](../ROADMAP.md). Concrete migration mechanics, rollback evidence, and equivalent real public-boundary tests must be established before replacing the existing app. Do not retain the previous runtime as a permanent compatibility wrapper.

## Continuous Baseline Comparison

Confirmed on 2026-10-02 as part of question 14. Preserve a reproducible old baseline, its runnable artifact, toolchain/dependency identity, and build/run instructions. Keep old and new applications independently launchable with isolated local data and ports. The reference remains usable after the old production implementation is removed, without shipping its runtime in the replacement.

Compare semantics through real public actions and equivalent cluster preconditions, not private structure or pixel-identical screenshots. Separate mutable test resources so one run cannot change the other run's starting state. Record every discovered shipped function and its preserved, intentionally changed, approved-removed, or unresolved disposition in the existing test map.

The specification wins when the old application has a known bug. A baseline that fails an accepted requirement is evidence of an old defect, not permission to reproduce it. Unsupported old platforms must not be simulated as old releases; verify new-platform behavior against the accepted contract instead.

## Consequences

All 14 interview blocks are confirmed. Existing accepted behavior remains authoritative. Qt Quick, thin QML presentation, the tightened performance/package-size contract, and the comparison/retirement gates are confirmed; actual conformance and distribution obligations still require evidence. The working C# application establishes a retained test reference, not a requirement to ship the previous runtime in the replacement.

Android uses Qt's OpenSSL TLS backend, but the official Qt Android kit does not include the OpenSSL runtime libraries. Android packages therefore bundle verified OpenSSL 3 libraries for each shipped ABI; configuration fails if they are missing. The pinned source build and `_3` library names are owned by `scripts/build-native-android-openssl.sh`. The existing OpenSSL notice covers this dependency. Package inspection proves inclusion, not an installed-device HTTPS connection.

## Upstream Qt Quick Shutdown Correction

On 2026-11-07 the user delegated the release-blocking Qt dependency decision.
Retain the pinned 6.11.3 baseline and apply the unmodified upstream correction
`bbcaf3f39143b9add9722f4e31b51fc1dd102892`, rather than jump to a larger Qt release
that does not yet contain it. This is a bounded supplier backport, not a new
application renderer, a behavior change or a maintained alternative Qt fork.

The official source archive and exact diff are checksum-pinned. Source-built
SDKs apply the same diff before compilation. Prebuilt kits rebuild only Quick
and its two related plugins, including required Android Java runtime assets.
Android app configuration validates the patch identity at the SDK resolved from
the actual Qt Quick package, so an old kit cannot silently bypass preparation.
SDK cache identities include the patch and build
recipe, and restored metadata must identify the same patch. A public Qt
application-owned asynchronous image/engine shutdown regression must finish,
not merely load the image. The unmodified 6.11.3 kit demonstrably hangs there.

Requalify installed applications, packages and affected platforms. Disclose the
modified library and preserve corresponding-source obligations. Remove this
backport once a qualified unmodified Qt release contains the correction.

## What Licensing Evidence Is Required Before Platform Releases?

Verify the licenses of the exact shipped Qt modules, source and relinking obligations, signing, and distribution terms before each desktop or mobile release. No commercial Qt purchase, project-license change, or App Store compatibility is assumed. The native development toolchain currently uses Qt Core and Qt Quick components, without WebEngine. Installation of development tools is not evidence that the shipped application meets platform distribution requirements. References: [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt open-source obligations](https://www.qt.io/development/open-source-lgpl-obligations).

## Android early-Activity startup correction (2026-11-10)

A second, explicitly approved exception permits a minimal correction to the embedded Qt 6.11.3 Android SDK. If Android recreates the Activity after loading Qt libraries but before starting the native application, Qt returns `AlreadyLoaded` and previously skipped native startup. The correction initializes the new Activity's delegate for both successful loading states. Application arguments are still appended only on the first load. Failed loading and already-running application handling remain unchanged.

On library reuse, Qt refreshes its native Activity reference before starting the delegate. The JNI reference was initialized during library loading and otherwise still points at the destroyed Activity. The destroyed Activity also unregisters its application-state listener regardless of configuration retention. Otherwise that obsolete delegate can register its input backend when native startup completes. The lifecycle regression must therefore prove a visible system keyboard and actual text input, not merely a rendered window.

`scripts/patches/qtbase-6.11.3-android-startup.diff` contains the complete modification. `scripts/qt-android-startup.cmake` verifies the Qt version, exact official source and patch hashes, compiles only the modified Activity class with Java 8 bytecode compatibility, and updates a caller-owned SDK. It records the original and resulting JAR hashes. Android application configuration rejects missing or inconsistent SDK metadata and verifies the actual JAR. Non-Android SDK cache keys exclude this Android-only patch.

Prepare an isolated Android SDK before configuring either the normal app or the regression app:

```sh
cmake -DQT_SDK_PREFIX=/absolute/path/to/owned/qt-android \
  -DANDROID_SDK_ROOT=/absolute/path/to/android-sdk \
  -DWORK_ROOT=/absolute/path/to/new/patch-work \
  -P scripts/qt-android-startup.cmake
```

The regression application uses the real Podlord Activity and native entrypoint. Only its test Application requests one early Activity recreation through Android's lifecycle API. The actual Map import action must become accessible in the same process, and Sources must support the system keyboard and real text entry. An unmodified SDK must reproduce the missing UI. The first incomplete correction is retained as a negative input-lifecycle control. The fixture and its trigger are not included in distributable packages. The isolated emulator drains first-boot broadcasts before this controlled lifecycle test, rather than confusing a later system-overlay reconfiguration with the requested recreation.

This is not a general Qt fork or an application restart workaround. Preserve the modified-source notice and corresponding-source availability required by Qt's license. Retire the patch, helper and SDK gate together when an upstream Qt release passes the same regression without the correction.
