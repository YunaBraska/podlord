# How Does The Native Runtime Persist Sessions Without Sharing Legacy State?

Status: accepted implementation of the isolated-runtime constraint in [ADR 0015](0015-cpp-rewrite-for-performance-and-device-coverage.md).

## Decision

The first native runtime slice owns local session lifecycle and immutable configuration copies. It exposes a real CLI before integration with Qt Quick. Qt Core supplies JSON, process execution, UUIDs, inter-process locking, and atomic file publication; no second persistence dependency or plugin layer is introduced. C++20 is the language baseline and CMake builds the runtime independently of .NET.

The native catalog is `sessions.json`, schema version 2, in an explicitly selected absolute profile. Legacy `store.json` is neither read nor overwritten. Unknown schema versions, unknown fields, malformed values, duplicate identities or titles, inconsistent replacement sequences, symlink profiles/files, sequence-number exhaustion, and failed writes produce explicit errors. Future migration must be a separate verified copy operation, not implicit fallback parsing. The initial, unreleased schema 1 cannot establish individual usage timestamps or replacement sequences; it is rejected without modifying its data, not guessed into the new model.

Mutations use a local `QLockFile` and a `QSaveFile` commit; lock contention returns `Busy` before any write. Equivalent configurations anywhere within the same replacement sequence and repeated activation of the already-active session are no-ops. Repeated change notifications cannot create another copy of an existing replacement; another sequence with equivalent configuration does not suppress a required copy. Unsigned ordinals are decimal strings to avoid precision loss. A sequence identity and base name determine replacement labels for both named and automatic sessions. Number allocation follows SES-005-007 and SES-017-019; computed labels are not persisted redundantly. Renaming to a different title starts a separate sequence and retains configuration and usage history.

Each explicit activation records its UTC timestamp. The selection entrypoint derives the preceding-30-day usage ranking and tie-breaks required by SES-008-014. The persisted catalog order is unchanged by ranking or activation, preserving SES-015-016. Usage counts and latest-use labels are presentation output only. History is not discarded merely because it falls outside the ranking window.

Correction on 2026-10-02: the first schema and tests incorrectly treated named replacements as unchanged visible titles and recorded lifetime counts. Six failing public-CLI regressions exposed those deviations from the already accepted operational contract. Schema 2 corrects the implementation rather than changing that contract.

## Scope And Evidence

This slice stores context identifiers and namespace scopes, not credentials or copied kubeconfigs. It makes no network requests and owns no port-forward process. It does not prove context resolution, final tab selection, forwarding cleanup, authentication, legacy import, UI parity, or platform release readiness. CLI tests run independent real processes and filesystem operations through public entrypoints. Test outcomes belong in [the test map](../spec/k3d-test-map.md).

Persistence references: [QSaveFile](https://doc.qt.io/qt-6/qsavefile.html), [QLockFile](https://doc.qt.io/qt-6/qlockfile.html). Qt platform and packaging obligations remain owned by ADR 0015.

## How Does Normal Application Launch Select Its Isolated Profile?

The GUI now defaults to Qt's application-specific `AppLocalDataLocation`, using
the native application identity. This enables normal desktop launch without
mandatory command-line arguments and delegates platform paths to Qt rather than
hard-coding desktop paths for mobile devices. The session and source CLIs retain
their explicit profile requirement; `--profile` remains an absolute-directory
override for the GUI and for isolated comparisons.

The selected path passes the existing profile validation before opening any
store. An explicitly empty, invalid or repeated override is rejected, never
replaced with the default. A file or symlink occupying the default profile is
also rejected. The GUI does not import `KUBECONFIG`, `~/.kube/config`, legacy
configuration or legacy sessions merely because it was started. Import and
interactive authentication remain explicit public actions.

Default-path launch tests first resolve Qt's real path in an isolated child
environment and refuse to start the GUI if it escapes that environment. They
exercise normal launch, ambient kubeconfig and provider isolation, invalid
default paths and explicit overrides through the real executable. This desktop
evidence does not establish signed packaging, mobile execution or legacy-state
migration. Reference: [Qt 6.11 QStandardPaths](https://doc.qt.io/qt-6.11/qstandardpaths.html).

## Rollback

The preserved legacy application remains independently runnable. Stop the native runtime and select the legacy reference; native files do not modify the legacy store. This is coexistence, not proof that native state can already be migrated back into the old application.
