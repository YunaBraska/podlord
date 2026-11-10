# Original audio

Podlord bundles 20 original short cues and 20 original instrumental pieces. Scores and the offline renderer live here. No samples, downloaded clips, vocals or copied commercial-game melodies are used.

`native/assets/audio/catalog.json` owns IDs, display metadata and legacy cue mappings. The C++ app uses only embedded assets. Rendering is a development step, never an installation prerequisite or runtime dependency. See `render.sh` for reproducible local commands.

The alert specification owns playback and migration behavior. Full silent decoding and a few muted device lifecycle tests replace repeated audible asset tests. Qt/FFmpeg and other runtime library notices remain required. The preserved C# reference owns its historical audio separately.
