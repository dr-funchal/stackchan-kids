#!/usr/bin/env python3
"""Synthesize the Papa-Letras sounds (16 kHz mono WAV) and encode them as Ogg Opus (needs opusenc).
Output: crunch.ogg (the panda eats a letter), fanfare.ogg (end of the game)."""
import math, random, struct, subprocess, wave

RATE = 16000

def write_wav(name, samples):
    with wave.open(name, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 30000)) for s in samples))

def crunch():
    # Three bites: short bursts of filtered noise with a fast decay
    random.seed(7)
    out, prev = [], 0.0
    for bite in range(3):
        n = int(0.09 * RATE)
        for i in range(n):
            noise = random.uniform(-1, 1)
            prev = 0.55 * prev + 0.45 * noise  # Soft low-pass: "crunchy", not hissy
            out.append(prev * math.exp(-i / (0.025 * RATE)) * 0.9)
        out += [0.0] * int(0.07 * RATE)
    return out

def fanfare():
    # C-E-G-C arpeggio with a final chord, bell-like tones
    notes = [(523.25, 0.14), (659.25, 0.14), (783.99, 0.14), (1046.5, 0.5)]
    out = []
    for f, d in notes:
        n = int(d * RATE)
        for i in range(n):
            t = i / RATE
            env = min(1, i / 160) * math.exp(-t * (6 if d < 0.3 else 3))
            out.append(env * (0.6 * math.sin(2 * math.pi * f * t) + 0.25 * math.sin(4 * math.pi * f * t)))
    return out

for name, gen in (("crunch", crunch), ("fanfare", fanfare)):
    write_wav(name + ".wav", gen())
    subprocess.run(["opusenc", "--quiet", "--framesize", "60", "--bitrate", "24", name + ".wav", name + ".ogg"],
                   check=True)
    print(name)
