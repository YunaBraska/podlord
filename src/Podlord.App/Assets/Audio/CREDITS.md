# Audio Asset Credits

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
| `ui/click.ogg` | Tab + button click | _pending_ | _pending_ | _pending_ |
| `ui/tab-switch.ogg` | Inspector tab switch | _pending_ | _pending_ | _pending_ |
| `ui/segment-ping.ogg` | Health bar segment lit | _pending_ | _pending_ | _pending_ |
| `ui/hover.ogg` | Subtle hover beep | _pending_ | _pending_ | _pending_ |

## Alerts

| File | Role | Source | Author | License |
| ---- | ---- | ------ | ------ | ------- |
| `alerts/incident.ogg` | New CRITICAL appears | _pending_ | _pending_ | _pending_ |
| `alerts/warning.ogg` | New WARNING appears | _pending_ | _pending_ | _pending_ |
| `alerts/recovery.ogg` | Critical → healthy | _pending_ | _pending_ | _pending_ |
| `alerts/under-attack.ogg` | Many criticals at once | _pending_ | _pending_ | _pending_ |

## Events

| File | Role | Source | Author | License |
| ---- | ---- | ------ | ------ | ------- |
| `events/startup.ogg` | App launch | _pending_ | _pending_ | _pending_ |
| `events/load-complete.ogg` | Initial resource load done | _pending_ | _pending_ | _pending_ |
| `events/radar-activated.ogg` | Screensaver → live radar | _pending_ | _pending_ | _pending_ |
| `events/session-switch.ogg` | Source switched | _pending_ | _pending_ | _pending_ |

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
| `music/energetic/01.ogg` | Battle drums | _pending_ | _pending_ | _pending_ | _pending_ |
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
