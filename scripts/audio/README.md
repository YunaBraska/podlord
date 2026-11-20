# Original audio

Podlord bundles 20 original short cues and 20 original instrumental pieces. Scores and the offline renderer live here. No samples, downloaded clips, vocals or copied commercial-game melodies are used.

`native/assets/audio/catalog.json` owns IDs, display metadata and legacy cue mappings. The C++ app uses only embedded assets. Rendering is a development step, never an installation prerequisite or runtime dependency. See `render.sh` for reproducible local commands.

The alert specification owns playback and migration behavior. Full silent decoding and a few muted device lifecycle tests replace repeated audible asset tests. Qt/FFmpeg and other runtime library notices remain required.

## Music auditions

The two standalone pieces use the existing synthesizer, with separate themes, bass lines, drum grooves and arrangements. They do not change the bundled playlist. From the repository root on macOS:

```sh
clang++ -std=c++20 -O2 -Wall -Wextra -Wpedantic scripts/audio/compose.cpp -o /tmp/podlord-compose
/tmp/podlord-compose prototype native/assets/audio forge /tmp/podlord-forge.wav
/tmp/podlord-compose prototype native/assets/audio switchyard /tmp/podlord-switchyard.wav
afconvert -f m4af -d aac -b 192000 -q 127 /tmp/podlord-forge.wav /tmp/podlord-forge.m4a
afconvert -f m4af -d aac -b 192000 -q 127 /tmp/podlord-switchyard.wav /tmp/podlord-switchyard.m4a
```
