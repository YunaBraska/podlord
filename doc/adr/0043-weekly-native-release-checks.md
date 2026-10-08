# How does the native application check for updates safely?

Status: accepted, 2026-10-08.

## Decision

Check the repository's latest stable release anonymously at startup when due,
then once per seven days while running and on application activation when due.
The user confirmed weekly checks; the existing C# implementation uses the same
interval. An explicit About action may bypass that interval. There is no
automatic installation, authentication, browser launch, or redirect following.
`PODLORD_DISABLE_UPDATE_CHECK=1` disables both scheduled and manual checks.

`ReleaseUpdates` owns the request, deadline, timer and metadata. Workspace
exposes that owner; QML renders its cached state. Reading About does not fetch.
A separate versioned `release-check.json` is an operational cache, not another
preference store. It leaves the authoritative settings schema unchanged.
Atomic owner-only writes and one cross-process lock prevent concurrent checks
or loss of old metadata. Invalid or unsupported caches remain untouched.
Attempts are stamped before dispatch so restarting after a failure cannot cause
a retry storm. Errors retain previously verified release links.

An HTTPS link is accepted only if it exactly matches this repository, a valid
stable three-part version, and the native distribution name for this build:

- `podlord-native-macos-{arm64|x64}.zip`
- `podlord-native-win-{arm64|x64}.zip`
- `podlord-native-linux-{arm64|x64}.tar.gz`

Other targets do not guess a standalone installer. Existing `podlord-*` C#
packages are incompatible and never offered. These native asset names must
exist before publishing an update; the current private package's filename alone
does not constitute a compatible published asset.

Metadata is limited to 1 MiB; a compatible uploaded package must satisfy the
existing 50 MB distribution budget. The metadata GET has a 10-second deadline,
unlike Kubernetes requests, which follow their independent completion policy.
Errors and HTTP rate limits never cause immediate retries. No response body,
credentials, kubeconfig, resource data or machine identifier is sent to or
copied into an update error.

## Alternatives rejected

Reusing legacy asset names could replace the native app with C#. Reusing the
settings file for release metadata would couple network completion to user
preference writes. An updater framework or installer is unnecessary for a
deliberate verified browser handoff.

## Verification and rollback

`native.release_updates.*` drives real HTTP through the public owner and actual
About buttons. Only GitHub and the browser handoff are external test boundaries.
Disabling checks or removing this operational cache leaves sessions, sources,
settings and installed software unchanged. Publishing, signature verification
and an installer are outside this decision; no public readiness claim follows.
