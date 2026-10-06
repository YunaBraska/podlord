# 0010 Consistent Table Interaction And Layout

## Status

Accepted on 2026-10-02. Implementation conformance remains to be reviewed.

## Context

Inconsistent sorting, column controls, copying, and overflow handling force operators to relearn each table. Losing layout or reading position during refresh adds navigation work without adding information.

## Decision

Use TBL-001 through TBL-012 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-shared-table-requirements) as the owning shared table contract. Resource-row navigation remains governed by INS-001 through INS-003 rather than introducing a second interaction definition.

Save column layout per table type so repeated visits and app restarts preserve the operator's chosen presentation. Keep data refresh independent of the user's selection and reading position wherever the corresponding record remains displayed. Overflow presentation reads available content; it is not a reason to fetch data.

## Alternatives And Trade-offs

- Independent table conventions reduce initial coordination but make navigation and controls inconsistent. Shared observable behavior is preferred without prescribing a custom table framework.
- Transient column layout avoids persistence but repeatedly discards user choices. Per-table-type persistence preserves useful layout without coupling unrelated tables.
- Colors alone provide compact identification but are insufficient without text. Consistent colors supplement, rather than replace, labels.

## Consequences

The later review must exercise each applicable table through visible controls, verify typed sorting and saved layouts, and check selection and reading-position stability during refresh. This decision does not prescribe a UI component, storage format, or sorting algorithm and does not claim that existing tables already conform.

## How Does The Native Application Own Column Layouts?

Resources, Events and Ports share the native column controls and virtualized table component. Stable column IDs come from each actual model's headers, not a separate presentation catalog. The pinned and scrolling regions use the same filtered model and selection, with Qt's vertical synchronization; pinning creates no resource copy, cache or network owner. Narrow pinned regions retain native horizontal scrolling rather than making excess pinned columns unreachable.

The profile's private `table-layouts.json` owns only per-table layout. Save runs off the GUI thread, locks and atomically replaces the file, and compares the saved table against the originating window's cached layout. Other table types are merged from the current authoritative file. A conflicting or invalid save retains both the saved file and the editable dialog. Explicit reload handles conflicts; malformed persisted data is not silently overwritten. Layout reads during painting, hovering or scrolling remain memory-only. No additional timer or background layout refresh is introduced.

Column editing uses Qt's stable list model so changing a checkbox, width or position does not recreate the entire dialog's controls. Visibility, pinning and widths remain distinct from session-specific filtering and sorting. Verification belongs to the existing native test map; this implementation does not establish parity for other still-missing Settings or inspector capabilities.

### Native implementation constraints confirmed by regression evidence

The two synchronized table views have one column-reorder owner. Reorder/clear
requests on a view with `syncView` are forwarded, so applying them to both views
would move columns twice. Width providers use visual positions; delegate/header
column properties identify logical model columns. Persisted IDs and explicit
visual ordering keep these separate. These behaviors follow the
[Qt TableView contract](https://doc.qt.io/qt-6.11/qml-qtquick-tableview.html).
Non-first Namespace pin, copy/sort and all-pinned scrolling are public UI
regressions in Basic and Fusion. Overflow popups are limited to clipped content,
so fully readable cells do not unnecessarily cover subsequent header actions.
Resources, Events and Ports are implemented; this decision does not mark other table
surfaces or full legacy parity complete.

### How Are Resource Fields And Older Layouts Preserved?

The resource model owns the fifteen data-column IDs in the shared-table
specification. API ingestion owns group-qualified workload fields and status;
the existing source catalog owns Cluster; the metric normalizer owns measurements.
Cluster is held once in the active table projection, not copied into every cached
JSON row. Changing that projection emits the affected column roles so filtering,
sorting, colors and text follow the active session even when resource paths match.
The table and inspector share the same quantity formatter. Sorting reads typed
roles without formatting display text; identity colors apply only to categorical
identities. No paint, tooltip, selection or layout path starts a request.

A valid version-1 six-column native resource layout is decoded against its exact
original schema and extended with the new model IDs. Loading is read-only. The
next explicit, locked, conflict-checked atomic save writes version 4. Invalid
layouts remain errors; this is a bounded schema migration, not a general fallback
or a second layout authority. Version 4 must be supported before an older native
binary can consume the upgraded file; the preserved C# comparison profile remains
independent.

Hover-enabled native delegates expose the shared plain-text overflow tip for both
mouse and keyboard users. No per-cell popup, refresh timer, network owner or
additional table framework is introduced. Canonical row updates retain the user's
selection and reading position. The test map records both regression failures
before fixes and subsequent executed evidence; visual parity remains a separate gate.

### How Does The Ports Table Preserve Forward Ownership And Older Layouts?

Reuse the existing snapshot model, typed Qt proxy sorting, virtualized grid,
plain-text overflow tip and column editor. Forward tokens, not resource paths,
identify rows: two independently owned forwards must never collapse into one
row. The transport remains the sole producer; the model updates on actual
forward changes and session activation. Filtering and sort choices belong to
the existing session navigation state and do not restart or transfer forwards.
Only explicit resource inspection can request fresh detail. Paint, filtering,
sorting, layout and clipboard operations remain cache-only.

Keep endpoint copy, HTTP/HTTPS opening and stopping in the existing controller.
The toolbar and context menu share native Actions. Whole-row mouse/keyboard
inspection replaces the redundant per-row Inspect button. No per-row timer,
second cache, table framework or browser engine is introduced.

Layout version 4 adds Ports. Loading valid versions 1 through 3 extends only the
missing Ports layout with model-derived defaults and preserves the saved
Resources/Events layouts without writing. Unknown types, malformed layouts,
missing version-4 types and newer versions remain explicit errors. The next
explicit atomic save writes all supported layouts as version 4. Older binaries
must refuse this profile rather than erase the added layout; preserve the
profile and use a compatible binary for rollback.

### How Do Inspector Events And Links Use The Same Contract?

Inspector Events and Links reuse the same virtualized grid, native typed proxy
sorting, Find navigation, column editor, overflow presentation and clipboard
actions. The detail cache owns their projections; presentation does not fetch.
Link source and destination cells inspect their own resource endpoint, including
keyboard and context-menu actions. Copy keeps the canonical relationship row
identity, independently of the endpoint opened by inspection.

Layout version 5 adds `inspectorEvent` and `inspectorLink`. Valid older layouts
receive only these missing model-derived defaults on read; saving atomically
writes all five supported types. Missing version-5 tables and unknown types are
errors. This supersedes the version-4 writer above without discarding user
column choices. Values, Alerts and Diagnostics still require separate evidence.

### Keyboard access in the alarm rule table

Alarm value cells support the platform Copy shortcut and Menu/Shift+F10 without changing a rule or requesting cluster data. Up/Down/Home/End retain the selected column while changing the selected rule. Focus is visibly outlined. The table owns focus visibility for both cells and enable controls, including narrow-window resizing and reverse Tab traversal; its horizontal extent is derived from its existing column definitions rather than an undersized fixed width. These controls do not imply that the remaining column-toolbox parity is complete.
