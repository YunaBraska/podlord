# Why Is Request Cadence Measured at Local Scheduler Admission?

Status: Accepted
Date: 2026-10-04

## Context

LOG-009 constrains automatic refresh-cycle starts, not transport arrival times.
The existing native log test compared timestamps taken when its external HTTP
boundary processed requests on the UI event loop. Concurrent Radar work reproduced
an apparent 2,988 ms interval against a 2,990 ms tolerance, despite the precise
local timer and 3,000 ms monotonic scheduling guard. Consumer-side scheduling and
transport delay make those timestamps a different measurement boundary.

## Decision

The existing request scheduler exposes a `requestStarted` event through the
workspace. Its timestamp is the scheduler's own monotonic admission time, already
used by request spacing and log-cycle scheduling. It is emitted after the request
and completion callbacks have been established; diagnostic observers do not run
while connection/request setup is incomplete.

The event contains the originating session ID, canonical API path and monotonic
milliseconds only. It does not expose authorization, query parameters, bodies,
responses or raw credentials. No new diagnostic persistence, cache, timer, retry,
request or UI fetch is introduced. Observers must remain bounded and nonblocking.
The timestamp is not a promise of remote arrival, successful delivery or response.

The public log test still drives the real UI, verifies actual HTTP requests and
retained log contents, and now requires at least 3,000 ms between local cycle
admissions. Remote ingress times remain failure diagnostics. This makes the
assertion stricter at its correct boundary rather than loosening the rate limit.

## Rejected Alternatives

- Increasing transport-arrival tolerance: hides a measurement mismatch behind an
  arbitrary margin and does not prove the local scheduling contract.
- Serializing every UI test: conceals the concurrent condition without explaining
  the result and provides no reliable transport-arrival clock.
- Exposing private scheduler state to tests or adding a second clock/cadence owner:
  duplicates authority; the existing monotonic scheduler supplies the observation.
- Treating the event as a complete request-audit UI: queued/completed/failure
  diagnostics and retained Settings capabilities still need their own evidence.

## Verification And Consequences

The original full-suite failure and repeated-load reproduction are retained in
`radar-lifecycle-complete-native.txt` and `radar-cadence-stress.txt`. With the
unchanged scheduling algorithm and corrected public observation, ten log-cadence
runs and ten concurrent Radar navigation runs passed in
`radar-local-cadence-stress.txt`. The current complete-suite outcome belongs to the
existing test map. Coverage and release gates are unchanged.
