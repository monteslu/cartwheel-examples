#!/usr/bin/env python3
"""Render an original, deliberately bombastic brass-band march for Liberty Leap."""
from pathlib import Path
import subprocess
import tempfile
import wave

import numpy as np

RATE = 32000
BPM = 126
BEAT = 60 / BPM
BARS = 16
LENGTH = BARS * 4 * BEAT
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/audio/music_stunt.ogg"
rng = np.random.default_rng(0x57A25)
left = np.zeros(round(LENGTH * RATE), dtype=np.float64)
right = np.zeros_like(left)


def note(midi, start, beats, level, kind="brass", pan=0):
    duration = beats * BEAT
    count = min(round(duration * RATE), len(left) - round(start * RATE))
    if count <= 0:
        return
    t = np.arange(count) / RATE
    hz = 440 * 2 ** ((midi - 69) / 12)
    phase = 2 * np.pi * hz * t + 0.016 * np.sin(2 * np.pi * 5.4 * t)
    if kind == "brass":
        tone = np.sin(phase) + .42 * np.sin(2 * phase) + .23 * np.sin(3 * phase) + .1 * np.sin(4 * phase)
        env = np.minimum(1, t * 48) * np.minimum(1, (duration - t) * 20)
    elif kind == "tuba":
        tone = np.sin(phase) + .37 * np.sin(2 * phase) + .17 * np.sin(3 * phase)
        env = np.minimum(1, t * 65) * np.exp(-t * 2.1) * np.minimum(1, (duration - t) * 25)
    else:
        tone = np.sin(phase) + .15 * np.sin(2 * phase)
        env = np.minimum(1, t * 8) * np.minimum(1, (duration - t) * 8)
    s = tone * env * level
    at = round(start * RATE)
    left[at:at + count] += s * (1 - .25 * pan)
    right[at:at + count] += s * (1 + .25 * pan)


def drum(start, kind, level):
    duration = .18 if kind == "snare" else .28
    count = min(round(duration * RATE), len(left) - round(start * RATE))
    if count <= 0:
        return
    t = np.arange(count) / RATE
    if kind == "kick":
        s = np.sin(2 * np.pi * (72 - 20 * t) * t) * np.exp(-t * 24)
    elif kind == "snare":
        noise = rng.normal(size=count)
        s = (.55 * noise + .25 * np.sin(2 * np.pi * 185 * t)) * np.exp(-t * 27)
    else:
        s = rng.normal(size=count) * np.exp(-t * 65) * .3
    at = round(start * RATE)
    left[at:at + count] += s * level
    right[at:at + count] += s * level


# F major, Bb, G minor, C7. This fanfare melody was written for this level.
chords = [
    (53, 57, 60), (58, 62, 65), (55, 58, 62), (60, 64, 67),
    (53, 57, 60), (58, 62, 65), (55, 58, 62), (60, 64, 67),
    (58, 62, 65), (53, 57, 60), (55, 58, 62), (60, 64, 67),
    (53, 57, 60), (58, 62, 65), (60, 64, 67), (53, 57, 60),
]
melody = [
    [(77, 0, 1), (81, 1, .5), (84, 1.5, .5), (89, 2, 1), (84, 3, 1)],
    [(86, 0, 1), (82, 1, .5), (79, 1.5, .5), (82, 2, 2)],
    [(79, 0, .5), (82, .5, .5), (86, 1, 1), (84, 2, 1), (82, 3, 1)],
    [(84, 0, 1), (88, 1, 1), (91, 2, .5), (88, 2.5, .5), (84, 3, 1)],
    [(89, 0, 1), (84, 1, .5), (81, 1.5, .5), (77, 2, 1), (84, 3, 1)],
    [(86, 0, 1), (89, 1, 1), (86, 2, .5), (82, 2.5, .5), (79, 3, 1)],
    [(82, 0, .5), (86, .5, .5), (89, 1, 1), (86, 2, 1), (82, 3, 1)],
    [(84, 0, 1), (79, 1, 1), (76, 2, 1), (72, 3, 1)],
]
for bar in range(BARS):
    base = bar * 4 * BEAT
    chord = chords[bar]
    for pitch in chord:
        note(pitch + 12, base, 3.9, .035, "pad", (pitch % 3) - 1)
    for beat in range(4):
        note(chord[0] - 12 if beat % 2 == 0 else chord[0] - 5, base + beat * BEAT, .85, .15, "tuba")
        drum(base + beat * BEAT, "kick" if beat % 2 == 0 else "snare", .18)
        drum(base + (beat + .5) * BEAT, "hat", .12)
    for pitch, offset, duration in melody[bar % 8]:
        transposition = 0 if bar < 8 else (-2 if bar in (8, 10) else 0)
        note(pitch + transposition, base + offset * BEAT, duration * .94, .11, pan=-.35)
        if bar >= 8:
            note(pitch + transposition - 12, base + offset * BEAT, duration * .94, .042, pan=.55)

peak = max(np.max(np.abs(left)), np.max(np.abs(right)))
left = np.tanh(left * (.84 / peak))
right = np.tanh(right * (.84 / peak))
fade = round(.045 * RATE)
edge = np.linspace(0, 1, fade)
for channel in (left, right):
    channel[:fade] *= edge
    channel[-fade:] *= edge[::-1]
pcm = np.column_stack((left, right))
pcm = (np.clip(pcm, -1, 1) * 32767).astype("<i2")
with tempfile.TemporaryDirectory() as temp:
    wav_path = Path(temp) / "music_stunt.wav"
    with wave.open(str(wav_path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(pcm.tobytes())
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav_path),
                    "-c:a", "libvorbis", "-q:a", "4", str(OUT)], check=True)
print(OUT)
