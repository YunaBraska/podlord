# How Should Session Loading And Shared Filter State Behave?

Status: accepted, 2026-10-05.

## Context

Initial discovery populated the radar as new resources, producing distracting
alerts before the session had loaded. Serialized reads let one slow collection
hold up every other resource kind. Problems/Activity and named presets were still
disabled in the native sidebar.

## Decision

The session client owns collection progress separately from inspector/log work.
Discovery, pagination and collection requests contribute to the same cycle.
Progress is a monotonic work indicator, not a prediction of undiscovered work or
elapsed time; authentication has no invented percentage. The strip grows upwards
and shows red/yellow/green from top to bottom.

Alerts pause effects during collection loading. Once the initial snapshot is
complete, a matching sound-enabled rule plays once so an existing problem is
audible when the context opens. The initial snapshot remains a baseline for
zoom and animation. Unchanged later refreshes do not replay sounds. New real
changes are evaluated against the prior completed snapshot. Activity
classification has one owner in the canonical session cache.

A single scheduler admits up to four independent reads while retaining minimum
start spacing, hard-limit and backoff. Mutations and forward handshakes remain
exclusive. Admission timestamps prevent late old collection reads from replacing
newer inspector data. Already sent work finishes in its originating session.

Existing session-view persistence owns mode state and the profile-wide named
preset catalog. Presets contain resource filters, not sort or camera state.
Default is protected; atomic writes compare the expected catalog before saving.

## Alternatives And Consequences

A fixed-duration fake loading animation was rejected: it could finish before the
cluster did. An unbounded parallel scheduler was rejected: it defeats budgets.
A second filter implementation was rejected: table, radar and counts use one
proxy over the canonical cache. Saved preset file schemas differ across format versions;
profile migration requires a separate verified importer.

Older native binaries reject version 3 view documents instead of rewriting them. Keep the
original profile before any intentional rollback; do not downgrade documents
by deleting unknown state.

## Verification

Public UI checks cover exclusivity, reset, filter composition, session isolation,
preset load/overwrite/delete/restart, one initial sound for matching audible
rules, monotonic progress
and overlapping external HTTP reads. Store checks cover atomic conflict/busy,
private permissions, protected default, malformed/future documents, symbolic
links and size limits. Broader scheduler, inspector and mutation regressions
remain part of the complete native suite.
