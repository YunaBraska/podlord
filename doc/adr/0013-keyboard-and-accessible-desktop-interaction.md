# 0013 Keyboard And Accessible Desktop Interaction

## Status

Accepted on 2026-10-02. Implementation conformance remains to be reviewed.

## Context

Mouse-only actions, invisible or lost focus, color-only meaning, and clipped controls prevent reliable operation of a desktop console. Confirmation and draft protection must continue to work when interactions are driven by the keyboard or assistive technology.

## Decision

Use ACC-001 through ACC-009 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-keyboard-and-accessibility-requirements) as the owning behavioral contract. Make core operations keyboard-accessible, preserve meaningful focus across dialogs, expose understandable accessible descriptions, and retain information without reliance on color or non-essential motion.

Keyboard cancellation is not authorization to apply changes or discard a draft. Existing confirmation and draft-protection contracts remain in force for every input method.

## Alternatives And Trade-offs

- Hover-only or mouse-only controls can reduce visible UI but exclude keyboard operation. Provide an equivalent keyboard path rather than a separate product workflow.
- Color and animation can supplement status but cannot be its only expression. Retain understandable information with reduced motion or without color distinctions.
- Fixed layouts simplify implementation but can hide actions when text grows or windows shrink. Keep content and actions reachable without prescribing a new layout framework.

## Consequences

Later verification must use the real public desktop UI and supported assistive technology. Automated component checks alone do not establish keyboard, focus, screen-reader, or small-window behavior. This decision makes no legal-compliance or platform-certification claim.

## How Does Inspector History Preserve Its Target?

The native inspector exposes visible Back/Forward controls and the platform's
standard keyboard bindings. It retains the reference application's 32-visit
bound, consecutive-visit deduplication, and insertion before an existing forward
branch. History belongs to its session, not to the whole window, and is not
persisted as a second resource catalogue.

Availability reads the current session collection snapshot. Retained detail-only
entries may remain readable in an already open inspector, but cannot resurrect a
resource removed from that snapshot through history. Presentation filters do
not remove history targets. An explicit visit displays cached content immediately
and admits a foreground detail read through the existing request owner; checking
history availability never fetches or performs authentication.

The existing unsaved-YAML decision freezes the session, path and history position.
Stay and Escape preserve the draft. Discard clears only the explicitly approved
draft; if the frozen target disappears before confirmation, navigation fails
explicitly instead of selecting a different entry. Authentication failure does
not prevent cached navigation or authorize automatic login, and does not grant
fresh-YAML editing authority.
