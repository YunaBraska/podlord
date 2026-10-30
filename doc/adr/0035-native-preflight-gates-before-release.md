# Why Must Native Preflight Gate CI And Release?

Status: accepted implementation gate, 2026-10-05; CI reporting clarified 2026-10-09.

## Context

The native replacement has executable local tests, LLVM coverage and a macOS
package preflight, but CI and release previously executed only .NET workflows.
A successful C# build therefore said nothing about the replacement. Native
startup tests also assumed an offscreen plugin that production packages
correctly omit.

## Decision

CI and release reuse one native preflight workflow. It runs the existing public
test/benchmark/coverage entrypoint and package inspection on macOS arm64 and
x86_64 runners. Release's existing test job depends on this gate; legacy assets
are not silently relabeled as native assets. Hosted execution remains required
evidence, not inferred from workflow syntax validation.

Production packages are exercised using Cocoa through the existing isolated
application startup test. Offscreen remains the component-test default.
Native coverage includes the read-overlap and reference-radar executable paths,
is reported even after a CTest failure, and never replaces that failure with
success. Under
the confirmed 2026-10-09 CI policy, coverage and performance target misses are
reported diagnostically rather than replacing functional check results. Green
CI is necessary evidence, not proof that every release requirement is satisfied.
Owned raw profiles are removed on exit; retained reports identify target misses
and functional failures separately.

Fail-fast ordering also covers the existing settings read-denial and cached-row
field-presence contracts, plus the port-dialog refresh regression in Basic and
Fusion. These six tests run before the remaining suite and are excluded from
that later phase, not removed or duplicated. This catches an unsuitable root
container and silent cache/input mutations before a full test run. Permission
tests must execute under the CI runner's ordinary user identity.

Primary native-trust tests call Apple's evaluation directly. The existing
non-gating failure-reproduction step alone sets `PODLORD_TRACE_PLATFORM_TLS=1`
to load the test-only observer for delayed/cancellation diagnostics. Observed
replays do not replace the original exit status or establish an uninstrumented
pass. Deliberate external-API fault-injection scenarios remain separate.

On 2026-10-15 the native release coverage decision changed to 95% line and 85%
branch coverage overall, plus at least 90% branch coverage for new logic.
The full native runner enforces the new-logic floor on the newly introduced
AWS EKS, GKE, OCI and OIDC provider source files, the background HTTP transport
and native Apple trust evaluation. The credential/detail revision, credential
revocation and YAML-draft authority functions also have individual 90% branch
gates. Their public HTTP regressions cover expired credentials, detail expiry
and backward clock movement while preserving readable cached documents.
New production logic added elsewhere
must receive its own explicit 90% gate before its release claim; a passing
overall percentage alone does not establish this. The reason is that the
broader UI/runtime code has many independently verified public behaviors but
does not yet meet the former 90% aggregate branch threshold. This decision
does not waive missing behavior, package, device, image, or performance evidence.

The capped display model's source-change optimization has a separate 90% branch
gate across its complete constructor family, including the insertion, removal
and layout callbacks. The existing overall and Radar/Map gates remain unchanged.

CI uploads evidence only, not unsigned or legally unreviewed native release
binaries. macOS Developer ID/notarization, license notices, clean-device
installation, other platform builds and function parity remain independent
release gates.

## Can Metal Be Diagnosed Without Activating The Test Window?

The separately approved `--background-metal-diagnostic` mode reuses the existing
performance driver and workload. LaunchServices starts the stable test bundle
with `open -g`. Before Qt initialization, only the diagnostic process sets
`QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM=1` to suppress Qt's own launch
activation. This switch is checked by the pinned
[Qt 6.11.2 Cocoa launch delegate](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/cocoa/qcocoaapplicationdelegate.mm).
Initial QML properties prevent window activation before creation.
The window also rejects pointer input. The diagnostic skips the shared input
helper that raises Cocoa windows even during layout checks.
The driver never raises or activates that window, requires Metal and an exposed
inactive window, and fails if it observes activation. An unavailable desktop or
occluded renderer is not replaced by a foreground launch or software result.

Routine performance modes mute their disposable profiles before loading
contexts. Audio behavior has its separate tests. No user profile or system
volume changes.
Every JSON record has `diagnosticOnly: true` and the boundary
`background-cocoa-metal-diagnostic`. The output directory also contains an
explicit diagnostic-only marker. Existing foreground and offscreen modes retain
their workloads, guards and limits. Background timing can locate CPU work, but
cannot satisfy any foreground, installed-startup or release gate.

