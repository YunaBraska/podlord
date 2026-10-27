# 0049: Separate first and repeat startup budgets

Status: Accepted

## Context

The installed macOS bundle reached interactive Settings in 1.31 seconds on its first measured launch and 0.59-0.62 seconds on subsequent launches. The single 1.0-second maximum conflated first-use platform loading with repeat startup. The first-launch target was explicitly revised without relaxing the repeat target.

## Decision

Measure ten launches of the installed bundle with isolated profiles. The first launch must make Settings interactive within 1.5 seconds; each of the nine subsequent launches must do so within 1.0 second. Keep the existing accessibility interaction as the public boundary. No other release gate changes.

## Consequences

The first-launch budget increases by 0.5 seconds. A passing local run is not a clean-device or cross-device proof; packaging, performance, coverage, and visual/device evidence remain independent release gates. The benchmark must terminate every launched test process and remove its isolated profile after each sample.
