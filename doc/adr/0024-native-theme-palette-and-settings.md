# How are the existing themes applied to the native application?

Status: accepted for the native appearance implementation.

## Decision

Preserve all 19 named palettes from the C# theme catalog, their dark/light
variants, the subtle/medium/arcade choices, and the Sirocco Command/dark/subtle
defaults. The native runtime reads one compiled JSON catalog; the migration
tests compare every semantic color against the C# source catalog.

Use Qt's standard Fusion controls for the desktop application. Apply semantic
palette roles to the application, its window, and inherited controls, including
placeholder and disabled text. Tests can retain their explicit Basic style.
Do not build a second control toolkit to reproduce Avalonia internals.

Intensity changes a cached static texture and glow strength. It does not own an
animation timer, network request, sync loop, or independent settings store.
Identity colors use the existing theme-derived deterministic hash behavior.
Recolor table data without resetting the model.

Keep appearance in version 4 of the existing atomic, locked read-settings file.
Versions 1 through 3 retain their existing policy migration and receive the
appearance defaults. Validate canonical choices before saving. A failed or
conflicting save retains the active appearance and restores the visible choice.
Read-policy saves preserve appearance and appearance saves preserve policy.

## Consequences

The runtime does not need C#, a theme plugin, or a network connection to apply
a theme. The preserved C# catalog remains a migration test reference, not a
runtime dependency. Palette-only changes bypass resource scheduler
reconfiguration when the read-policy values did not change.

Older native builds that do not support settings version 4 must report the
unsupported document rather than silently overwrite it. Rollback must preserve
the profile; it must not discard newly stored appearance choices.

Matching palettes and functional selectors are not proof of complete visual
parity. Typography, layout, icons, populated inspector behavior, responsive
surfaces, localization, and the remaining settings capabilities still need their
own acceptance evidence. Static texture rendering is not pixel-identical to
the legacy renderer.

## Which control styles belong in the macOS package?

Ship Fusion and its Basic fallback, not every toolkit control style discovered
through the runtime-selected `QtQuick.Controls` import. This does not remove any
of the 19 product palettes, variants or intensities. The current QGuiApplication
does not use QWidget controls; their style plugin/framework are not required.
Unused Timeline plugins discovered by deployment are also omitted, rather than
shipping plugins whose dependent frameworks were absent.

The package checker verifies every retained Mach-O file and its bundled load
dependencies after subtraction. Actual-artifact tests cover valid packages,
paths containing spaces, a removed WebSocket framework and a corrupt executable.
The test map records measured size, identity and limitations. This is not
clean-machine startup, signed distribution, license clearance or mobile proof.

## Verification

`native.appearance.*` drives the actual inline Settings controls, compares all 19 palettes
across both variants and all three intensities, and checks real control palette
roles. Separate cases exercise restart, policy preservation, unchanged navigation,
lock contention, cross-window conflict, invalid choices, and unchanged choices.

The dark placeholder regression was reproduced before correcting the shared
palette role and window binding. Desktop screenshots also exercise production
Fusion controls, not only the offscreen Basic test style. The test map records
the evidence and its limitations.

## Which settings surface owns appearance editing?

The inline Appearance, Graphics, Sync and Privacy sections are the sole editors
for palettes, variants, intensity, water, reduced motion, read policy, retained
logs and YAML limits. Remove duplicate appearance and synchronization dialogs
and their routing rather than retaining hidden controls, compatibility
identifiers or a second presentation state. Existing persistence, validation,
failure feedback and atomic save ownership remain unchanged. Tests use actual
inline selectors and the water slider's keyboard interaction. This subtraction
does not resolve the separate native Cocoa accessibility/occlusion investigation.