## What Changes For A Private macOS Build?

The confirmed 2026-10-06 distribution scope is private local use while the
developer enrollment is pending. Developer ID signing and notarization are
deferred public-distribution gates, not blockers for that private artifact.
Local ad-hoc verification, dependency closure, safe startup, functional parity,
performance, tests, and the declared coverage gates remain required. A private
macOS result does not establish public distribution or other-platform support.
No unsigned native release is published by CI under this decision.

Desktop comparisons reject quarantined input bundles before starting their local
cluster. The runner does not remove macOS quarantine or treat a locally valid
code signature as user approval; use an already-approved local reference build.

## What Changes For A Private Android Build?

On 2026-10-27 the user approved distributing the private Android release as
`podlord-native-android-arm64.apk.zip`. The archive contains the complete signed
APK. Extracting it before installation is required. No runtime library, feature
or installation prerequisite is moved to a separate download.

PER-012 measures this actual ZIP download against the unchanged 50,000,000-byte
limit. PER-013 still measures the installed application against 100,000,000 bytes.
Archive size does not establish installed size, Android runtime behavior or
store-distribution readiness. Retain the APK signature, checksum and extraction
round-trip result with the package evidence. Existing raw-APK size failures
remain historical failures, not retroactive passes.

## Verification And Rollback

Validate workflow syntax with actionlint, repeat the failing public search
restore scenario, execute all native tests/coverage and the packaged Cocoa
startup scenarios. Cold startup may synchronize; editing an existing cached
search may not. Keep the preserved C# reference and existing distribution
scripts until RWT-015 is satisfied. This change does not migrate user profiles.

## How Is The macOS Runtime Boundary Checked?

The package must declare `LSMinimumSystemVersion`. Its executable, every bundled
framework and every runtime plugin must contain each executable architecture,
target macOS, and require no newer macOS than that declaration. Dependency closure
alone does not prove compatibility. The same checker owns physical dependency,
architecture and minimum-OS inspection; production gates are not weakened for
test artifacts.

The local Homebrew build was arm64-only and included macOS-27 dependencies.
Use the pinned official Qt 6.11.2 desktop SDK and checksum-verified yaml-cpp
0.9.0 source through `scripts/install-native-macos-toolchain.sh`. The macOS-13
build floor is a candidate artifact boundary, not a declaration that old-device
installation, Intel, Windows, Linux, Android or iOS has been verified.

Single-architecture packages remove only the SDK's other architecture slices.
Universal packages retain their slices. The application has no ODBC, PostgreSQL
or Mimer integrations; those unused plugins are omitted, while SQLite and both
deployed media backends remain. Codesign, physical runtime inspection and size
gates execute after pruning. Keep the corresponding dependency sources and
build inputs available; this does not resolve distribution-license obligations.

On 2026-10-06, six physical metadata regressions failed before the gate was added.
The corrected gate passed all 24 real-package dependency/platform cases. The
official arm64 candidate passed ad-hoc verification and package budgets at
28,520,490 ZIP bytes and 73,520,027 installed bytes. Twelve packaged Cocoa startup
and command-line cases passed. Hosted workflow execution and clean-device
compatibility remain independent required evidence.

## Which TLS boundary must the portable package preserve?

The official Qt SDK's SecureTransport fallback failed real client-certificate authentication on the tested macOS runtime (`SecPKCS12Import` error -26275). The portable build therefore retains the previously shipped OpenSSL backend using source-verified OpenSSL 3.5 LTS, deployed inside the application. Certificate validation remains enabled; neither a system OpenSSL installation nor a verification bypass is a deployment solution.

The dependency gate requires both bundled OpenSSL libraries and the Qt OpenSSL plugin. The preflight also runs trusted mutual TLS, untrusted-server CA and untrusted-client cases from a private copy of the actual package with ambient library search paths removed. This test uses an instrumented public-workspace driver inside that copy; it does not replace main-executable startup, clean-device or visual evidence. Standalone test binaries receive their configured SDK library directory because Darwin's Qt loader does not search their already-loaded library paths like a deployed app's Frameworks directory.

