#!/usr/bin/env python3
"""Convert Assets/Audio/story_mode.mp3 into the Story Mode soundtrack blob.

Story Mode's own track (the renamed Frozen Jam) is the ONLY asset that ships
as an MP3, so it gets its own converter instead of riding in
``generate_gba_data.py``:

* the six arcade WAVs decode with Python's stdlib ``wave`` module, so
  ``npm run generate-assets`` needs no third-party packages and CI stays
  dependency-free;
* an MP3 needs a real decoder, so this script depends on ``miniaudio`` and is
  run by hand whenever the source track changes.

The output, ``android/app/src/main/cpp/audio_story_data.c``, is committed.
Only the Android CMake target compiles it, so the GBA ROM (which has no Story
Mode) does not carry the samples.

    pip install miniaudio
    python3 tools/generate_story_audio.py
"""

import math
import os
import random
import sys

SRC = os.path.join('Assets', 'Audio', 'story_mode.mp3')
OUT_C = os.path.join('android', 'app', 'src', 'main', 'cpp', 'audio_story_data.c')
OUT_H = os.path.join('android', 'app', 'src', 'main', 'cpp', 'audio_story_data.h')

TARGET_RATE = 18157      # the DirectSound rate the whole mixer runs at
PEAK_TARGET = 0.82       # a hair under the arcade tracks: it plays under text

# Deterministic dither, exactly like generate_gba_data.py, so re-running the
# script on the same MP3 reproduces the same bytes.
_rng = random.Random(0x5EED)


def _build_filter_table(phases, taps, fc):
    """Windowed-sinc low-pass polyphase filter (same design as the WAV path)."""
    half = taps // 2
    table = []
    for p in range(phases):
        frac = p / phases
        row = []
        for k in range(taps):
            x = (k - half) - frac
            if x == 0.0:
                s = 2.0 * fc
            else:
                s = math.sin(2.0 * math.pi * fc * x) / (math.pi * x)
            w = 0.42 + 0.5 * math.cos(math.pi * x / half) + 0.08 * math.cos(2.0 * math.pi * x / half)
            row.append(s * w)
        ssum = sum(row)
        table.append([v / ssum for v in row])
    return table


def decode_mp3(path):
    """Decode to a mono float list plus its sample rate."""
    try:
        import miniaudio
    except ImportError:
        sys.exit(
            "miniaudio is required to decode the MP3 soundtrack.\n"
            "    pip install miniaudio\n"
            "The generated audio_story_data.c is committed, so this is only\n"
            "needed when Assets/Audio/story_mode.mp3 itself changes."
        )
    decoded = miniaudio.decode_file(path)
    nch = decoded.nchannels
    raw = decoded.samples
    if nch == 1:
        samples = [s / 32768.0 for s in raw]
    else:
        samples = [sum(raw[i:i + nch]) / (nch * 32768.0) for i in range(0, len(raw), nch)]
    return samples, decoded.sample_rate


def resample(samples, rate, target_rate):
    """Identical filter design to generate_gba_data.py's resample_wav(), but
    evaluated with numpy: this track is minutes long, and the pure-Python
    inner loop would take the better part of an hour."""
    try:
        import numpy as np
    except ImportError:
        sys.exit(
            "numpy is required to resample the soundtrack.\n"
            "    pip install numpy miniaudio"
        )

    x = np.asarray(samples, dtype=np.float64)
    x -= x.mean()

    ratio = target_rate / rate
    fc = 0.45 * min(1.0, ratio)
    taps = 64
    half = taps // 2
    phases = 4096
    table = np.asarray(_build_filter_table(phases, taps, fc))     # (phases, taps)

    padded = np.concatenate([x[half:0:-1], x, x[-2:-half - 2:-1]])

    out_len = int(len(x) * ratio)
    src_pos = np.arange(out_len, dtype=np.float64) * rate / target_rate
    idx = src_pos.astype(np.int64)
    phase = ((src_pos - idx) * phases + 0.5).astype(np.int64) % phases

    out = np.zeros(out_len, dtype=np.float64)
    # Accumulate one tap at a time: 64 vector passes instead of out_len loops.
    for k in range(taps):
        out += table[phase, k] * padded[idx + k]
    return out.tolist()


