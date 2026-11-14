# 0014 Separate UI Performance Budgets From Infrastructure Latency

## Status

Accepted on 2026-10-02. The interaction-specific frame budget was confirmed on 2026-11-02. Numerical enforcement was superseded by the user's 2026-11-06 guideline decision below. Current implementation conformance is tracked in the test map.

## Context

Cached desktop interactions should remain responsive even when Kubernetes, custom resources, or authentication are slow. Enforcing local UI targets as network deadlines would conceal rendering problems and interrupt legitimate infrastructure work.

## Decision

On 2026-11-06, treat our self-imposed package size, startup, response-time,
frame-CPU, RSS and idle-CPU figures as optimization guidelines rather than
universal hard release gates. Different operating systems and architectures
need their own recorded profiles. Keep measuring the existing targets and
report target misses explicitly, without silently labeling them met. A numeric
miss alone does not fail CI or prevent release. It is not necessary to enlarge
a number merely to turn an otherwise valid report green.

This does not waive correctness, lifecycle leaks, authentication/TLS safeguards,
coverage, missing image/device evidence, actual platform requirements or invalid
measurements. Sampling, frame-clock calibration, foreground validity, fixture
identity and bounded hang watchdogs remain mandatory. The 60-second idle window
still excludes active synchronization without hiding its observed requests.
User-configured log/cache/YAML caps are product safeguards, not benchmark budgets.
Historical failures retain their then-current verdict and raw evidence.

The following earlier numerical decisions retain their values as reference
targets; this amendment supersedes their hard-gate wording.

Use PER-001 through PER-013 and the measurement profile in the [operational specification](../spec/podlord-operational-spec.md#confirmed-performance-acceptance-requirements) as the owning performance contract. The accepted numeric budgets provide falsifiable targets for cached interaction, frame work, startup, idle CPU, process memory, and release size rather than relying on subjective impressions of speed.

The original 200 ms cached-inspector target was withdrawn on 2026-10-02 as too permissive and replaced by the confirmed tighter contract in question 13. Startup and RSS targets were tightened as well; frame-work and package-size budgets were added. The specification owns the numeric limits. Cached-interaction latency remains action-to-visible-content, not pure painting, and performance targets still do not authorize request cancellation. The requested C++ rewrite does not itself prove a performance improvement.

Measure at public UI boundaries and distinguish local work from external response latency. Keep already dispatched infrastructure calls independent from UI performance deadlines. Existing cache, scheduling, authentication, and write-safety contracts remain applicable.

On 2026-11-02, distinguish discrete filtering, sorting, cached-session tab switches
and cached-inspector opening from scrolling. The four named actions use a 20 ms
frame-CPU p95 gate; scrolling and other frame-work cases retain 8 ms. The same
limit applies to frame samples and per-action frame maxima. Native measurements
showed that the discrete actions met their separate response-time limits while
exceeding the earlier universal frame budget. This is an explicit release-budget
decision, not a claim that further optimization is impossible. Memory, idle CPU,
response-time and sampling requirements remain unchanged.

## Alternatives And Trade-offs

- A single end-to-end deadline is easy to state but conflates local rendering with infrastructure latency. Separate evidence identifies the owner of a delay.
- Cancelling slow requests to meet UI budgets can interrupt valid infrastructure work. Cached presentation and visible progress are preferred.
- Averages or isolated fast runs can hide repeated interaction delays. Repeated public-boundary measurements with the specified percentile provide more useful acceptance evidence.

## Consequences

Benchmark evidence must record the real environment and workload, not claim unexecuted targets as results. The review must retain resource-use measurements and delayed-response lifecycle checks. This decision does not introduce a transport timeout, fabricate a benchmark result, or claim current conformance.
