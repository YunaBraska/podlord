# Audio Asset Credits

Roles marked `_pending_` map to empty arrays in `MANIFEST.json`, so the app plays them silent. They are optional enhancements, not release blockers; new assets can land row-by-row without code changes.

Every audio file shipped under `Assets/Audio/` must be licensed for free redistribution and modification. Acceptable licenses:

- CC0 / public domain
- Creative Commons CC BY (attribution required; capture below)
- Pixabay Content License (free, no attribution required but recorded for transparency)
- Author-granted permission for redistribution

**Forbidden:** any commercial game assets (Blizzard, EA/Westwood, Bungie, Valve, etc.). We mimic *style*, not files.

When adding a file, fill in a row in the table and update `MANIFEST.json`.

## UI

| File | Role | Source | Author | License |
| ---- | ---- | ------ | ------ | ------- |
| `ui/panel-segment-load.ogg` | Health bar / panel segment lit | https://kenney.nl/assets/ui-audio (`switch1.ogg`) | Kenney | CC0-1.0 |
| `ui/metal-click.ogg` | Mechanical UI click | https://kenney.nl/assets/rpg-audio (`metalClick.ogg`) | Kenney | CC0-1.0 |
| `interface/click_001.ogg`, `click_003.ogg` | UI click variants (shuffled with `ui/metal-click.ogg`) | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |
| `interface/switch_001.ogg`, `switch_004.ogg` | Inspector tab switch | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |
| `interface/tick_001.ogg`, `tick_004.ogg` | Subtle hover beep | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |

## Interface Pack

| Path | Role | Source | Author | License |
| ---- | ---- | ------ | ------ | ------- |
| `interface/*.ogg` | 100 searchable UI and alert candidate sounds | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |

## Alerts

| File | Role | Source | Author | License |
| ---- | ---- | ------ | ------ | ------- |
| `alerts/critical-klaxon.ogg` | New CRITICAL appears | https://kenney.nl/assets/sci-fi-sounds (`lowFrequency_explosion_001.ogg`) | Kenney | CC0-1.0 |
| `alerts/warning-ping.ogg` | New WARNING appears | https://kenney.nl/assets/interface-sounds (`error_002.ogg`) | Kenney | CC0-1.0 |
| `alerts/electro-warning.ogg` | Electronic warning chirp | https://kenney.nl/assets/digital-audio (`zapThreeToneUp.ogg`) | Kenney | CC0-1.0 |
| `alerts/bell-alert.ogg` | Metallic command bell | https://kenney.nl/assets/impact-sounds (`impactBell_heavy_002.ogg`) | Kenney | CC0-1.0 |
| `alerts/metal-impact.ogg` | Heavy incident impact | https://kenney.nl/assets/impact-sounds (`impactMetal_medium_004.ogg`) | Kenney | CC0-1.0 |
| `interface/confirmation_001.ogg`, `confirmation_004.ogg` | Critical → healthy (alert recovery) | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |
| `interface/error_006.ogg`, `error_008.ogg` | Many criticals at once (under attack) | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |

## Events

| File | Role | Source | Author | License |
| ---- | ---- | ------ | ------ | ------- |
| `interface/open_001.ogg`, `interface/bong_001.ogg` | App launch | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |
| `interface/confirmation_002.ogg` | Initial resource load done (paired with `events/power-up.ogg`) | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |
| `events/radar-activated.ogg` | Screensaver → live radar | https://kenney.nl/assets/interface-sounds (`open_001.ogg`) | Kenney | CC0-1.0 |
| `events/activity-tick.ogg` | New non-critical activity | https://kenney.nl/assets/interface-sounds (`tick_001.ogg`) | Kenney | CC0-1.0 |
| `events/power-up.ogg` | Positive activation | https://kenney.nl/assets/digital-audio (`powerUp8.ogg`) | Kenney | CC0-1.0 |
| `events/power-down.ogg` | Descending deactivation | https://kenney.nl/assets/digital-audio (`lowDown.ogg`) | Kenney | CC0-1.0 |
| `events/three-tone.ogg` | Neutral system notification | https://kenney.nl/assets/digital-audio (`threeTone1.ogg`) | Kenney | CC0-1.0 |
| `interface/switch_002.ogg` | Source switched (paired with `events/three-tone.ogg`) | https://kenney.nl/assets/interface-sounds | Kenney | CC0-1.0 |

## Fantasy

