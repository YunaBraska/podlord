# Where do session filters and sort order live?

Status: accepted; desktop verification is required before release.

## Why is another persisted record needed?

The session catalog already owns session identity, open tabs, activation and usage.
It does not contain resource/event filters or sort order. A real desktop restart
restored the tab but lost its filter and descending Name sort. In-memory navigation
is not a durable source for these preferences. Existing table-layout records own
column presentation, not session filters.

## What is the decision?

Store resource, event and port filter/sort preferences, plus inspector Event
and Link sort preferences, in one versioned JSON record per
canonical session UUID under the private profile's `views` directory. Use stable
column IDs from the actual table schemas, never display labels or column indexes.
A missing record means empty filters and NONE sorting. Reading must not create a
profile, overwrite malformed data, authenticate, or fetch Kubernetes resources.

Explicit filter/sort actions update the cached UI immediately. One asynchronous
writer serializes saves and coalesces unsent edits for each session/table. Each
save locks its record, checks the expected state of the affected table, merges
unrelated table changes and atomically replaces the private file. Unsupported,
oversized, malformed, inaccessible or symlinked inputs fail explicitly. A failed
session does not prevent independent sessions from saving. Only pending edits of
the failed session are stopped; unrelated pending saves are still drained. Errors
remain visible, with the active session's error preferred when applicable.

Record version 5 owns those six actual table schemas, including inspector
Values sorting. Reading versions 1 through 4 adds empty defaults only for
known, missing auxiliary tables and does
not write. Unknown tables and incomplete current records fail explicitly. The
existing atomic writer performs the next upgrade; malformed files remain
untouched. Port preferences never restart saved forwards, and inspector sort
restoration never starts a detail request. Find remains transient navigation.

Normal window closure waits asynchronously for pending saves. A save failure
offers Stay or Close without saving, with Stay as the default. Explicit reload
replaces local filter/sort preferences with the last valid saved records; failure
does not silently reset them. Direct object destruction drains dispatched work
and pending writes before profile lifetime ends. It reports shutdown failures
without exposing filter contents.

## What is deliberately not included?

This does not duplicate session catalogs, persist Kubernetes snapshots, change
authentication policy, add a polling timer, or implement window geometry and
session-specific column layout. Closing a tab retains its preferences because the
session still exists. Session deletion must eventually remove its owned view
record as part of the same user-visible deletion workflow.

## How is the boundary checked?

Public store tests cover schema validation, stable IDs, conflicts, table/session
isolation, locking, atomic persistence and preservation of invalid input. Native
QML tests exercise real filter/header controls, close/reopen, saved-state recovery
and close-failure decisions using only an external Kubernetes HTTP fake. Actual
process restart against an owned local Kubernetes cluster supplies desktop
evidence. These checks do not prove unimplemented session deletion or mobile
packaging.

## How Are Existing Named Filters Imported?

Explicit import reads the existing C# saved-filter array or native preset record
through the same bounded store boundary. Field names map to stable native IDs;
Problems and Activity retain their distinct modes. As in the reference loader,
the old saved ID does not become a resource predicate. Its display Limit is not
a predicate either; this does not implement the missing row-limit control.

Import keeps the source bytes unchanged, protects the empty default, rejects
conflicting names rather than replacing existing filters, and uses the existing
locked atomic writer. Merely loading or selecting a preset cannot fetch resources.
The migration runner compiles the actual reference store to produce its input;
its screenshots prove native headless controls, not desktop visual parity.
