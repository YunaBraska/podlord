# How do native field filters preserve cached presentation and stored views?

Status: accepted for text-field filtering; typed quantity filters remain pending.

## Which behavior is authoritative?

The retained C# matcher combines alternatives inside an expression with OR and
different fields plus global search with AND. The native implementation reuses
one parser for global search, field expressions and exact-value selection.
It does not introduce a second expression grammar or fetch resources to filter.
The filtered proxy remains the single membership source for table, radar and
counts. Alarm evaluation continues using the complete session cache.

The user confirmed a deliberate quantity-filter difference on 2026-10-04:
CPU, memory and storage compare actual cached measurements only. Missing
measurements never fall back to limits, requests or capacity and never become
zero. References remain visible separately. This decision does not claim that
typed quantity filters have already been implemented.

## How does value selection remain stable?

An explicit opening or Reload values action snapshots the supported field's
values from the complete current session cache. The open list does not rebuild
on background refresh or every keystroke. Case-insensitive option search changes
visibility only. Selecting known values adds quoted exact alternatives without
dropping custom expressions. Reloading retains exact selected values no longer
present in the cache. Invalid expressions stay visible; picker actions do not
silently replace them with an unfiltered query.

The picker captures its owning session. Switching sessions closes it, and
actions cannot alter another session using the previous option snapshot. No
timer, separate persistence owner or networking path is added.

## Where are field expressions persisted?

Extend ADR 0027's existing session view record to version 2 with a field-expression
map using actual stable column IDs. Version 1 reads as an empty field map without
rewriting. An unchanged explicit save also leaves version 1 untouched. A changed
save atomically writes version 2 using the existing lock, same-table expected
state check and independent-table merge. Unknown fields, invalid value types,
empty stored expressions, unsupported versions and oversized records fail
explicitly without replacing existing data.

Older executables reject version 2 rather than silently deleting its filters.
Migration rollback therefore means using a preserved pre-change private profile
or explicitly clearing the affected view record with a user decision, not
rewriting a newer record behind the user's back.

## What does this increment not establish?

The text-field scope is Name, Kind, Namespace, Status, Node, Image, Cluster,
Owner and Issue. Typed quantity/age/readiness/restart filters, Problems/Activity,
UID filtering and preset management require their own implementation and evidence.
Tests through the real QML and public store boundaries are recorded in the
existing test map. The external HTTP fake replaces Kubernetes only.
This increment is not a whole-product parity or release verdict.

## What implements the confirmed measurement-source decision?

The native numeric CPU, memory and storage field predicates now compare the
canonical cached sample, not displayed text or configured reference values.
The existing metricQuantity parser owns Kubernetes unit interpretation. Display
aliases are normalized once at expression compilation. No numeric filter starts
a request, refresh loop or timer. Missing samples cannot match numeric predicates,
including =0 and >=0; an actual measured zero can.

Range comparisons are conjunctive and exact quantity alternatives are disjunctive,
matching the reference application's numeric combination behavior. Invalid numeric
expressions fail visibly without reverting to an unfiltered view. The dialog shows
quantity-specific units and combination rules. Its option snapshot contains actual
canonical measurements formatted without stale/incomplete suffixes, so selection
cannot reinterpret annotations as units.

The current increment covers numeric predicates. The reference application's
text/regex fallback for quantity display strings remains an explicit migration gap;
it is not silently treated as complete. Positive storage filtering cannot be claimed
from a capacity-only volume: the currently integrated Metrics API supplies CPU and
memory, not storage usage. A supported storage measurement source remains necessary.

## How is the existing quantity-display grammar preserved?

The subsequent compatibility increment closes the previously listed text/regex
fallback gap. When there are numeric terms, range comparisons all match and exact
numeric alternatives use OR; nonnumeric terms do not substitute measurements.
Without numeric terms, the same bounded text matcher handles display substrings,
quoted exact values, prefix/suffix and regular expressions. An explicitly authored
text search for the unavailable marker can find missing display values; it does
not turn those values into numeric zero. Quoted strings remain exact display
strings, including stale/incomplete annotations, rather than silently becoming
numeric comparisons.

Human filter unit aliases remain compatible with the reference: CPU accepts m,
mCPU, millicore(s), core(s) and c; byte units are case-insensitive. In this UI
syntax m/MB means megabytes, mi/MiB means mebibytes. This does not alter Kubernetes
API quantity interpretation: source ingestion keeps its canonical, case-sensitive
quantity grammar. Alias normalization happens at the filter boundary, then the
existing metricQuantity implementation owns arithmetic, finiteness and units.
Numeric-looking malformed values and malformed regex fail visibly. Spaced
operators and display-unit spacing are supported without the reference's accidental
interpretation of a displayed 500 mCPU as 500 cores.

Individual sidebar fields use a nonmodal flyout anchored to their button, matching
the reference application's interaction. Bounds are clamped to the window; the
remaining workspace stays operable. Escape, an outside press and session changes
close the flyout. Text and checkbox edits share the existing cached filter owner;
opening, searching options and filtering never schedule API requests.

The complete field picker and narrow-window field selection remain centered modal
dialogs. Both presentations reuse the same dialog and option snapshot; there is
no second filter model, timer or persistence. Checkbox borders and keyboard focus
remain visible in the HUD palette. Public UI cases cover placement, keyboard
selection, cache-only editing, navigation, viewport edges and session isolation.
