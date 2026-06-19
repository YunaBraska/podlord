# Alert Automation

Podlord alerts are user-owned rules that reuse the same matcher language already used by resource filters. They run against the local resource cache and drive visual or audio feedback without adding direct Kubernetes calls.

## Model

An alert is built from a guided top-to-bottom inspector flow:

| Section | Purpose |
|---|---|
| Name | Human-readable alert name. |
| Description | Short operator note explaining why the rule exists. |
| Matchers | One or more matcher blocks. Criteria inside a block are `AND`; blocks are `OR`. |
| Color | Optional radar block color with `none`, `no-match`, or duration hold behavior. |
| Animation | Optional radar animation with `none`, `no-match`, or duration hold behavior. |
| Zoom | Optional radar zoom percentage when the rule fires. |
| Sound | Select a bundled or imported sound with visible author, license, and source link. |

Default alerts are built in, enabled by default, and locked against editing. Users can enable, disable, duplicate, or add custom alerts. Duplicating a built-in alert creates an editable custom copy.

## Matcher Language

Alerts use the existing Podlord matcher behavior:

- strings: `pod`, `"exact"`, `~prefix`, `suffix~`, `/regex/`
- numbers: `5`, `=5`, `>5`, `<5`, `>=5`, `<=5`
- durations: `>5m`, `<1h`, `<=10s`
- stats: `outlier`, `p95`
- scopes: kind, namespace, name, status, issue, node, image, owner, CPU, memory, storage, restarts, age, event reason, event message, activity, problems

Rules evaluate cached rows only. The UI updates immediately when a rule changes; Kubernetes sync cadence remains controlled by the existing cache, request queue, TTL, and request-limit settings.

## Default Alerts

| Alert | Trigger | Action |
|---|---|---|
| Pod restart spike | `kind="Pod"` and `restarts=outlier` | radar zoom, blink, amber color, warning sound |
| Problem resource | current problem matcher | radar zoom, blink, red color, critical sound |
| Recent activity | current activity matcher | pulse and green color for a short duration |
| CPU outlier | `cpu=p95` | radar focus, pulse, amber color |

## Sound Policy

Bundled sounds must be original, generated, or clearly royalty-free. Every sound choice must show:

- display name
- purpose
- author
- license
- source URL

No copyrighted game samples, faction voices, melodies, logos, or copied assets are allowed. Imported user sounds should keep their metadata beside the imported asset.

The built-in pack currently uses Kenney CC0 assets from UI Audio, Interface Sounds, Sci-fi Sounds, and Music Jingles. The app stores local OGG copies and keeps source, author, and license visible in the alert editor.

## Planned Expansion

- Add in-app sound playback and volume per alert.
- Add reusable alert presets for teaching, production, and noisy dev clusters.
- Move current hard-coded activity behavior fully into user-visible default rules.
- Add radar animation variants per rule: pulse, blink, sweep, outline, and trail.
- Add per-rule quiet hours and action throttling.
