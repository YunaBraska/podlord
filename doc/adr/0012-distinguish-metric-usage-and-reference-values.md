# 0012 Distinguish Metric Usage And Reference Values

## Status

Accepted on 2026-10-02. Implementation conformance remains to be reviewed.

## Context

Configured requests and limits, provisioned capacity, and measured usage answer different questions. Conflating these values or substituting zero for missing measurements can mislead operational decisions. Partial aggregates and old measurements also need visible qualification.

The Kubernetes [resource metrics pipeline](https://kubernetes.io/docs/tasks/debug/debug-cluster/resource-metrics-pipeline/) provides CPU and memory usage; it does not provide general volume occupancy. Kubernetes [resource management](https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/) distinguishes requests and limits from actual consumption.

## Decision

Use MET-001 through MET-011 in the [operational specification](../spec/podlord-operational-spec.md#confirmed-resource-metric-presentation-requirements) as the owning behavioral contract. Preserve the meaning, units, reference quantities, availability, completeness, and freshness of presented metrics. Display recommendations only as sourced recommendations, not as inferred measurements or invented estimates.

## Alternatives And Trade-offs

- A single unlabeled percentage is compact but hides its denominator. Explicit references permit correct interpretation, including usage above the reference.
- Substituting zero or capacity for unavailable usage fills the display but misrepresents evidence. An explicit unavailable state is preferred.
- An always-present recommendation marker looks informative but invents advice without a source. Omit it when no substantiated recommendation is available.

## Consequences

The later review must trace displayed values to their actual sources and exercise missing, stale, partial, and above-reference measurements through public surfaces. This decision does not choose new providers, invent a recommendation algorithm, or claim that existing visualizations already conform.

## Native Implementation Evidence

The native ResourceClient now normalizes discovered `metrics.k8s.io` PodMetrics and NodeMetrics collections into the same session snapshot as core-v1 resources. Metric samples are not extra resource-table or radar objects. CPU is normalized to millicores; memory and storage references use bytes. Requests, limits, Pod-level overrides, node capacity and volume provisioning remain configured references rather than observed usage.

Pod aggregation currently includes configured regular containers and restartable init sidecars. Missing measurements are visibly incomplete. Samples older than the resource's creation, in the future, or without a verifiable resource UID/creation time are not attached. The existing central expiry owner marks measurements stale after 25 seconds; surfaces do not start their own polling. Metrics API failure retains the previous accepted collection. Storage occupancy and recommendations remain unavailable without an actual source.

Dashboard and inspector use the same normalized gauge representation, including labeled reference markers, above-reference percentages, units, timestamps and completeness. Public UI and normalization tests cover this increment; they do not prove the entire MET contract, full legacy dashboard parity or all possible transient init/ephemeral-container aggregation. Those gaps remain explicit in the test map.
