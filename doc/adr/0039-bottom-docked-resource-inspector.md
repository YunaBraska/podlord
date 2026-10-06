# Why Does The Resource Inspector Dock Below The Workspace?

Status: accepted

## Context

Actual populated C#/C++ desktop comparisons show that a side-by-side native
inspector consumes the table's column width. The reference instead docks its
inspector below the workspace and leaves Radar and filters at the right.
This visible difference is not resolved by passing resource transport tests.

## Decision

Reuse Qt Quick's existing SplitView: the left workspace gets a vertical split
with the existing inspector underneath it; the right sidebar retains its sole
owner. The user adjusts height with the native handle. Closing the inspector
returns height to the current workspace without replacing its cached model.

Existing inspector actions use a wrapping Flow rather than separate rows of
buttons. Actions keep their original request, authentication, target-identity,
Secret masking and confirmation owners. The layout change grants no new write
or authentication authority and introduces no timer or persistence mechanism.

Overview and Values End/Home navigation targets actual first/last model indexes
rather than estimated list extents. Variable-height, lazily rendered values must
remain reachable through the public keyboard boundary.

## Verification And Limits

Eight Basic/Fusion scenarios failed against the side-by-side layout and passed
after docking below: wide, 390-pixel narrow, actual handle dragging and page
switching followed by close. The broader inspector suite exposed insufficient
reading height and offscreen pointer assumptions; those failures remain visible
in the evidence and require passing corrected public tests before packaging.

Tests share a real wheel-input helper to reveal clipped controls; they do not
invoke private scrolling functions or click beyond the actual window. A separate
four-case alarm-editor check verifies wheel navigation to the final field and
back in normal/narrow Basic/Fusion windows without Kubernetes requests.

Persisting split geometry, all-target accessibility, populated installed-window
comparison after the change and performance budgets remain independent gates.
No earlier screenshot or package is relabeled as this implementation.

## Rollback

The change does not modify user resource data or create a profile schema.
The isolated C# baseline remains available. Reverting presentation must preserve
the inspector's current security and ownership behavior.

## Corrected Public Evidence

The corrected inspector/shell/reference-alert/layout subset passes 174/174
cases. This includes keyboard as well as pointer dock resizing and single-row
HUD cards at wide width. Long/narrow alarm forms pass four real wheel-navigation
cases; this does not establish a fix for the transient blank area observed on the
physical desktop. Open/closed dock frames and the current scoped package identity
are linked in the existing test map. Window geometry persistence and installed
C#/C++ paired visual parity remain separate gates.
