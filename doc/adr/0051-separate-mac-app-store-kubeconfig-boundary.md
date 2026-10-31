# 0007 Separate Mac App Store kubeconfig boundary

## Status

Accepted

## Context

The direct PodLord distribution can import the home kubeconfig, scan directories, watch source files, and run a kubeconfig `exec` credential command. A Mac App Store build must run in the App Sandbox and cannot rely on arbitrary files or external programs.

## Decision

Build the Store edition with `PodlordAppStore=true`. It imports individual kubeconfig files selected through the system picker or pasted YAML, then uses its app-owned snapshot. It does not automatically read or watch source files, scan directories, accept typed paths, run k3d, check GitHub Releases, or run kubeconfig `exec` credentials. Store imports require embedded token or certificate data.

The direct download keeps the existing import, refresh, and authentication behavior.

## Consequences

One codebase produces two deliberately different distribution channels. The Store build preserves Kubernetes resource operations while making its file and credential boundary explicit. Users whose clusters require an external login command use the notarized direct download.

## Native channel, 2026-10-31

The confirmed native boundary is compiled with `PODLORD_MAC_APP_STORE=ON`.
It retains native OIDC and cloud authentication and explicitly imported private
keys. It never falls back to an external credential program. This supersedes
the embedded-token/certificate-only restriction above for C++, not C#.

The system picker and paste import app-owned copies. Folder imports, ambient
home imports, typed-path controls and reimporting original source files are
unavailable. Certificate and token file references fail before reading a file.
GitHub update checks and cached direct-download links are unavailable. Direct
builds retain their existing behavior.

The native entitlements enable the App Sandbox, user-selected read-only files,
network clients and the local listener required for session-owned forwards.
An ad-hoc signed package is only a local preflight. App Store distribution
signing, provisioning, publisher metadata and Apple review remain separate.

### Universal2 distribution

The native Mac App Store application contains both `arm64` and `x86_64` slices. Intel support is not delegated to Rosetta or removed from the Store channel. The two Store-configured builds must originate from the same source revision and use matching dependencies and deployment targets.

`scripts/merge-native-macos-apps.sh` verifies the signed thin input bundles, their identifiers, contained symlinks and matching resource files before merging their Mach-O binaries. The result is unsigned and must pass the existing signing and packaging boundary before distribution. The existing download and installed-size gates remain applicable unless a separate explicit size decision changes them.

The native publication workflows prepare C++ candidates. The C# application remains an independently runnable comparison reference, not the implementation shipped by those workflows. Preparing or checking a candidate does not authorize a release, tag or Store upload.
