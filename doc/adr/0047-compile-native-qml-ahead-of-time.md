# 0047: Compile native QML ahead of time

Status: Accepted

## Context

The private macOS release must pass the installed-app startup budget. Embedding QML with `qt_add_resources` leaves compilation to runtime and a per-profile disk cache, making first use less predictable.

## Decision

Use Qt's `qt_add_qml_module` for the existing QML files, with no separate plugin. Keep their `qrc:/podlord/<name>.qml` paths unchanged so application and test entrypoints continue to load the same UI. Keep images, themes, license, and notices as ordinary resources.

## Consequences

Build time and bundle size may rise. An installed-app cold-start measurement, resource-load UI tests, and package-size check are required before treating this as a release improvement. The release startup threshold is not relaxed by this decision.
