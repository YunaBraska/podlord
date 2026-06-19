# Roadmap

Podlord is built around a flat, cache-first Kubernetes workspace. The current focus is making the desktop app reliable, readable, and safe before adding larger orchestration features.

## Deferred Audio Polish

- Record CC0 voice cues for `voice/radar-activated.ogg`, `voice/under-attack.ogg`, `voice/load-complete.ogg` (free TTS such as Piper, Coqui-TTS, macOS `say`).
- Source CC0 calm ambient or industrial loops for `music/calm/`. Energetic loops already shipped.

Empty roles play silent today and do not block the first OSS release.

## Workspace Views

### Network View

Add a Network workspace alongside the Graph view. Map Services, Endpoints, Ingresses, NetworkPolicies, Gateway/HTTPRoute, and pod-to-pod traffic into a topology that surfaces:

- Service → backing pods fan-out.
- Ingress/Gateway → Service → pods routing chains.
- Cross-namespace edges and NetworkPolicy restrictions.
- Live throughput when metrics sources expose it.

## Custom Automations

Promote the current built-in radar reactions (auto-zoom to next problem, deterministic colors, blink on alerts) into a user-configurable automation engine.

Surface: a rules editor where each rule binds a *trigger* to one or more *actions*.

### Triggers

- Resource status changes (Created, Updated, Deleted, Restart, CrashLoopBackOff, etc.).
- Metric thresholds crossed (CPU, memory, storage usage or limit-ratio).
- Event reasons or counts within a window.
- Filter membership (resource enters or leaves a saved filter).
- Custom field expressions (same DSL as saved filters).

### Actions

- Radar reactions:
  - color override
  - blink / pulse animation
  - zoom or pan radar to the resource
  - mark resource with custom glyph
- Inspector or table reactions:
  - highlight row
  - auto-focus the resource
- Sound reactions: play a chosen sample on trigger.
- Desktop notifications.
- Status line banner.
- Optional debounce, cooldown, and per-source scoping.

### Persistence

- Automations live in app settings; import/export as YAML/JSON shareable presets.
- A default automation pack ships with the same behaviors users see today.
- Reset-to-defaults at any time.

Automations build on top of the existing alert rule engine — they are the unified primitive for everything that reacts to cluster state.

## Sound Gamification

Make the cluster feel like a late-90s / early-2000s RTS using CC0 / royalty-free sounds and music. We mimic *style only* — no commercial game assets (Blizzard, EA/Westwood, etc.) ever ship in the repo.

Asset framework already in place at `src/Podlord.App/Assets/Audio/` with:

- Directory tree: `ui/`, `alerts/`, `events/`, `voice/`, `music/calm/`, `music/energetic/`.
- `CREDITS.md` table for every file (source URL, author, license).
- `MANIFEST.json` mapping semantic roles (`ui.click`, `alert.incident`, `event.radar_activated`, etc.) to one or more files. Multiple files per role get shuffled.
- `manifest.schema.json` describing the manifest format.
- `scripts/audio/README.md` sourcing guide.

### Planned Roles

| Role | Trigger |
| ---- | ------- |
| `ui.click` | Button / tab click |
| `ui.tab_switch` | Inspector tab switch |
| `ui.segment_ping` | Health bar segment changes state |
| `ui.hover` | Subtle hover beep on rare elements |
| `alert.incident` | New CRITICAL appears |
| `alert.warning` | New WARNING appears |
| `alert.recovery` | Critical → healthy |
| `alert.under_attack` | Many criticals at once |
| `event.startup` | App launch |
| `event.load_complete` | Initial resource load done |
| `event.radar_activated` | Screensaver → live radar |
| `event.session_switch` | Source switched |
| `voice.radar_activated` | Voice cue "Radar activated" |
| `voice.under_attack` | Voice cue "We are under attack" |
| `voice.load_complete` | Voice cue "Cluster online" |
| `music.calm` | Healthy cluster ambient loops, shuffled |
| `music.energetic` | Incident-active loops, shuffled |

### Default Catalog Sources

All listed in `Assets/Audio/CREDITS.md`. Confirmed-free catalogs:

- kenney.nl (CC0)
- freesound.org filtered to CC0
- pixabay.com (Pixabay license)
- opengameart.org filtered to CC0
- incompetech.com (CC BY, attribution required)
- fesliyanstudios.com (own free-commercial license)

Voice cues generated locally via Piper TTS (MIT) or espeak-ng (retro RTS HUD timbre). Output is not GPL-encumbered.

### Engine Work

- Avalonia audio playback layer (LibVLCSharp or OpenAL via Silk.NET).
- Manifest loader + runtime role dispatcher.
- Settings UI:
  - Per-category volume slider (UI / alert / voice / music).
  - Per-role enable/disable + sound picker.
  - Master mute.
  - Playlist preview.
- Music engine:
  - Cross-fade between calm and energetic when health state changes.
  - Shuffled playlist with no-immediate-repeat.
- Asset packs as ZIP bundles users can drop into a config directory; auto-merged into the manifest.
- In-app credits screen surfaces CC BY attributions.

### Forbidden Sources

Do NOT add files extracted from Blizzard, EA/Westwood, or any commercial game. Style only.

## Near-Term Reliability

- Continue reducing unnecessary UI redraws.
- Add more UI regression tests around table layout, inspector sizing, and radar selection.
- Add screenshot regression coverage when the test infrastructure can run it reliably.
- Improve source manager deduplication and deletion workflows.

## Kubernetes Engine

- Add watch-based resource updates for supported resource kinds.
- Keep list/poll fallback for clusters where watches are not available or are forbidden.
- Add better CRD discovery and generic CRD tables.
- Add more metrics sources beyond `metrics.k8s.io`:
  - Prometheus
  - kube-state-metrics
  - cAdvisor/kubelet where available

## Packaging

- Add signed and notarized macOS releases.
- Add Windows installer packaging.
- Add Linux `.deb`, `.rpm`, and AppImage when the portable archives are stable.