The earlier 73.5 MB Qt-only artifact is superseded: its static dependency pass did not establish functional client-certificate TLS. Current package measurements and executed boundaries belong in the test map.

## Which dependency notices belong to a local candidate?

About reuses its existing read-only license dialog for original Qt LGPL 3/GPL 3,
FFmpeg LGPL 2.1, OpenSSL Apache 2.0 and yaml-cpp MIT texts. Reading is explicit,
local and network-free; closing releases the displayed text. Sound credits keep
their existing owner. The package includes those same texts and the original
SPDX documents for the five deployed Qt supplier modules.

The deployment tool and supplier metadata come from the configured Qt Core SDK,
not an unrelated tool found on PATH. Missing required metadata fails packaging
explicitly. Supplier source/tool entries are preserved as supplied; these
documents are not asserted to be a filtered, complete runtime SBOM.

This closes neither corresponding-source nor replacement/relinking obligations.
Exact sources/build inputs, additional component notices, installation evidence
and platform distribution terms remain independent release gates. In particular,
ad-hoc signing does not establish Developer ID or notarization.

## How Is Independent UI Test Work Scheduled?

Native macOS CI runs six isolated test processes, matching the already bounded
Linux pool. The same 146 public Terminal scenarios passed locally with four and
six processes: 130.83 s and 76.08 s. This motivates bounded concurrency, not a
hosted-run performance claim. Desktop focus, CPU and RSS measurements remain
exclusive. The 60-minute job budget and release coverage/performance targets are unchanged;
Intel's previous incomplete run must be replaced by executed evidence.

Only the six multi-theme Radar image tests run exclusively within CTest. The
October 30 Intel run exceeded five 45-second capture watchdogs under concurrent
load, while the same checks completed individually in 19.73 to 29.05 seconds.
Their 45-second watchdogs, rendered-color assertions and theme matrix remain
unchanged. Other functional tests keep their bounded parallel pool. Generated
CTest metadata is checked for all six exclusive registrations.

The C# comparison CI requests Release compilation so runtime budgets exercise
optimized code. It does not remove legacy regression cases or change their
thresholds. Target misses are reported without failing CI; functional regressions
still fail it. Source/binary parity, hosted execution and runtime budgets remain
separate checks.

## Does the Apple product require Rosetta?

No. The native macOS Apple Silicon package and iOS device build target ARM64.
The older Qt 6.11.2 prebuilt iOS simulator libraries target x86_64. Their Rosetta
dependency is superseded by the verified stock-source ARM64 simulator toolchain
described below. Rosetta is not shipped in the application. Simulator results
must record the actual SDK architecture.

Apple identifies macOS 27 as the final general-purpose Rosetta release. Later
support is limited to selected older games and is not an application-toolchain
compatibility promise. Before using macOS 28 or later for this test lane, replace
the x86_64 simulator SDK with an ARM64 simulator build of Qt and its FFmpeg
dependencies. The local source-built alternative now passes the real Kubernetes
theme matrix on Apple Silicon without Rosetta. This does not certify a future
macOS release or require separate mobile UI and authentication implementations.

