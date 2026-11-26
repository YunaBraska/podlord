# 0035 - Native preflight gates before release

Status: accepted.

The product release is built from the C++ and Qt Quick source. Pull requests,
main branch qualification and stable releases use the same native candidate
workflow. A source change must not pass through an event-path gap.

Qualification collects platform builds, native behavior and UI checks, package
runtime checks and owned local Kubernetes evidence. Performance, coverage and
package size are measured and reported. A missing or incorrect package,
security boundary or required public journey fails qualification.

Merging to main creates qualified candidates without tags or releases. A stable
release selects one version, signs and notarizes direct macOS packages, prepares
Store packages and publishes only after required jobs succeed. Store upload is
an explicit workflow step. The release workflow in `.github/workflows/release.yml`
is the executable source of truth for these steps.
