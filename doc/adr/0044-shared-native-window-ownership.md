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
session store remains the durable authority. Do not introduce a second registry,
new window-placement persistence format or network client per window.

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

Window geometry and placement are not newly persisted. Existing restoration
opens the retained sessions through the main workspace. Scope does not establish
mobile-device, complete visual, performance, accessibility or release readiness.
Public-boundary and real-cluster evidence is maintained in the existing test map.
