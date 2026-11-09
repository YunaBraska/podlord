# How is source management kept out of the active resource workspace?

Status: accepted, revised after native acceptance testing on 2026-11-09.

## Decision

Use context cards and the existing anchored quick-open chooser to open contexts or saved views. The sidebar caption invokes that same chooser. Sources settings owns file/folder/paste imports, source editing and provider-key/profile management. Remove the duplicate two-row source-management form from the resource workspace. The empty Map retains one import action. Drag-and-drop and OS document-open events reuse the ordinary importer.

A context is an imported connection definition. A saved view is the existing session configuration with its own scope, filters, name and history. Renaming or reopening either uses its existing owner. Presentation does not add a cache, parser, watcher, authentication path or persisted setting.

## Consequences

The original disclosed form became a second route with repeated import/open/key buttons. Removing it eliminates a maintained UI implementation and its layout state without removing those capabilities. Source settings and quick-open remain reachable with keyboard, mouse and touch. Opening the chooser itself neither reloads storage nor contacts Kubernetes.

## Verification and rollback

Dedicated public UI tests exercise Sources import, quick-open, repeated disclosure, keyboard dismissal, narrow geometry, invalid input and context switching. Unrelated log/auth/filter component fixtures may prepare sources through the public Workspace API instead of repeating an import-form journey. They still use real stores, request scheduling, QML and external HTTP boundaries. Actual delivered-app and local Kubernetes checks remain separate proof. No source/session schema is changed by this consolidation.
