# Why share the reference shell without pretending feature parity?

Status: Accepted for the bounded shell increment, 2026-10-05.

## Context

The native implementation separated Radar, Alerts and usage details into top-level
pages. The reference keeps the resource/event/task workspace next to one Radar
and filter sidebar. That difference obstructed visual comparison and discoverability.

## Decision

Use Resources, Events, Ports and Settings as the primary navigation. Keep one
Radar/filter sidebar mounted beside the workspace on wide windows and move that
same instance into a right-hand drawer on narrow windows. Do not retain the old
Radar route. Cached usage details remain reachable from the header gauges;
Alerts are a Settings section. Settings sections wrap instead of becoming
inaccessible behind the inspector.

Preserve the existing source/session, inspector, queue, authentication, forward
and settings owners. The presentation does not create network calls, new timers
or a second camera/filter store. Unsupported actions are disabled with an
explicit reason rather than connected to an empty implementation.

The five header cards summarize the filtered canonical snapshot. CPU and memory
use Nodes when present, otherwise Pods, never both: counting Node usage together
with its Pods would count the same workload twice. Storage uses Node capacity
when present, otherwise claims, otherwise volumes; claims and volumes are not
added together. A limit or capacity is a reference, never an observed value.
Unknown, incomplete and stale values remain explicit; overflow fails to an
unavailable measurement. Aggregation belongs to the existing metric owner and
runs when cached data/filter state changes, not during paint or hover.

## Consequences and rollback

The visible shell is comparable without declaring the missing filter presets,
shortcuts or Settings sections complete. Header aggregation adds bounded work
per published filtered snapshot and requires fresh performance evidence. Alarm
evaluation and the health strip continue to use the entire session.

No new persistent schema is introduced. Existing profiles and the independently
runnable reference remain available. Rolling back this increment restores the
previous executable/layout without converting session or source records.

## Evidence

The existing [test map](../spec/k3d-test-map.md) records public-window navigation,
forward task data transfer/actions, all appearance selectors, metric summaries,
local Kubernetes screenshots, full-suite results and remaining acceptance gaps.

### Filtered terrain and inline settings refinement

The complete cached radar island remains visible when filters exclude resources. Excluded cells are painted once from viewport buckets, without interactive or animated QML delegates. This preserves spatial context without increasing delegate count or permitting inspection of excluded resources.

New table defaults use reference order and widths, while saved layouts remain authoritative. Defaults and reset share the C++ layout owner. Inline Settings reuse existing asynchronous policy/store entrypoints; diagnostics are explicitly refreshed snapshots. Completed request auditing is bounded to 200 in-memory metadata entries, is session-scoped, and excludes payloads, query parameters and credentials. No additional network work or timer is introduced by opening Settings.

### Deterministic reference geometry and selectable namespaces

Native uses the reference session/UID identity and virtual decoration keys for
jitter and stable collision ordering. The real C# pipeline exports a single
immutable resource projection and its public radar blocks; native compares its
public cache-model geometry and rendered centers against that output. No copied
reference algorithm or private helper is used as the oracle.

Actual Namespace resources remain selectable, unlike the reference decoration-only
behavior. Namespace aliases are placed after the canonical scene, reusing an
existing anchor or a free cell without moving reference resources. The default
ring for unlisted kinds is four, while their terrain remains grass.

Camera persistence is unchanged; the scope is supplied by the existing active
session, not a new store. A corrected scene can move relative to an older saved
camera; Reset view remains available. Rolling back the binary restores the old
geometry without a profile conversion. Geometry and filtered-terrain ownership,
viewport buckets and foreground-only delegates remain unchanged.

### How does the Alerts section reuse the reference arrangement?

Keep the existing alarm engine and private atomic store. Replace the native
side-by-side rule-button pane with the reference table/action-bar/editor order.
Flat Settings section labels use the selected section as their only selection
owner, avoiding independently toggled checked state. Table ordering is local
presentation; stable catalog order remains authoritative and NONE restores it.

Matcher-group edits produce the existing rule schema. Failure retains the draft
and committed catalog. A bounded Qt clipboard bridge serves explicit table copy;
no clipboard library, network action, persistence migration or new timer is added.
Derived table rows are updated only for a visible Alerts view. Rolling back the
executable restores the previous layout without converting any saved rule.

### How does the shell avoid permanent diagnostic controls?

Search is a transient presentation surface over the existing cached queries;
hiding it does not clear a filter or create a second filter owner. A highlighted
search toggle discloses a retained query. Commands and explicit synchronization
move into the workspace menu. Authentication and actionable failures do not
depend on opening search. Resets and table tools reuse the native vector glyph
renderer, not font symbols or another icon dependency.

The footer has one fixed line with elision and the full accessible status. Narrow
navigation uses the same actions with 44-pixel targets. One scrollable filter
surface sits below the retained Radar; there is no additional camera, timer,
request owner or profile schema. Binary rollback restores the previous shell.
