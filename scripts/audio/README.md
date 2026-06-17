# Audio Asset Sourcing

Audio files are NOT auto-downloaded by build. Each must be:
1. Manually sourced from a free-redistribution catalog (see `src/Podlord.App/Assets/Audio/CREDITS.md`).
2. License-verified at the file level.
3. Documented in `CREDITS.md`.
4. Registered in `MANIFEST.json`.

## Style Reference

Target aesthetic evokes late-90s / early-2000s RTS UIs: terse beeps, brassy alarm stings, tense percussion, calm industrial pads. We do NOT copy any commercial game's actual sound or music. We match the vibe with CC0 / royalty-free originals.

## Sourcing Workflow

```sh
# 1. Browse a CC0 catalog
open https://kenney.nl/assets/category:audio

# 2. Verify license on the asset page.

# 3. Download into a scratch dir.

# 4. Trim / fade with audacity or ffmpeg.
ffmpeg -i raw.wav -af "afade=t=in:ss=0:d=0.05,afade=t=out:st=0.45:d=0.05" -c:a libvorbis -q:a 4 ui/click.ogg

# 5. Add to CREDITS.md + MANIFEST.json.

# 6. Run app, audition under Settings > Audio.
```

## Free-Use Catalog Quick Reference

| Catalog | License | Attribution | Notes |
| ------- | ------- | ----------- | ----- |
| kenney.nl | CC0 | optional | Best for UI clicks, beeps, sci-fi interface |
| freesound.org (CC0 filter) | CC0 | optional | Massive; verify each file |
| freesound.org (CC BY filter) | CC BY | required | Verify author + URL |
| pixabay.com | Pixabay License | not required | Music + SFX, free commercial use |
| opengameart.org | varies | varies | Filter to CC0; check per file |
| incompetech.com | CC BY 4.0 | required | Kevin MacLeod, cinematic tracks |
| fesliyanstudios.com | own free-use | required, link only | Royalty-free SFX/music |
| freemusicarchive.org | varies | varies | Filter to CC0 or CC BY |

## Voice Cues

For phrases like "Radar activated" or "We are under attack":

- **Piper TTS** (MIT) — small offline models. Render WAV with a single command:
  ```sh
  echo "Radar activated" | piper --model en_US-libritts_r-medium --output_file voice/radar-activated.wav
  ffmpeg -i voice/radar-activated.wav -c:a libvorbis -q:a 5 voice/radar-activated.ogg
  rm voice/radar-activated.wav
  ```
- **espeak-ng** (GPL) — retro robotic timbre, fits RTS HUD voice samples:
  ```sh
  espeak-ng -v en+m3 -s 150 -w voice/radar-activated.wav "Radar activated"
  ```

Generated voice files are derivative of the engine; engine license terms apply to redistribution. Piper output: free to use (MIT). espeak-ng output: free to use (output is not GPL-encumbered).

## Forbidden Sources

Do NOT add any file sourced from:
- Blizzard Entertainment (Warcraft, StarCraft, Diablo, Overwatch, etc.)
- Electronic Arts / Westwood Studios (Command & Conquer, Red Alert, etc.)
- Any commercial game extraction or rip
- YouTube rips (most YouTube audio is not licensed for redistribution)
- "Free" sites that don't specify a license

When in doubt: drop the file.
