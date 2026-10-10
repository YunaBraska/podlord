# How does the radar stay accessible with thousands of resources?

Status: Accepted
Date: 2026-10-10

## Context

ACC-001, ACC-006 and ACC-007 require understandable navigation and non-color
resource identity. A loaded native radar published every map tile as an
accessible button. In the real-cluster desktop comparison, the Settings tree
became unavailable while the sidebar was shown and returned when it was hidden.
The reference canvas does not expose thousands of individual map controls.

## Decision

Expose the native radar as a named accessible grouping with the complete
filtered count and keyboard guidance. Reuse its existing focused/hovered target
state to publish individual target buttons only when relevant. The resource
table and its filters remain the authoritative complete accessible list.

Hover and keyboard focus may identify different targets simultaneously; this
is bounded presentation, not a promise of exactly one node in every state.
Keep all rendered tiles, pointer hit targets, Home/End and arrow navigation,
pan/zoom, inspector activation, topology and cache semantics unchanged. Add no
second model, network reads, timer or persistence format.

## Alternatives and consequences

Publishing every tile repeats an already available table and scales the native
accessibility tree with the cache. Hiding the entire map would lose useful
focused-target feedback. Keeping the overview and relevant targets avoids both.
Deleting the optimization restores the old accessibility presentation without
changing stored data or resource discovery.

Public Qt accessibility tests use 1,001 resources and both supported control
styles. Actual packaged macOS evidence is separate from component tests, and
neither constitutes VoiceOver, mobile or legal-compliance certification.

## What Did The Packaged Cocoa Check Establish?

The 2026-10-10 local K3d desktop check exposed a named grouping containing
1,842 matching resources. Home exposed ComponentStatus/controller-manager;
End exposed StorageClass/local-path; Enter opened that last resource in the
inspector. The public accessibility snapshots are retained under
`2026-10-10-cocoa-radar-overview`. This closes the packaged-grouping check,
not assistive-technology certification or whole-product image parity.
