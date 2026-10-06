# 0011 Session-Scoped Filtered Resource Presentation

## Status

Accepted on 2026-10-02. Implementation conformance remains to be reviewed.

## Context

Operators navigate between sessions while background requests continue. Sharing filters across sessions or binding late responses to whichever session is currently active mixes unrelated contexts. Independently filtering a radar and resource table can also give conflicting accounts of the same selected scope.

## Decision

Use FLT-001 through FLT-010 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-filter-and-session-presentation-requirements) as the owning behavioral contract. Keep filter state session-specific and sort state specific to a session/table-type pair. Keep column layout per table type as already decided in [ADR 0010](0010-consistent-table-interaction-and-layout.md).

Derive table membership, radar membership, and corresponding counts from the same filtered cached resource set. Visible table rows are only a viewport, not the authoritative scope. Filtering changes presentation, not the session's synchronization scope. Bind response processing to its originating session rather than the currently visible session.

## Alternatives And Trade-offs

- Global filters reduce retained state but leak presentation choices across sessions. Independent session state is preferred.
- Filtering each surface independently permits inconsistent membership and counts. A shared semantic resource set avoids that inconsistency without prescribing a new framework.
- Fetching on filter changes adds network latency and load. Local evaluation uses the cache-first model while normal background synchronization continues for hidden resources.

## Consequences

The later review must cover session switching, delayed responses, table scrolling, filter reset, and alarm non-replay through visible behavior. This decision does not select a persistence format or settle filter-dependent alert evaluation; restart persistence and quantitative responsiveness remain separate interview topics.

## Whole-Session Alert Evaluation

Confirmed on 2026-10-03. ALT-001/002 in the operational specification resolve
the earlier alert-scope question: rules evaluate their complete owning session
scope even when presentation filters hide a matching resource. Filtered radar
and table membership remain unchanged; session switching does not replay alarms.
This avoids letting a navigation filter silently disable operational detection.
It does not authorize cross-session evaluation or invent rule/matcher defaults.

The native radar reads the same filtered Qt model as the table. Qt's standard
virtualized [GridView](https://doc.qt.io/qt-6/qml-qtquick-gridview.html) provides
cached item presentation, scrolling and keyboard navigation without a second
resource index, animation owner or network path. C++ owns the resource and
session state. This implementation choice is not evidence of full legacy camera,
effects, alert behavior, accessibility or performance equivalence.

### How Are Duplicate Event API Representations Handled?

The real local Kubernetes comparison returned 130 Core Events and 130 modern Events with 130 matching UIDs. Rendering both paths counted the same resource twice. RWT-018 therefore makes the shared session resource projection the owner of Event alias resolution. Identity is the Event UID, namespace and name, not the related resource UID. The modern representation wins when both exist; all cached API paths remain available for explicit detail access. No new cache, UI-specific deduplication or network request is introduced.
