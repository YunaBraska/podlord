# Why Must Native Preflight Gate CI And Release?

Status: accepted implementation gate, 2026-10-05.

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
success. The 95% line and 90% branch thresholds remain unchanged. Owned raw
profiles are removed on exit; retained reports identify the failed gate.

CI uploads evidence only, not unsigned or legally unreviewed native release
binaries. macOS Developer ID/notarization, license notices, clean-device
installation, other platform builds and function parity remain independent
release gates.

## What Changes For A Private macOS Build?

The confirmed 2026-10-06 distribution scope is private local use while the
developer enrollment is pending. Developer ID signing and notarization are
deferred public-distribution gates, not blockers for that private artifact.
Local ad-hoc verification, dependency closure, safe startup, functional parity,
performance, tests, and the declared coverage gates remain required. A private
macOS result does not establish public distribution or other-platform support.
No unsigned native release is published by CI under this decision.

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
exclusive. The 60-minute job budget and coverage/performance gates are unchanged;
Intel's previous incomplete run must be replaced by executed evidence.

The C# comparison CI requests Release compilation so runtime budgets exercise
optimized code. It does not remove legacy regression cases or change their
thresholds. Source/binary parity, hosted execution and runtime budgets remain
separate checks.
