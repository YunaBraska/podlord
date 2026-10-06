# How Does Disabling Workspace Restoration Affect Saved Sessions?

Status: accepted

## Context

The C# reference exposes a workspace restoration preference. Native previously
displayed a fixed Enabled label despite always adopting saved open-tab placement.
Restoration must not grant authority for interactive authentication, YAML writes
or reopening port-forwards. Session catalog data and view preferences have
existing owners and must not acquire a second persistence path.

## Decision

The existing read-settings owner persists `workspaceRestore`, default true, in
schema version 6. Versions 1 through 5 remain readable and default to enabled;
version 6 requires an actual boolean. Existing atomic saves and conflict checks
remain authoritative.

Changing the preference affects the next launch, not the current tabs or their
forwards. Disabled startup closes saved tab placement through the existing locked
session-store mutation, without deleting session identity, configuration, history,
usage/order metadata or saved view preferences. Explicit reopen is always allowed.
Ordinary catalog reload is not a second startup and must not close current tabs.
Reenabling allows subsequent startup to restore saved open-tab placement normally.

Malformed, locked or conflicting settings fail explicitly without replacing saved
bytes. No alternate settings file or fallback catalog is introduced.

## Verification And Limits

The public workspace/UI driver covers defaults, disabled startup, reload, explicit
reopen, reenabling, startup locks, malformed/missing flags, conflicting/busy saves,
checkbox, keyboard, 390-pixel navigation and repeated use in Basic and Fusion.
The official SDK focused run passed 198/198 including those 28 cases.
The narrow-window regression exposed an offscreen Settings button; native Flow
now wraps the existing header controls rather than hiding them in horizontal
navigation scrolling.

Separate release gates remain for saved filter/sort retention, installed-app
restart comparison and concurrent-window placement ownership. Store locking
protects bytes but does not establish ownership of tabs in another running
window. This decision does not declare multiwindow parity.

## Migration And Rollback

Existing documents upgrade only through the established settings save path.
Older binaries cannot be assumed to read version 6. Preserve the original profile
for rollback and keep reference/native profiles isolated; do not rewrite a native
profile into an older schema merely to make a downgrade appear successful.