Sources: [Apple's Rosetta support notice](https://developer.apple.com/news/?id=w5ngl9k2)
and [Qt's simulator architecture requirements](https://doc.qt.io/qt-6/ios.html).

### 2026-10-26: iOS TLS and native simulator toolchain

The real iOS Kubernetes import reached the API but could not negotiate mutual TLS with an EC client certificate. Qt 6.11.2 SecureTransport reported `SecPKCS12Import failed: -26275`. Qt's shared PKCS#12 encoder handles RSA and DSA keys, not EC keys, while its SecureTransport backend always uses that encoder for client identities. The application parsed the same EC key successfully through Qt's public key API. Changing the kubeconfig parser or weakening certificate verification would not address this failure.

The approved solution is to build stock Qt 6.11.2 for iOS with directly linked OpenSSL 3.5.9 and SecureTransport disabled. This uses the existing OpenSSL dependency rather than maintaining a private Qt cryptography patch. OpenSSL is embedded in the application. Users install neither OpenSSL nor kubectl. A source check established that Qt's shared Apple CA loader enumerates system certificates only on macOS and returns an empty list on iOS. The approved native Apple trust integration closes that gap without shipping a separate CA bundle.

Apple Security evaluates the actual peer chain and hostname against system and installed-user trust. Explicit kubeconfig authorities remain exclusive anchors. Only missing-issuer errors may be reconsidered. Hostname, expiry, revocation and other TLS errors are never bypassed. Native evaluation uses `SecTrustEvaluateAsyncWithError` on its owning dispatch queue.

The approved iOS HTTP adapter owns a background thread and Qt network manager. Qt's synchronous HTTP TLS-error callback waits there, not on the GUI thread. Caller-side replies expose cached metadata and bounded response chunks through the existing asynchronous request flow. Cancellation wakes the worker, rejects late results and tears down owned replies and threads. WebSockets use Qt's pause/resume API with background native evaluation. Other platforms retain their existing Qt HTTP transport. No Qt fork, global trust-store mutation or external service is introduced.

The October 27 hosted build found that Xcode 16.4 does not expose the C++ stop-token and joining-thread types used by the initial adapter. Cancellation now uses the existing per-request shared lifetime with a standard mutex and condition variable. Native trust completion and cancellation lock and notify that same state, so cancellation wakes a blocked HTTP validation without polling or a lost notification. WebSockets retain the same cancellation owner. No Qt fork, extra runtime dependency, relaxed trust policy or deployment-floor change is introduced. The same public cancellation and TLS regressions remain mandatory.

The Android product does not compile the unused iOS HTTP adapter or its HTTP trust hook. The configured NDK 27.2 standard library does not expose the adapter's `std::stop_token` type. Android continues using its existing Qt network manager and TLS verification. This build boundary avoids an unnecessary toolchain dependency without changing Android request or authentication behavior. The ordinary desktop adapter tests remain enabled.

The native test runner includes the Objective-C++ trust source in macOS coverage. It enforces the existing 90% new-logic branch target for both `background_http.cpp` and `platform_tls_apple.mm`, alongside the existing overall and provider gates. Test-only fault injection substitutes individual external CoreFoundation/Security failures in the isolated CLI process, after startup and only for calls from that test executable. It does not replace the owned trust policy or enter any product bundle. Normal native framework evaluation and the real iOS transport tests remain independent evidence.

`scripts/test-native-ios-trust.cjs APP_BUNDLE OPENSSL NEW_EVIDENCE_DIRECTORY` verifies actual HTTPS and WSS using isolated local certificates and a disposable ARM64 simulator. It proves rejection before root installation, acceptance after installation, explicit wrong-CA rejection, a slow intermediate-certificate fetch while GUI heartbeats continue, and cancellation without a later application request. Test roots are installed only in the disposable simulator, never the Mac keychain. All owned servers, private keys and the simulator are removed. The complete populated iOS Kubernetes theme matrix then verifies EC-client-certificate mutual TLS and shared UI behavior through this transport.

`scripts/build-native-ios-toolchain.sh iphoneos HOST_QT NEW_OUTPUT_DIRECTORY` builds the ARM64 device SDK. Replace `iphoneos` with `iphonesimulator` for a simulator SDK matching the host architecture. On Apple Silicon that is ARM64 and does not require Rosetta. The host Qt must be version 6.11.2 with `qtshadertools`. Each output directory is new and separate from the existing official SDK. Downloads are SHA256-pinned. FFmpeg 7.1.5 remains embedded as frameworks and yaml-cpp 0.9.0 remains static. Failed builds retain their own logs for diagnosis and never overwrite an existing toolchain.

The required regression boundary is the running iOS application importing a real local Kubernetes kubeconfig, negotiating EC-certificate mutual TLS and loading the resource cache. A successful toolchain build alone does not satisfy that gate. The populated theme matrix must then run against the same local API. Qt still warns that the installed Xcode SDK 27.1 is newer than its tested SDK 26. This warning is not suppressed and does not constitute a supported-platform certification.

Sources: [Qt SecureTransport client identity import](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/tls/securetransport/qtls_st.cpp), [Qt PKCS#12 key encoder](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/tls/shared/qsslsocket_qt.cpp#L126), [Qt OpenSSL linkage and Apple CA integration](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/tls/openssl/CMakeLists.txt), [Qt iOS source builds](https://doc.qt.io/qt-6.11/ios-building-from-source.html).

Google account and registration work is paused at the user's request. No account creation is a prerequisite for continuing implementation or local release checks. This does not claim that an unregistered personal Google login has passed a live-provider gate.
