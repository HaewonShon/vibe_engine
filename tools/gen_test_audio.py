"""
gen_test_audio.py
=================
Generates synthetic WAV test files for VibeEngine audio demos.
All output: 44100 Hz, mono, 16-bit PCM.

Run from the repo root:
    python tools/gen_test_audio.py

Output files (created automatically):
    Assets/Sounds/impact.wav   -- low thud   (cube hits floor)
    Assets/Sounds/bounce.wav   -- mid boing   (light bounce)
    Assets/Sounds/bgm.wav      -- simple loopable tone bed
"""

import struct
import math
import os
import random

SAMPLE_RATE = 44100
MAX_INT16   = 32767


# ─── WAV writer ────────────────────────────────────────────────────────────────

def write_wav(path: str, samples: list[float]) -> None:
    """Write a list of float samples (−1.0…1.0) as a 16-bit mono WAV."""
    pcm = b''.join(struct.pack('<h', max(-MAX_INT16, min(MAX_INT16, int(s * MAX_INT16))))
                   for s in samples)

    num_samples   = len(pcm) // 2
    byte_rate     = SAMPLE_RATE * 2        # 1 channel × 16-bit
    block_align   = 2
    data_size     = len(pcm)
    riff_size     = 36 + data_size

    header = struct.pack('<4sI4s'          # RIFF .... WAVE
                        '4sIHHIIHH'        # fmt  chunk
                        '4sI',             # data tag + size
        b'RIFF', riff_size, b'WAVE',
        b'fmt ', 16,
        1,                  # PCM
        1,                  # mono
        SAMPLE_RATE,
        byte_rate,
        block_align,
        16,                 # bits per sample
        b'data', data_size)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(pcm)

    duration = num_samples / SAMPLE_RATE
    print(f"  wrote: {path}  ({duration:.2f}s, {data_size} bytes)")


# ─── Synthesis helpers ──────────────────────────────────────────────────────────

def sine(freq: float, t: float) -> float:
    return math.sin(2.0 * math.pi * freq * t)

def exp_decay(t: float, half_life: float) -> float:
    """Amplitude envelope that halves every `half_life` seconds."""
    return math.exp(-math.log(2) * t / half_life)

def adsr(t: float, duration: float,
         attack: float, decay: float, sustain_level: float, release: float) -> float:
    """Simple ADSR envelope (returns amplitude 0‥1)."""
    if t < attack:
        return t / attack
    t -= attack
    decay_end = decay
    if t < decay_end:
        return 1.0 - (1.0 - sustain_level) * (t / decay_end)
    t -= decay_end
    sustain_end = duration - attack - decay - release
    if sustain_end < 0:
        sustain_end = 0
    if t < sustain_end:
        return sustain_level
    t -= sustain_end
    if t < release:
        return sustain_level * (1.0 - t / release)
    return 0.0


# ─── impact.wav ─────────────────────────────────────────────────────────────────
#
# Layered thud: low sine (80 Hz) + sub-bass click (40 Hz) + short noise burst.
# Fast exponential decay — sounds like a dense object hitting a hard floor.

def gen_impact(duration: float = 0.55) -> list[float]:
    n = int(SAMPLE_RATE * duration)
    samples = []
    random.seed(42)

    for i in range(n):
        t = i / SAMPLE_RATE

        # Sub-bass punch
        sub   = sine(40.0, t)  * exp_decay(t, 0.04)
        # Main body thud
        body  = sine(80.0, t)  * exp_decay(t, 0.10)
        # Upper harmonic presence
        upper = sine(160.0, t) * exp_decay(t, 0.06)
        # Short noise burst (simulates attack transient)
        noise = (random.random() * 2 - 1) * exp_decay(t, 0.015)

        s = 0.35 * sub + 0.40 * body + 0.15 * upper + 0.30 * noise
        samples.append(max(-1.0, min(1.0, s)))

    return samples


# ─── bounce.wav ─────────────────────────────────────────────────────────────────
#
# Mid-frequency "boing": a frequency-swept sine (600 → 200 Hz) with a
# percussive attack and smooth tail.  Sounds like a rubber or hollow
# object bouncing on a surface.

def gen_bounce(duration: float = 0.30) -> list[float]:
    n = int(SAMPLE_RATE * duration)
    samples = []

    f_start = 600.0
    f_end   = 200.0
    phase   = 0.0

    for i in range(n):
        t    = i / SAMPLE_RATE
        frac = t / duration

        # Exponential frequency sweep (high → low = natural decay)
        freq  = f_start * (f_end / f_start) ** frac
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        raw   = math.sin(phase)

        env = adsr(t, duration,
                   attack=0.002, decay=0.05, sustain_level=0.35, release=0.15)
        samples.append(max(-1.0, min(1.0, raw * env * 0.85)))

    return samples


# ─── bgm.wav ────────────────────────────────────────────────────────────────────
#
# Simple loopable ambient pad: three detuned sines in a minor chord (A minor)
# that cross-fade at the loop point so it sounds seamless.
# Root = 220 Hz (A3),  minor third = 261 Hz (≈C4),  fifth = 330 Hz (E4).

def gen_bgm(duration: float = 4.0) -> list[float]:
    n = int(SAMPLE_RATE * duration)
    samples = []

    freqs      = [220.0, 261.63, 329.63]     # Am chord
    detunes    = [0.0,  +0.3,   -0.2]        # slight detuning for warmth
    amplitudes = [0.30,  0.25,   0.25]

    fade = int(SAMPLE_RATE * 0.05)           # 50 ms cross-fade at edges

    for i in range(n):
        t   = i / SAMPLE_RATE
        s   = sum(amp * sine(freq + det, t)
                  for freq, det, amp in zip(freqs, detunes, amplitudes))

        # Fade in / fade out for seamless looping
        env = 1.0
        if i < fade:
            env = i / fade
        elif i > n - fade:
            env = (n - i) / fade

        samples.append(max(-1.0, min(1.0, s * env)))

    return samples


# ─── Entry point ────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    base = os.path.join(os.path.dirname(__file__), '..', 'Assets', 'Sounds')

    print("Generating test audio files...")
    write_wav(os.path.join(base, 'impact.wav'), gen_impact())
    write_wav(os.path.join(base, 'bounce.wav'), gen_bounce())
    write_wav(os.path.join(base, 'bgm.wav'),    gen_bgm())
    print("Done.")
