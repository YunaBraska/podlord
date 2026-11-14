# How do native windows share sessions without restarting transports?

Status: Accepted
Date: 2026-10-09

## Context

SES-020 through SES-027 require session-owned forwarding/log lifecycles and
uninterrupted transfer to a detached window. The reference `AppRuntime` owns
single session placement. The native request client already supports view IDs
and transfer, but the application previously exposed only one window.

## Decision

Use one application-owned `WorkspaceRuntime` for the existing request client,
credential process and weekly release checker. `WindowHost` owns one QML engine
and a separate QML context/workspace projection per native window. An in-memory
placement map assigns every open session to one live window; the existing
session store remains the durable authority. Do not introduce a second session
registry or network client per window.

Detachment transfers the existing view binding and cached navigation, saved
filter/sort baseline, log positions and alarm holds/deduplication. It neither
closes nor recreates the session, shell or forwards. Active YAML drafts require
an explicit discard decision; detaching an unrelated background tab leaves its
window's current draft intact. Revealed Secret values are not copied. A
background inspector must obtain its own fresh document before editing.

The existing quick-open menu also opens saved sessions and imported contexts in
a separate window. Already placed sessions focus their owning window rather than
opening a duplicate. A detached window with multiple tabs can split another tab
out; a single-tab detached window is already the requested placement. Opening a
separate context does not discard an unrelated editor draft. If window creation
fails after activation was saved, keep the session open and report a recoverable
error rather than deleting it.

Windows share successfully committed settings, column layouts, filter presets
and alarm catalogs. Persistence keeps the existing optimistic conflict checks.
Each window evaluates alarms only for its own sessions. Each view owns its
focus/activity and log visibility, while the request budget/jitter remains
application-wide. Authentication is coordinated once, not once per window.
Focusing a session in another window updates the same durable last-used and
usage-frequency authority even if that window already displays it. Repeated
focus without a selection change is not another use. One runtime generation
guards asynchronous ranking reads so an older completion cannot replace the
newer selection order in a different window.

Closing an individual window atomically closes only its sessions before releasing
its transports. A failed persistence operation keeps that window open. Closing
the final main window retains existing open-session restoration while stopping
its runtime transports. Detached-window closure remains an explicit session
closure. Mobile targets do not offer detached-window controls.

## Alternatives and consequences

A second independent workspace transport would restart shells/forwards,
duplicate authentication and bypass the shared request budget; reject it.
Sharing a single workspace selection between windows would couple navigation,
filters, logs and secret reveal state; reject it. Independent projections consume
memory only for windows the user opens and retain the established UI components.

The confirmed STR-002 clarification on 2026-11-03 supersedes the original
main-window-only restoration. A private `window-layout.json` stores only window
identities, references to existing session UUIDs, the selected tab within each
window, normal geometry and maximized state. It contains no session configuration,
credentials or Kubernetes data. The session catalog remains authoritative for
open sessions and their order. Restoration intersects saved memberships with that
catalog, keeps unassigned open tabs in the main window, and never reconnects them.
Existing mobile detached-window restrictions are unchanged.

`WindowHost` owns the cached layout and bounded background load/save. Geometry
signals update memory only. Manual window transfers remain disabled until initial
restoration completes, so a delayed load cannot overwrite a live transfer.
Saving occurs during orderly application termination
or final main-window closure, using an atomic, private, versioned compare-and-save.
A failed save uses the existing Stay/Close guard and retains the previous file.
The host owns that layout error without replacing unrelated workspace errors.
Malformed metadata is reported without replacing the session catalog. Geometry is
fitted to a currently available screen where supported. Size, grouping and
position restoration do not restore shells, forwards, credentials or YAML drafts.
Scope does not establish complete visual, performance, accessibility or release readiness.
Public-boundary and real-cluster evidence is maintained in the existing test map.
