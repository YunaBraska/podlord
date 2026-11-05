#!/usr/bin/env python3
"""Generate the compact original notification cues using LAME for MP3 encoding."""

import math
import os
import shutil
import struct
import subprocess
import tempfile
import wave
from pathlib import Path


OUTPUT = Path(__file__).resolve().parents[1] / "assets" / "audio" / "modern"
RATE = 22050
MAX_TOTAL_BYTES = 12 * 1024

# Each cue is a sequence of (frequency, start, duration, amplitude) notes.
CUES = {
    "soft-confirm": [(660, 0.00, 0.17, 0.25), (880, 0.13, 0.24, 0.21)],
    "gentle-change": [(523, 0.00, 0.16, 0.22), (659, 0.13, 0.16, 0.20), (784, 0.26, 0.20, 0.17)],
    "attention": [(740, 0.00, 0.20, 0.20), (587, 0.17, 0.25, 0.18)],
    "complete": [(587, 0.00, 0.15, 0.21), (740, 0.12, 0.15, 0.19), (880, 0.24, 0.23, 0.16)],
    "soft-warning": [(622, 0.00, 0.19, 0.19), (554, 0.16, 0.22, 0.17), (494, 0.34, 0.25, 0.15)],
    "new-activity": [(784, 0.00, 0.12, 0.19), (988, 0.10, 0.17, 0.16)],
}


def write_wave(path: Path, notes: list[tuple[float, float, float, float]]) -> None:
    frames = int((max(start + duration for _, start, duration, _ in notes) + 0.09) * RATE)
    samples = []
    for index in range(frames):
        time = index / RATE
        value = 0.0
        for frequency, start, duration, amplitude in notes:
            local = time - start
            if 0 <= local <= duration:
                fade_in = min(1.0, local / 0.012)
                fade_out = min(1.0, (duration - local) / 0.055)
                envelope = max(0.0, min(fade_in, fade_out))
                phase = 2 * math.pi * frequency * local
                value += amplitude * envelope * (math.sin(phase) + 0.16 * math.sin(2 * phase))
        samples.append(struct.pack("<h", max(-32767, min(32767, round(value * 32767)))))
    with wave.open(str(path), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(RATE)
        audio.writeframes(b"".join(samples))


def main() -> None:
    lame = shutil.which("lame")
    if not lame:
        raise SystemExit("The LAME MP3 encoder is required to regenerate this pack")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        for name, notes in CUES.items():
            source = Path(temporary) / f"{name}.wav"
            write_wave(source, notes)
            target = OUTPUT / f"{name}.mp3"
            subprocess.run([
                lame, "--silent", "--noreplaygain", "--strictly-enforce-ISO",
                "--resample", "22.05", "-m", "m", "-b", "16",
                os.fspath(source), os.fspath(target),
            ], check=True)
    total = sum(path.stat().st_size for path in OUTPUT.glob("*.mp3"))
    if len(list(OUTPUT.glob("*.mp3"))) != len(CUES) or total > MAX_TOTAL_BYTES:
        raise SystemExit(f"Generated audio pack is invalid or exceeds 12 KiB: {total} bytes")
    print(f"Generated {len(CUES)} cues, {total} bytes total")


if __name__ == "__main__":
    main()
