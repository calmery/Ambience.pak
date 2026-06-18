#!/usr/bin/env python3
"""Generate ambient sound sources as seamless-looping OGG files."""
import array, math, os, random, subprocess, wave

SR = 44100
FADE = 4410  # ~100ms crossfade for seamless loop
OUT_DIR = "res/sounds"

def write_wav(path, samples_L, samples_R):
    with wave.open(path, "w") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        data = array.array("h")
        for l, r in zip(samples_L, samples_R):
            data.append(max(-32767, min(32767, int(l * 32767))))
            data.append(max(-32767, min(32767, int(r * 32767))))
        w.writeframes(data.tobytes())

def crossfade_loop(samples):
    n = len(samples)
    for i in range(FADE):
        t = i / FADE
        samples[i] = samples[i] * t + samples[n - FADE + i] * (1 - t)
    return samples[:n - FADE]

def to_ogg(wav_path, ogg_path):
    subprocess.run(
        ["ffmpeg", "-y", "-i", wav_path, "-c:a", "libvorbis", "-q:a", "4", ogg_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    os.remove(wav_path)

# --- noise generators (5s = minimal seamless loop) ---

NOISE_DUR = 5

def gen_white_noise():
    r = random.Random(42)
    L = crossfade_loop([r.gauss(0, 0.35) for _ in range(SR * NOISE_DUR)])
    r2 = random.Random(99)
    R = crossfade_loop([r2.gauss(0, 0.35) for _ in range(SR * NOISE_DUR)])
    return L, R

def gen_pink_noise():
    def voss(seed):
        rng = random.Random(seed)
        n = SR * NOISE_DUR
        rows = 16
        running = [0.0] * rows
        total = 0.0
        out = []
        for i in range(n):
            changed = i ^ (i - 1) if i > 0 else (1 << rows) - 1
            for r in range(rows):
                if changed & (1 << r):
                    old = running[r]
                    running[r] = rng.gauss(0, 1)
                    total += running[r] - old
            white = rng.gauss(0, 1)
            out.append((total + white) / (rows + 1) * 0.45)
        return out
    return crossfade_loop(voss(42)), crossfade_loop(voss(99))

def gen_brown_noise():
    def brown(seed):
        rng = random.Random(seed)
        n = SR * NOISE_DUR
        val = 0.0
        out = []
        for _ in range(n):
            val += rng.gauss(0, 0.02)
            val *= 0.998
            out.append(val)
        mx = max(abs(v) for v in out) or 1
        return [v / mx * 0.5 for v in out]
    return crossfade_loop(brown(42)), crossfade_loop(brown(99))

# --- tonal generators (10s for LFO variation) ---

TONE_DUR = 10

def gen_binaural():
    n = SR * TONE_DUR
    L = [math.sin(2 * math.pi * 200.0 * i / SR) * 0.3 for i in range(n)]
    R = [math.sin(2 * math.pi * 210.0 * i / SR) * 0.3 for i in range(n)]
    return crossfade_loop(L), crossfade_loop(R)

def gen_drone():
    n = SR * TONE_DUR
    freqs = [55.0, 55.3, 82.4, 82.7, 110.0, 110.5]
    L, R = [], []
    for i in range(n):
        t = i / SR
        lfo = 0.7 + 0.3 * math.sin(2 * math.pi * 0.05 * t)
        sL = sum(math.sin(2 * math.pi * f * t) for f in freqs) / len(freqs) * 0.35 * lfo
        sR = sum(math.sin(2 * math.pi * (f + 0.2) * t) for f in freqs) / len(freqs) * 0.35 * lfo
        L.append(sL)
        R.append(sR)
    return crossfade_loop(L), crossfade_loop(R)

# --- main ---

sounds = [
    ("White Noise", gen_white_noise),
    ("Pink Noise", gen_pink_noise),
    ("Brown Noise", gen_brown_noise),
    ("Binaural Beat", gen_binaural),
    ("Drone", gen_drone),
]

for name, gen in sounds:
    print(f"generating {name}...")
    L, R = gen()
    dur = len(L) / SR
    wav = os.path.join(OUT_DIR, f"{name}.wav")
    ogg = os.path.join(OUT_DIR, f"{name}.ogg")
    write_wav(wav, L, R)
    to_ogg(wav, ogg)
    sz = os.path.getsize(ogg)
    print(f"  -> {ogg} ({sz // 1024}KB, {dur:.1f}s)")

print("done")