def quantize_8bit(samples, gain):
    """Scale to 8-bit signed with triangular dither + 1st-order noise shaping."""
    out = bytearray()
    error = 0.0
    for s in samples:
        v = s * 127.0 * gain + error
        dither = _rng.uniform(-0.5, 0.5) + _rng.uniform(-0.5, 0.5)
        q = int(math.floor(v + dither + 0.5))
        if q > 127:
            q = 127
        elif q < -128:
            q = -128
        error = v - q
        if error > 2.0:
            error = 2.0
        elif error < -2.0:
            error = -2.0
        out.append(q & 0xFF)
    return out


def loop_seam(samples, fade):
    """Cross-fade the tail into the head so the endless loop has no click."""
    n = len(samples)
    if fade * 2 >= n:
        return samples
    out = list(samples)
    for i in range(fade):
        t = i / fade
        # Fade the head in over the tail that is about to wrap onto it.
        out[i] = samples[i] * t + samples[n - fade + i] * (1.0 - t)
    return out[:n - fade]


def main():
    if not os.path.exists(SRC):
        sys.exit(f"missing {SRC}")

    print(f"decoding {SRC} ...")
    samples, rate = decode_mp3(SRC)
    print(f"  {len(samples)} frames @ {rate} Hz ({len(samples) / rate:.1f}s)")

    print(f"resampling to {TARGET_RATE} Hz ...")
    out = resample(samples, rate, TARGET_RATE)

    # ~0.35s seam so the track can loop under the intro and the level map for
    # as long as the player leaves it there.
    out = loop_seam(out, int(TARGET_RATE * 0.35))

    peak = max(abs(s) for s in out) if out else 0.0
    gain = (PEAK_TARGET / peak) if peak > 0.0 else 1.0
    pcm = quantize_8bit(out, gain)
    print(f"  {len(pcm)} samples ({len(pcm) / TARGET_RATE:.1f}s), gain {gain:.3f}")

    os.makedirs(os.path.dirname(OUT_H), exist_ok=True)
    with open(OUT_H, 'w') as f:
        f.write("""/* GENERATED by tools/generate_story_audio.py - do not edit by hand.
 *
 * Story Mode's soundtrack, decoded from Assets/Audio/story_mode.mp3 and
 * resampled to the DirectSound rate.  Android only: the GBA ROM has no Story
 * Mode, so audio_story_data.c is compiled by the Android CMake target alone
 * and never linked into SpaceUnlimited.gba. */
#ifndef AUDIO_STORY_DATA_H
#define AUDIO_STORY_DATA_H

#include "platform.h"

extern const s8 snd_story_pcm[];
extern const u32 snd_story_len;

#endif
""")

    print(f"writing {OUT_C} ...")
    with open(OUT_C, 'w') as f:
        f.write('/* GENERATED by tools/generate_story_audio.py - do not edit by hand.\n'
                ' * Source: Assets/Audio/story_mode.mp3 '
                f'({len(pcm)} samples @ {TARGET_RATE} Hz). */\n'
                '#include "audio_story_data.h"\n\n')
        f.write(f"const u32 snd_story_len = {len(pcm)};\n")
        f.write(f"const s8 snd_story_pcm[{len(pcm)}] __attribute__((aligned(4))) = {{\n")
        # Compact rows: this array is large, so skip the pretty spacing the
        # arcade tracks use and keep the source file a third smaller.
        for i in range(0, len(pcm), 32):
            row = pcm[i:i + 32]
            f.write(','.join(str(b - 256 if b > 127 else b) for b in row) + ',\n')
        f.write("};\n")

    print(f"done: {os.path.getsize(OUT_C) / 1048576:.1f} MB of C source")


if __name__ == '__main__':
    main()
