# 0009 Explicit Secret Reveal And Copy

## Status

Accepted on 2026-10-01. Implementation conformance remains to be reviewed.

## Context

The inspector exposes Secret values through value views, YAML, and apply previews. Hiding only decoded values leaves the encoded YAML representation exposed. A reveal must also not become permission to expose other values or populate the clipboard.

## Decision

Use SEC-001 through SEC-008 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-secret-presentation-requirements) as the owning behavioral contract. Use explicit per-value reveal and copy actions, reset reveal state when leaving the inspected scope, and keep app-generated diagnostics and audit records free of Secret values.

Keep presentation masking separate from the actual data used for authorized editing and apply. Masking is not a transformation of the resource and must not corrupt an apply payload. Existing fresh-YAML and apply safeguards remain governed by [ADR 0008](0008-protect-yaml-editing-and-apply.md).

## Alternatives And Trade-offs

- Revealing an entire Secret by default is convenient but exposes more values than the requested operation needs. Per-value actions keep disclosure deliberate.
- Showing base64 YAML while hiding decoded values does not protect confidential data: [Kubernetes guidance](https://kubernetes.io/docs/concepts/security/secrets-good-practices/#avoid-sharing-secret-manifests) explicitly distinguishes base64 encoding from encryption.
- Resetting reveal state requires another reveal after returning, but avoids carrying disclosure into a different resource or session.

## Consequences

Values, YAML, diffs, clipboard actions, and diagnostics must be checked together at their public boundaries. Tests must establish that presentation redaction never becomes applied resource content. This decision does not claim that hiding UI values secures the operating-system clipboard or changes Kubernetes authorization.
