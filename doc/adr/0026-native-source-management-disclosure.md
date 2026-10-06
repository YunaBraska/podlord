# How is source management kept out of the active resource workspace?

Status: accepted for the native desktop presentation.

## Decision

Keep the existing import, file/folder chooser, context selector, open-context and
reload actions together in one source-management panel. Show it while no session
is active. After a context is opened, collapse it and retain one keyboard-operable
Sources disclosure in the application toolbar. Synchronization and appearance
settings remain directly reachable outside this panel.

Opening or closing the panel reads the existing source catalog. It performs no
network request, storage reload, authentication retry or session change. Import,
reload and opening a context remain explicit actions using their existing owners.
The disclosure is transient presentation state, not another persisted setting.

## Consequences

Two source-management rows no longer occupy the active resource workspace.
Controls remain available for subsequent imports and context changes. The empty
workspace still provides the complete import path without an extra discovery
step. No source cache, background watcher or timer is added.

This is a presentation improvement, not evidence of the still-missing source,
session, localization or settings capabilities elsewhere in the migration.

## Verification and rollback

The public QML tests exercise collapsed state, reopening, repeated use, keyboard
activation, the minimum desktop width, failed import without losing the session,
and opening another context. Each runs with Basic and production Fusion styles.
Existing import, authentication and session-navigation tests use the same visible
Sources action when the panel is collapsed. The isolated C# reference and its
profile are unchanged; no stored data format is modified.