| File | Role | Source | Author | License |
| ---- | ---- | ------ | ------ | ------- |
| `fantasy/book-open.ogg` | War-room page cue | https://kenney.nl/assets/rpg-audio (`bookOpen.ogg`) | Kenney | CC0-1.0 |

## Voice

Short spoken cues. Use a CC0 / royalty-free TTS engine (e.g. Piper, Coqui-TTS) and record locally. Captured here for traceability.

| File | Phrase | Engine / Voice | Notes |
| ---- | ------ | -------------- | ----- |
| `voice/radar-activated.ogg` | "Radar activated" | _pending_ | _pending_ |
| `voice/under-attack.ogg` | "We are under attack" | _pending_ | _pending_ |
| `voice/load-complete.ogg` | "Cluster online" | _pending_ | _pending_ |

## Music — Calm

Background loops for healthy clusters. Aim for low-energy ambient / industrial / orchestral pads.

| File | Mood | Duration | Source | Author | License |
| ---- | ---- | -------- | ------ | ------ | ------- |
| `music/calm/01.ogg` | Ambient pad | _pending_ | _pending_ | _pending_ | _pending_ |
| `music/calm/02.ogg` | Slow industrial | _pending_ | _pending_ | _pending_ | _pending_ |
| `music/calm/03.ogg` | Drone / sci-fi | _pending_ | _pending_ | _pending_ | _pending_ |

## Music — Energetic

Background loops when incidents present. Aim for tense percussion / cinematic / electronic.

| File | Mood | Duration | Source | Author | License |
| ---- | ---- | -------- | ------ | ------ | ------- |
| `music/energetic/command-jingle.ogg` | Short command jingle | <1s | https://kenney.nl/assets/music-jingles (`jingles_HIT00.ogg`) | Kenney | CC0-1.0 |
| `music/energetic/steel-command.ogg` | Metallic command jingle | <1s | https://kenney.nl/assets/music-jingles (`jingles_STEEL03.ogg`) | Kenney | CC0-1.0 |
| `music/energetic/bit-command.ogg` | 8-bit command jingle | <1s | https://kenney.nl/assets/music-jingles (`jingles_NES04.ogg`) | Kenney | CC0-1.0 |
| `music/energetic/02.ogg` | Industrial percussion | _pending_ | _pending_ | _pending_ | _pending_ |
| `music/energetic/03.ogg` | Tense cinematic | _pending_ | _pending_ | _pending_ | _pending_ |

## Suggested Source Catalogs

All require user verification of each individual file's license before adoption.

- **freesound.org** — filter `license:"Creative Commons 0"` for CC0. Good for UI clicks, alarms, radar beeps, sci-fi pads.
- **kenney.nl/assets** — entirely CC0. "UI Audio", "Sci-fi Sounds", "Interface Sounds" packs cover most UI/alert needs.
- **pixabay.com/music** + **pixabay.com/sound-effects** — Pixabay license, free for commercial use, no attribution required.
- **opengameart.org** — filter by `CC0` license. Has cinematic / fantasy / RTS-style loops.
- **incompetech.com** — Kevin MacLeod, CC BY. Many epic / cinematic / dark tracks. Attribution required.
- **freemusicarchive.org** — filter for CC0 or CC BY. Mixed catalog.
- **fesliyanstudios.com** — own royalty-free license, free for commercial use with attribution to fesliyanstudios.com.

## Voice Synthesis (Local, Free)

- **Piper TTS** (https://github.com/rhasspy/piper) — MIT-licensed, offline TTS, English voices included. Render WAV → convert to OGG with `ffmpeg`.
- **Coqui TTS** (https://github.com/coqui-ai/TTS) — MPL-2.0, larger model, more voice options.
- **espeak-ng** — GPL, retro robotic voice — fits the RTS aesthetic naturally.

## How to Add a File

1. Verify the source license. Reject anything that isn't CC0, CC BY, Pixabay, or an explicit free-redistribution permission.
2. Drop the file under the right directory (`ui/`, `alerts/`, `events/`, `voice/`, `music/calm`, `music/energetic`).
3. Convert to OGG Vorbis at ~96-128 kbps if not already (`ffmpeg -i input.wav -c:a libvorbis -q:a 4 output.ogg`).
4. Update the matching table in this file with source URL, author, license, and any required attribution string.
5. Update `MANIFEST.json` so the runtime can map roles → files.
6. If the license is CC BY or another attribution-required license, the in-app Credits screen must surface it (see `Settings > About`).
