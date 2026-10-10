# Which resident-memory budget applies to the macOS desktop profile?

Status: Accepted
Date: 2026-10-10

## Context

PER-006 originally required at most 250,000,000 resident bytes in the agreed
macOS workload: three sessions, 5,000 resources and visible retained logs.
The user authorized adjusting performance thresholds when further optimization
is not practical. This does not authorize waiving correctness or coverage gates.

The renderer comparison did not identify a useful basic/threaded RSS reduction.
Healthy-cache sharing and normal-weight workspace text retained behavior.
Before bounded radar accessibility, two clean foreground-Cocoa runs measured
285,540,352 and 286,867,456 bytes. After that change, repeated clean runs measured
260,341,760 and 260,096,000 bytes, with warm growth of 1,605,632 and 2,195,456 bytes.
All those reports retain their original 250 MB failures. No causal percentage
claim is inferred from unlike runs or allocation-instrumented measurements.

## Decision

Set the macOS desktop release ceiling to 275,000,000 resident bytes, leaving
about 14.7 MB above the higher current measured peak. Keep 250 MB as the
optimization target. Keep the 5,000,000-byte warm-growth gate, idle CPU, cached
interaction and frame-work budgets unchanged. Report both memory ceilings in
each new benchmark result; never relabel historical failures as passes.

Other operating systems retain their existing 250 MB ceiling pending their own
profile review. This decision concerns RSS, not private heap, kernel physical
footprint, unbounded workloads or a universal device-memory guarantee. It
does not approve mobile installation or change request cancellation behavior.

## Alternatives and consequences

Keeping 250 MB as an immediate release gate would require another allocation or
renderer investigation. No further small, verified reduction has been identified
in this pass. Replacing the radar's rendering architecture remains a possible
optimization, but is not a safe local substitution without new topology,
interaction, animation and accessibility evidence. This is not a claim that no
future optimization is possible. A larger arbitrary ceiling is unnecessary.

Validate the amended ceiling in a fresh uninstrumented foreground run, retain
the raw protocol/environment evidence, and review the budget again if the
measured workload, display, renderer or supported target changes. Revert the
ceiling when repeatable evidence meets 250 MB. The 95% line and 90% branch
coverage gates and all functional/image release gates remain authoritative.

## What Did The Fresh Gate Run Establish?

`2026-10-10-macos-275mb-gate/measurements.jsonl` records an uninstrumented,
foreground Cocoa run against the existing 5,000-resource, three-session profile.
Its report explicitly records the new 275,000,000-byte RSS ceiling and unchanged
5,000,000-byte warm-growth ceiling. Peak RSS was 261,799,936 bytes; warm growth
was 1,376,256 bytes. Both passed. Idle CPU was 1.4882 percent of one core over
60,002 ms with no foreground loss. Filter/sort/tab/inspector p95 values were
40.28/29.46/45.08/24.72 ms and passed their unchanged limits. Installed startup,
per-frame UI work, other platforms and broader lifecycle gates are separate.
