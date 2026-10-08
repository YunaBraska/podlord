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

Alarm value cells support the platform Copy shortcut and Menu/Shift+F10 without changing a rule or requesting cluster data. Up/Down/Home/End retain the selected column while changing the selected rule. Focus is visibly outlined. The native selection model owns the current cell; Tab enters the table, arrows traverse its columns and rows, and Return activates the current alarm enable control. The table retains focus visibility during narrow-window resizing; its horizontal extent is derived from its existing column definitions rather than an undersized fixed width. These controls do not imply that the remaining column-toolbox parity is complete.

### How Does The Values Inspector Preserve Secret Boundaries?

Values reuses the same virtualized grid, column editor, Find, clipboard and
plain-text overflow presentation. Its model owns Key, Encoding, Value, Copy and
Reveal, matching the reference inspector. Only the first three columns sort;
action headings are not presented as sortable keyboard controls. KEY and VALUE
remain direct buttons; RAW and DEC are available for encoded values. The existing
clipboard owner performs decoding and reports invalid encoding without replacing
the clipboard. Reveal/Hide remains an explicit action for Secret values.

One masked table projection owns displayed values. Full-value sorting, Find and
tooltips read that projection, never concealed raw Secret fields. No duplicate
stored value list or per-row popup/timer is added. Private values enter presentation
only after explicit reveal and are cleared on inspector scope changes. Layout and
sort persistence contain column IDs and presentation state, not resource values.
Refresh retains the inspector's existing reading-position owner.

Layout version 6 adds `value`; session views version 5 adds its sort state. Valid
older documents receive only the missing defaults on read and remain unchanged
until an explicit save. Invalid or newer documents fail without replacement.
Rollback requires a compatible executable or a preserved older private profile,
not destructive rewriting. Values now has scoped public-boundary evidence;
Alerts/Diagnostics tools and complete paired visual parity remain separate gates.

### How Does A Session Switch Differ From A Cache Refresh?

Resource and Event model identities are scoped to their session. Switching the
session replaces the published model once using the standard Qt model-reset
boundary; equal resource paths in different sessions do not retain selection
identity. A refresh within the same session continues using incremental updates
so scroll and selected-resource identity can survive fresh data. Unscoped model
callers keep the existing incremental contract, including cluster-label rebinding.

Hidden resource grids disconnect their rendered table models while retaining the
shared cache and view state. Radar glyphs are instantiated only at readable zoom
sizes. Compact Radar buttons reserve sufficient content space in both control
styles. Text sorting fetches each operand once and uses Qt's string comparison;
locale-aware, numeric and timestamp sorting retain Qt's proxy implementation.
When a hidden grid becomes visible, its saved column order is applied only after
the model exposes its columns. Qt's current index mapping is checked before
clearing an existing order. Reapplying an unchanged order can recycle the focused
delegate during keyboard input and destabilize rapid filtering of a large table;
it provides no presentation benefit. Menus and overflow tips close on hiding.
These changes do not relax the latency or memory gates and add no network or
storage work to rendering.

### How Does A Cached Session Switch Avoid Rebuilding The Table?

The existing incremental row publisher handles snapshot changes across sessions,
including equal API paths with different metadata. A changed session or inspector
scope clears the shared native selection, menu and overflow target explicitly;
matching paths do not carry a selected entity into another context. This removes
the separate model-reset branch and its duplicate identity-scope state. It does
not introduce another model/cache or bypass asynchronous session persistence.

Focused cells synchronize the native selection model's current index without
selecting a row or opening the inspector. Keyboard copying and navigation then
refer to the actually focused logical column, including pinned/reordered columns.

Qt Dialog automatically connects its DialogButtonBox footer's decision signals.
Forwarding those same signals again delivers two decisions for one click and is
removed. The YAML preview clears the footer ListView's initial current item before
opening and assigning keyboard focus: its deferred current-item creation must
not steal focus for Apply. No timer, delayed focus retry or custom dialog is used.

Row-wide filter invalidation listens to the existing row-metadata role across all
columns. Presentation changes keep their bounded display-column notification;
metadata-only updates no longer invalidate every cell's display text. A changed
cluster still invalidates row predicates, including when source JSON is otherwise
identical. No second cache, polling loop or hidden mutation is introduced.

### How Do Alarm Rules Reuse The Table Contract?

The existing alarm controller publishes a read-only Qt item model from its
canonical rules and current match counts. Native proxy sorting handles typed
Enabled and Active values as well as the textual columns. The shared grid owns
Find, selection, keyboard/context copying, overflow and column controls; the
editor continues to own an unsaved rule draft. No second JavaScript row catalog,
manual sort, navigation loop, refresh timer or Kubernetes request is added.

The model's stable rule ID, not its sorted row number, identifies an edited or
copied rule. Moving the current table selection opens that rule's existing
editor; model refresh does not overwrite an unsaved draft. Built-in definitions
remain locked and enabling a rule still uses the existing validated save path.

Layout version 7 adds `alert`. Reading valid versions 1 through 6 appends only
missing model-derived alarm defaults without writing or discarding other table
choices. The next explicit atomic save writes version 7. Missing current-schema
tables, unknown types and malformed layouts fail without replacement. Rollback
requires a compatible binary or a preserved older profile, not rewriting away
column choices. Executed evidence and remaining parity gaps belong to the test
map; this decision alone does not establish release readiness.

Alarm match updates publish only the Active column. Static conditions, actions and
sound labels change with definitions, not presentation ticks. Current-cell
presentation derives from the shared canonical selection index, including after
Qt recycles a delegate. Table positioning has no extra animation, and the grid
reserves enough height for its toolbar, header and at least one readable row.

### How Do Native Diagnostics Reuse The Table Contract?

Runtime counters and the bounded request audit reuse the existing snapshot model,
Qt sorting proxy, virtualized grid, Find navigation, overflow tip and column
editor. Entering the visible Diagnostics section or explicitly refreshing it
publishes one snapshot and its UTC observation time. The UI keeps only that time,
not another copy of both datasets. Hidden tables detach their rendered models;
there is no diagnostics timer, request, credential retry or storage read from
paint, focus, hover, scrolling, sorting, finding or copying.

The operating system owns RSS, physical/private memory, cumulative process CPU
seconds and thread count. Unavailable counters remain unavailable. macOS physical
footprint is not labeled private virtual memory; cumulative CPU seconds are not
presented as a utilization percentage. Native code does not invent .NET heap or
GC counters. Cache-byte accounting and remaining reference telemetry still need
separate evidence.

Request-duration text is converted once at snapshot publication into the shared
model's typed sort role. Its displayed milliseconds remain unchanged. Queued or
otherwise unmeasured durations do not become measured zeroes. Clipboard actions
resolve the retained row identity, not the current sorted position. Each table's
Find shortcut follows focus so the two visible tables do not compete for it.

Layout version 8 adds `diagnostic` and `audit`. Reading versions 1 through 7 adds
only missing model-derived defaults without changing existing layouts or writing
the profile. Explicit save uses the existing lock, optimistic conflict check and
atomic replacement. Missing current-version tables, unknown types and invalid
columns remain errors. Older binaries must reject a version-8 profile rather
than erase these columns; retain a compatible binary or the pre-save profile for
rollback. No session-view or credential format changes with this increment.

Executed scenarios, failed regressions and image limitations are recorded in the
existing test map. Shared table behavior does not establish complete Settings,
whole-view, physical-device or release parity.
