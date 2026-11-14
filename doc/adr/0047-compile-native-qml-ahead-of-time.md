# 0047: Compile native QML ahead of time

Status: Accepted

## Context

The private macOS release must pass the installed-app startup budget. Embedding QML with `qt_add_resources` leaves compilation to runtime and a per-profile disk cache, making first use less predictable.

## Decision

Use Qt's `qt_add_qml_module` for the existing QML files, with no separate plugin. Keep their `qrc:/podlord/<name>.qml` paths unchanged so application and test entrypoints continue to load the same UI. Keep images, themes, license, and notices as ordinary resources.

## Consequences

Build time and bundle size may rise. An installed-app cold-start measurement, resource-load UI tests, and package-size check are required before treating this as a release improvement. The release startup threshold is not relaxed by this decision.

## Measured alternative not adopted

On 2026-11-01, an experiment replaced the existing per-window context variable
with required typed Workspace properties, foreign type metadata and explicit
child injection. Across four sampled QML files, native code generation increased
from 208 to 241 bindings. The totals also include newly introduced injection
bindings and do not measure runtime work.

The controlled 5,000-resource, three-session headless comparison did not show a
CPU improvement. Four batches of 35 actions, including five warmups each, used
1,460 ms before and 1,560 ms after, measured through external process CPU
accounting. This is not a frame-CPU percentile or a foreground release result.
The final foreground attempt was visible but inactive and therefore invalid.
The user chose to discard the experiment. Keep the existing window ownership
and context boundary. Future optimization must follow measured runtime stacks,
not code-generation counts alone. The existing test map retains the evidence.
