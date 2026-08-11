#!/usr/bin/env python3
"""Generate C source files with all assets for Space Unlimited GBA."""

import os
import struct
import zlib
import wave
import math
import random
from fractions import Fraction

# GBATEK (Direct Sound): the GBA internally re-samples all audio to 32.768 kHz
# and "best re-sampling accuracy can be gained by using DMA/Timer rates of
# 32.768kHz, 16.384kHz, or 8.192kHz (ie. fragments of the physical output
# rate)".  We therefore master all PCM at exactly 32,768 Hz; Timer 0 with a
# /256 prescaler and 0xFFFE reload produces that rate exactly (65,536/2 Hz).
TARGET_RATE = 32768

def rgb15(r, g, b):
    # Convert 8-bit RGB to GBA 15-bit BGR555 integer
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    return r5 | (g5 << 5) | (b5 << 10)

def read_wav_mono(path):
    """Decode a PCM WAV to float samples in [-1.0, 1.0)."""
    with wave.open(path, 'rb') as w:
        nch = w.getnchannels()
        sw = w.getsampwidth()
        rate = w.getframerate()
        nframes = w.getnframes()
        raw = w.readframes(nframes)

    samples = []
    if sw == 2:
        fmt = f'<{nframes * nch}h'
        unpacked = struct.unpack(fmt, raw)
        for i in range(0, len(unpacked), nch):
            s = unpacked[i] if nch == 1 else sum(unpacked[i:i+nch]) // nch
            samples.append(s / 32768.0)
    elif sw == 1:
        for i in range(0, len(raw), nch):
            s = (raw[i] - 128) / 128.0
            samples.append(s)
    else:
        raise ValueError(f'{path}: unsupported sample width {sw}')
    return samples, rate

def make_lowpass_kernel(src_rate, cutoff_hz, ntaps):
    """Windowed-sinc low-pass filter (Blackman window), normalized to unity
    gain at DC.  `ntaps` should be odd."""
    M = ntaps - 1
    fc = cutoff_hz / src_rate  # cycles per source sample
    h = []
    for n in range(ntaps):
        k = n - M / 2.0
        if abs(k) < 1e-9:
            sinc = 2.0 * fc
        else:
            sinc = math.sin(2.0 * math.pi * fc * k) / (math.pi * k)
        # Blackman window centered on the main tap (1 at center, 0 at edges;
        # mainlobe width ~ 6*fs/N between -6 dB points, stopband ~ -74 dB)
        w = 0.42 + 0.5 * math.cos(2.0 * math.pi * k / M) + 0.08 * math.cos(4.0 * math.pi * k / M)
        h.append(sinc * w)
    norm = sum(h)
    return [v / norm for v in h]

def resample_wav(path, cutoff_hz, ntaps, target_rate=TARGET_RATE):
    """Anti-aliased resampling via a polyphase windowed-sinc low-pass,
    followed by TPDF dithered quantization to signed 8-bit PCM.

    The old implementation used plain linear interpolation, which is NOT an
    anti-alias filter: for `game.wav` (22,050 Hz source -> 16,384 Hz) the
    entire 8,192-11,025 Hz band (-18.8 dB of the track's energy) folded
    inharmonically into the audible range, turning the soundtrack into
    metallic screech.  `menu.wav` was even worse (48,000 -> 16,384 Hz folds
    everything above 8.2 kHz twice).  Here the spectrum above the target
    Nyquist is removed BEFORE decimation, so the result sounds like the
    source WAV, only at the GBA's native 32,768 Hz / 8-bit depth.
    """
    samples, rate = read_wav_mono(path)

    half = ntaps // 2

    # Exact rational step so the fractional phases repeat with a small period
    # and floating-point drift never accumulates over 4M output samples.
    step = Fraction(rate, target_rate)
    out_len = (len(samples) * target_rate) // rate
    n_src = len(samples)

    # The fractional position cycles with period `step.denominator`; cache the
    # phase-shifted kernels, then the inner loop is plain multiply-accumulate.
    n_phases = step.denominator
    phase_kernels = []
    for p in range(n_phases):
        frac = float(Fraction(p, n_phases))
        pk = []
        # Sample the continuous windowed-sinc at integer taps offset by -frac.
        M = ntaps - 1
        fc = cutoff_hz / rate
        vals = []
        for n in range(ntaps):
            k = n - M / 2.0 - frac
            if abs(k) < 1e-9:
                sinc = 2.0 * fc
            else:
                sinc = math.sin(2.0 * math.pi * fc * k) / (math.pi * k)
            w = 0.42 + 0.5 * math.cos(2.0 * math.pi * k / M) + 0.08 * math.cos(4.0 * math.pi * k / M)
            vals.append(sinc * w)
        norm = sum(vals)
        phase_kernels.append([v / norm for v in vals])

    # Sanity: every fractional-phase kernel must pass DC almost perfectly;
    # a sign error in the window collapses this to ~0 and the output to noise.
    worst = min(abs(sum(k)) for k in phase_kernels)
    assert worst > 0.9, f'low-pass kernels broken for {path}: worst DC gain {worst}'

    filtered = []
    # Track the source position as an integer numerator on a FIXED denominator
    # (Fraction arithmetic auto-reduces 128/256 to 1/2 and would scramble the
    # phase table lookups; pos % DEN / pos // DEN cannot go wrong).
    DEN = step.denominator
    STN = step.numerator
    pos = 0
    for _ in range(out_len):
        base, phase = divmod(pos, DEN)
        taps = phase_kernels[phase]
        j0 = base - half
        acc = 0.0
        sm = samples
        for n in range(ntaps):
            j = j0 + n
            if 0 <= j < n_src:
                acc += taps[n] * sm[j]
        filtered.append(acc)
        pos += STN

    # Peak-normalize with a little headroom, then quantize to 8-bit with
    # triangular-PDF dither (removes the crunchy quantization distortion of
    # plain truncation at the cost of a barely-audible steady hiss, standard
    # practice for low-bitdepth PCM).
    peak = max(1e-12, max(abs(v) for v in filtered))
    gain = 124.0 / peak
    rng = random.Random(0x53474B41)  # fixed seed -> reproducible builds
    out_samples = []
    for v in filtered:
        d = v * gain + (rng.random() - rng.random())
        if d >= 0.0:
            q = int(d + 0.5)
        else:
            q = -int(0.5 - d)
        out_samples.append(max(-128, min(127, q)))
    return out_samples

os.makedirs('gba/include', exist_ok=True)
os.makedirs('gba/src', exist_ok=True)

# 1. Process Audio
# Per-file anti-alias cutoffs/tap counts (chosen from measured source spectra):
#  - menu.wav is 48 kHz: filter at 15.0 kHz so everything near the 16,384 Hz
#    output Nyquist is in the stopband.
#  - game.wav is 22,050 Hz: its band ends at 11,025 Hz so it cannot alias at
#    32,768 Hz; the filter at 10.4 kHz only suppresses interpolation images.
#  - SFX get the same treatment as their sample rate allows.
audio_dir = 'SpaceUnlimited.Windows/Assets/Audio'
menu_snd = resample_wav(os.path.join(audio_dir, 'menu.wav'), 15000, 113)
game_snd = resample_wav(os.path.join(audio_dir, 'game.wav'), 10400, 81)
laser_snd = resample_wav(os.path.join(audio_dir, 'laser.wav'), 14600, 113)
explosion_snd = resample_wav(os.path.join(audio_dir, 'explosion.wav'), 15000, 113)
pickup_snd = resample_wav(os.path.join(audio_dir, 'pickup.wav'), 15000, 113)

print(f"Audio resampled: menu={len(menu_snd)}, game={len(game_snd)}, laser={len(laser_snd)}, expl={len(explosion_snd)}, pickup={len(pickup_snd)}")

with open('gba/include/audio_data.h', 'w') as f:
    f.write("""#ifndef AUDIO_DATA_H
#define AUDIO_DATA_H

#include <tonc.h>

/* Timer 0 / 256 with a 0xFFFE reload overflows at exactly 65,536/2 = 32,768
 * Hz, 1:1 with the GBA's internal audio re-sampler (GBATEK lists 32,768 Hz
 * as a best-accuracy DirectSound timer rate). */
#define AUDIO_SAMPLE_RATE 32768

extern const s8 snd_menu_pcm[];
extern const u32 snd_menu_len;

extern const s8 snd_game_pcm[];
extern const u32 snd_game_len;

extern const s8 snd_laser_pcm[];
extern const u32 snd_laser_len;

extern const s8 snd_explosion_pcm[];
extern const u32 snd_explosion_len;

extern const s8 snd_pickup_pcm[];
extern const u32 snd_pickup_len;

#endif
""")

with open('gba/src/audio_data.c', 'w') as f:
    f.write("""#include "audio_data.h"

""")
    def write_array(name, data):
        f.write(f"const u32 {name}_len = {len(data)};\n")
        f.write(f"const s8 {name}_pcm[{len(data)}] __attribute__((aligned(4))) = {{\n")
        for i in range(0, len(data), 24):
            chunk = data[i:i+24]
            f.write("    " + ", ".join(f"{v}" for v in chunk) + ",\n")
        f.write("};\n\n")

    write_array("snd_menu", menu_snd)
    write_array("snd_game", game_snd)
    write_array("snd_laser", laser_snd)
    write_array("snd_explosion", explosion_snd)
    write_array("snd_pickup", pickup_snd)

print("Audio data generated successfully!")

def decode_png(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'Not a PNG: {path}')
    pos = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    while pos < len(data):
        length, chunk_type = struct.unpack('>I4s', data[pos:pos+8])
        pos += 8
        chunk_data = data[pos:pos+length]
        pos += length + 4
        if chunk_type == b'IHDR':
            width, height, bit_depth, color_type = struct.unpack('>IIBB', chunk_data[:10])
        elif chunk_type == b'IDAT':
            idat.extend(chunk_data)
        elif chunk_type == b'IEND':
            break
            
    raw = zlib.decompress(idat)
    bpp = 4 if color_type == 6 else (3 if color_type == 2 else 4)
    stride = width * bpp
    pixels = []
    prev_line = bytearray(stride)
    src_idx = 0
    
    for y in range(height):
        filter_type = raw[src_idx]
        src_idx += 1
        curr_line = bytearray(stride)
        for x in range(stride):
            filt = raw[src_idx]
            src_idx += 1
            a = curr_line[x - bpp] if x >= bpp else 0
            b = prev_line[x]
            c = prev_line[x - bpp] if x >= bpp else 0
            if filter_type == 0: val = filt
            elif filter_type == 1: val = (filt + a) & 0xFF
            elif filter_type == 2: val = (filt + b) & 0xFF
            elif filter_type == 3: val = (filt + ((a + b) >> 1)) & 0xFF
            elif filter_type == 4:
                p = a + b - c
                pa = abs(p - a); pb = abs(p - b); pc = abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                val = (filt + pr) & 0xFF
            curr_line[x] = val
        prev_line = curr_line
        for px in range(width):
            if color_type == 6:
                r, g, b, a = curr_line[px*4 : px*4+4]
            elif color_type == 2:
                r, g, b = curr_line[px*3 : px*3+3]
                a = 255
            elif color_type == 4:
                g, a = curr_line[px*2 : px*2+2]
                r = b = g
            else:
                r = g = b = curr_line[px]
                a = 255
            pixels.append((r, g, b, a))
    return width, height, pixels

def scale_image(w, h, pixels, target_w, target_h):
    out = []
    for ty in range(target_h):
        for tx in range(target_w):
            sy0 = ty * h / target_h
            sy1 = (ty + 1) * h / target_h
            sx0 = tx * w / target_w
            sx1 = (tx + 1) * w / target_w
            
            r_sum = g_sum = b_sum = a_sum = 0.0
            total_weight = 0.0
            
            iy0 = int(sy0)
            iy1 = min(h, int(math.ceil(sy1)))
            ix0 = int(sx0)
            ix1 = min(w, int(math.ceil(sx1)))
            
            for y in range(iy0, iy1):
                wy = min(y + 1, sy1) - max(y, sy0)
                for x in range(ix0, ix1):
                    wx = min(x + 1, sx1) - max(x, sx0)
                    weight = wx * wy
                    r, g, b, a = pixels[y * w + x]
                    if a > 20:
                        r_sum += r * weight * (a / 255.0)
                        g_sum += g * weight * (a / 255.0)
                        b_sum += b * weight * (a / 255.0)
                        a_sum += a * weight
                    total_weight += weight
            if total_weight > 0 and (a_sum / total_weight) > 40:
                avg_a = a_sum / total_weight
                avg_r = min(255, max(0, int((r_sum / total_weight) / (avg_a / 255.0))))
                avg_g = min(255, max(0, int((g_sum / total_weight) / (avg_a / 255.0))))
                avg_b = min(255, max(0, int((b_sum / total_weight) / (avg_a / 255.0))))
                out.append((avg_r, avg_g, avg_b, int(avg_a)))
            else:
                out.append((0, 0, 0, 0))
    return target_w, target_h, out

# Build Master Palette (256 colors)
palette = [(0, 0, 0)] * 256
palette[0] = (0, 0, 0)
palette[1] = (4, 6, 12)
palette[2] = (8, 12, 24)
palette[3] = (14, 20, 38)
palette[4] = (20, 30, 56)
palette[5] = (30, 44, 78)
palette[6] = (42, 60, 105)
palette[7] = (60, 85, 140)
palette[8] = (80, 110, 175)
palette[9] = (110, 145, 215)
palette[10] = (150, 180, 240)
palette[11] = (180, 205, 255)
palette[12] = (220, 235, 255)
palette[13] = (255, 255, 255)
palette[14] = (6, 10, 20)
palette[15] = (16, 26, 48)

palette[16] = (240, 246, 255)
palette[17] = (180, 200, 225)
palette[18] = (130, 155, 185)
palette[19] = (85, 105, 135)
palette[20] = (50, 70, 100)
palette[21] = (35, 214, 255)
palette[22] = (120, 230, 255)
palette[23] = (18, 130, 170)
palette[24] = (255, 210, 74)
palette[25] = (255, 140, 40)
palette[26] = (255, 60, 60)
palette[27] = (80, 240, 140)
palette[28] = (180, 110, 255)
palette[29] = (25, 40, 70)
palette[30] = (45, 80, 135)
palette[31] = (255, 255, 255)

palette[32] = (22, 26, 35)
palette[33] = (38, 44, 58)
palette[34] = (60, 70, 90)
palette[35] = (90, 102, 125)
palette[36] = (125, 140, 168)
palette[37] = (165, 180, 208)
palette[38] = (210, 225, 245)
palette[39] = (15, 45, 75)
palette[40] = (30, 85, 130)
palette[41] = (60, 145, 200)
palette[42] = (140, 215, 255)
palette[43] = (230, 245, 255)
palette[44] = (10, 15, 25)
palette[45] = (50, 55, 68)
palette[46] = (75, 85, 105)
palette[47] = (195, 210, 235)

palette[48] = (140, 45, 15); palette[49] = (215, 80, 30); palette[50] = (255, 120, 56); palette[51] = (255, 185, 130)
palette[52] = (15, 105, 140); palette[53] = (25, 165, 215); palette[54] = (42, 214, 255); palette[55] = (165, 240, 255)
palette[56] = (95, 35, 150); palette[57] = (145, 65, 215); palette[58] = (188, 92, 255); palette[59] = (225, 175, 255)
palette[60] = (20, 120, 75); palette[61] = (45, 185, 120); palette[62] = (102, 255, 184); palette[63] = (195, 255, 225)
palette[64] = (140, 100, 15); palette[65] = (210, 155, 25); palette[66] = (255, 210, 74); palette[67] = (255, 240, 165)

palette[68] = (180, 50, 10); palette[69] = (255, 120, 56); palette[70] = (255, 220, 140)
palette[71] = (20, 130, 180); palette[72] = (42, 214, 255); palette[73] = (190, 245, 255)
palette[74] = (120, 40, 180); palette[75] = (188, 92, 255); palette[76] = (235, 190, 255)
palette[77] = (30, 140, 90); palette[78] = (102, 255, 184); palette[79] = (200, 255, 230)

for i in range(32):
    t = i / 31.0
    r = int(25 + 160 * t + (10 if i % 2 == 0 else 0))
    g = int(22 + 145 * t)
    b = int(28 + 155 * t + (5 if i % 3 == 0 else 0))
    palette[80 + i] = (min(255, r), min(255, g), min(255, b))

palette[112] = (20, 15, 35); palette[113] = (42, 28, 68); palette[114] = (75, 45, 115); palette[115] = (125, 65, 185)
palette[116] = (175, 95, 245); palette[117] = (220, 160, 255); palette[118] = (140, 15, 85); palette[119] = (225, 30, 130)
palette[120] = (255, 120, 190); palette[121] = (255, 220, 245); palette[122] = (160, 30, 220); palette[123] = (220, 80, 255)
palette[124] = (255, 160, 255); palette[125] = (255, 245, 255); palette[126] = (40, 18, 55); palette[127] = (95, 55, 140)

for i in range(32):
    t = i / 31.0
    if t < 0.25:
        sub = t / 0.25
        r = int(30 + 150 * sub); g = int(15 + 25 * sub); b = int(15 + 15 * sub)
    elif t < 0.6:
        sub = (t - 0.25) / 0.35
        r = int(180 + 75 * sub); g = int(40 + 130 * sub); b = int(30 + 20 * sub)
    elif t < 0.85:
        sub = (t - 0.6) / 0.25
        r = 255; g = int(170 + 75 * sub); b = int(50 + 90 * sub)
    else:
        sub = (t - 0.85) / 0.15
        r = 255; g = int(245 + 10 * sub); b = int(140 + 115 * sub)
    palette[128 + i] = (min(255, r), min(255, g), min(255, b))

palette[160] = (10, 40, 80); palette[161] = (20, 80, 150); palette[162] = (35, 140, 225); palette[163] = (60, 195, 255)
palette[164] = (140, 225, 255); palette[165] = (210, 245, 255); palette[166] = (15, 30, 60); palette[167] = (5, 15, 35)

palette[168] = (80, 55, 10); palette[169] = (150, 110, 20); palette[170] = (220, 170, 35); palette[171] = (255, 215, 70)
palette[172] = (255, 240, 145); palette[173] = (255, 252, 220); palette[174] = (60, 40, 8); palette[175] = (30, 20, 5)

palette[176] = (15, 65, 35); palette[177] = (30, 125, 65); palette[178] = (55, 190, 105); palette[179] = (105, 245, 155)
palette[180] = (175, 255, 205); palette[181] = (235, 255, 245); palette[182] = (12, 45, 25); palette[183] = (6, 25, 12)

for i in range(8):
    t = (i + 1) / 8.0
    palette[184 + i] = (int(30 * t), int(160 * t + 80 * (1-t)), int(255 * t))

for i in range(64):
    t = i / 63.0
    palette[192 + i] = (int(20 + 220 * t), int(30 + 215 * t), int(50 + 205 * t))

def find_closest_color(r, g, b, min_idx=1, max_idx=255):
    best_idx = min_idx
    best_dist = 999999999
    for i in range(min_idx, max_idx + 1):
        pr, pg, pb = palette[i]
        dr = (r - pr) * 0.30
        dg = (g - pg) * 0.59
        db = (b - pb) * 0.11
        dist = dr*dr + dg*dg + db*db
        if dist < best_dist:
            best_dist = dist
            best_idx = i
    return best_idx

img_dir = 'SpaceUnlimited.Windows/Assets/Images'

w_ship, h_ship, px_ship = decode_png(os.path.join(img_dir, 'classic-ship.png'))
_, _, ship_scaled = scale_image(w_ship, h_ship, px_ship, 20, 16)

ship_variants = []
for accent in range(5):
    var_pixels = bytearray(20 * 16)
    for i, (r, g, b, a) in enumerate(ship_scaled):
        if a < 30:
            var_pixels[i] = 0
            continue
        is_accent = (r > 130 and r > g + 25 and b < 110)
        is_cockpit = (b > r + 20 and b > g - 10)
        
        if is_accent:
            bright = (r + g) / 2
            if bright < 110: shade = 0
            elif bright < 170: shade = 1
            elif bright < 220: shade = 2
            else: shade = 3
            var_pixels[i] = 48 + accent * 4 + shade
        elif is_cockpit:
            bright = (r + g + b) / 3
            if bright < 80: var_pixels[i] = 39
            elif bright < 140: var_pixels[i] = 40
            elif bright < 200: var_pixels[i] = 41
            elif bright < 240: var_pixels[i] = 42
            else: var_pixels[i] = 43
        else:
            bright = (r + g + b) / 3
            if bright < 50: var_pixels[i] = 32
            elif bright < 80: var_pixels[i] = 33
            elif bright < 115: var_pixels[i] = 34
            elif bright < 155: var_pixels[i] = 35
            elif bright < 195: var_pixels[i] = 36
            elif bright < 235: var_pixels[i] = 37
            else: var_pixels[i] = 38
    ship_variants.append(var_pixels)

def convert_sprite_to_palette(path, tw, th, min_idx=80, max_idx=111):
    w, h, px = decode_png(path)
    _, _, scaled = scale_image(w, h, px, tw, th)
    out = bytearray(tw * th)
    for i, (r, g, b, a) in enumerate(scaled):
        if a < 30:
            out[i] = 0
        else:
            out[i] = find_closest_color(r, g, b, min_idx, max_idx)
    return out

ast_large = convert_sprite_to_palette(os.path.join(img_dir, 'asteroid-large.png'), 24, 24, 80, 111)
ast_med_a = convert_sprite_to_palette(os.path.join(img_dir, 'asteroid-medium-a.png'), 16, 16, 80, 111)
ast_med_b = convert_sprite_to_palette(os.path.join(img_dir, 'asteroid-medium-b.png'), 16, 16, 80, 111)
ast_small = convert_sprite_to_palette(os.path.join(img_dir, 'asteroid-small.png'), 10, 10, 80, 111)
ast_tiny  = convert_sprite_to_palette(os.path.join(img_dir, 'asteroid-tiny.png'), 6, 6, 80, 111)

drone_data = bytearray(18 * 14)
drone_art = [
    "      112211      ",
    "    1133333311    ",
    "  11344444444311  ",
    " 1344455885544431 ",
    "134455899998554431",
    "1345589AAAA9855431",
    "1345589AAAA9855431",
    "134455899998554431",
    " 1344455885544431 ",
    "  11344444444311  ",
    "   134411114431   ",
    "  1341      1431  ",
    " 131          131 ",
    " 1              1 ",
]
drone_map = {
    ' ': 0,
    '1': 112, '2': 113, '3': 114, '4': 115, '5': 116,
    '8': 118, '9': 119, 'A': 120, 'B': 121
}
for y in range(14):
    row = drone_art[y]
    for x in range(18):
        c = row[x] if x < len(row) else ' '
        drone_data[y * 18 + x] = drone_map.get(c, 0)

laser_standard = bytearray(4 * 10)
for y in range(10):
    for x in range(4):
        if x in (0, 3) and (y < 2 or y > 7): laser_standard[y*4 + x] = 0
        elif x in (1, 2) and y in range(2, 8): laser_standard[y*4 + x] = 16
        else: laser_standard[y*4 + x] = 21

laser_heavy = bytearray(6 * 14)
for y in range(14):
    for x in range(6):
        if (x == 0 or x == 5) and (y < 2 or y > 11): laser_heavy[y*6 + x] = 0
        elif x in (2, 3): laser_heavy[y*6 + x] = 16
        elif x in (1, 4): laser_heavy[y*6 + x] = 22
        else: laser_heavy[y*6 + x] = 23

laser_enemy = bytearray(6 * 6)
for y in range(6):
    for x in range(6):
        dx = x - 2.5; dy = y - 2.5
        dist = math.sqrt(dx*dx + dy*dy)
        if dist > 2.8: laser_enemy[y*6 + x] = 0
        elif dist > 2.0: laser_enemy[y*6 + x] = 122
        elif dist > 1.2: laser_enemy[y*6 + x] = 123
        elif dist > 0.6: laser_enemy[y*6 + x] = 124
        else: laser_enemy[y*6 + x] = 125

shield_bubble = bytearray(24 * 24)
for y in range(24):
    for x in range(24):
        dx = x - 11.5; dy = y - 11.5
        dist = math.sqrt(dx*dx + dy*dy)
        if dist >= 9.5 and dist <= 11.8:
            ring = int((1.0 - abs(dist - 10.65) / 1.15) * 7)
            shield_bubble[y*24 + x] = 184 + max(0, min(7, ring))
        else:
            shield_bubble[y*24 + x] = 0

def make_powerup(icon_type):
    data = bytearray(10 * 10)
    base_color = 160 if icon_type == 0 else (168 if icon_type == 1 else 176)
    for y in range(10):
        for x in range(10):
            dx = x - 4.5; dy = y - 4.5
            dist = math.sqrt(dx*dx + dy*dy)
            if dist > 4.6:
                data[y*10 + x] = 0
            elif dist > 3.6:
                data[y*10 + x] = base_color + 1
            else:
                data[y*10 + x] = base_color + 0
    if icon_type == 0:
        for y in range(3, 7):
            for x in range(3, 7):
                if y == 3 or (y in (4, 5) and x in (3, 6)) or (y == 6 and x in (4, 5)):
                    data[y*10 + x] = base_color + 4
                elif y in (4, 5) and x in (4, 5):
                    data[y*10 + x] = base_color + 3
    elif icon_type == 1:
        for (x, y) in [(4, 2), (5, 2), (3, 3), (4, 3), (5, 3), (3, 4), (4, 4), (5, 4), (6, 4), (4, 5), (5, 5), (4, 6), (5, 6), (4, 7)]:
            data[y*10 + x] = base_color + 4
    elif icon_type == 2:
        for y in range(2, 8):
            data[y*10 + 4] = data[y*10 + 5] = base_color + 4
        for x in range(2, 8):
            data[4*10 + x] = data[5*10 + x] = base_color + 4
    return data

pwr_shield = make_powerup(0)
pwr_rapid  = make_powerup(1)
pwr_repair = make_powerup(2)

expl_frames = []
for i in range(9):
    fpath = os.path.join(img_dir, f'explosion-{i}.png')
    f_data = convert_sprite_to_palette(fpath, 24, 24, 128, 159)
    expl_frames.append(f_data)

font_glyphs = {
    ' ': [0,0,0,0,0,0,0], '!': [4,4,4,4,4,0,4], '"': [10,10,10,0,0,0,0], '#': [10,10,31,10,31,10,10],
    '$': [4,15,20,14,5,30,4], '%': [25,26,2,4,8,19,19], '&': [12,18,20,8,21,18,13], "'": [4,4,2,0,0,0,0],
    '(': [2,4,8,8,8,4,2], ')': [8,4,2,2,2,4,8], '*': [0,10,4,31,4,10,0], '+': [0,4,4,31,4,4,0],
    ',': [0,0,0,0,6,4,8], '-': [0,0,0,31,0,0,0], '.': [0,0,0,0,0,12,12], '/': [1,2,4,8,16,0,0],
    '0': [14,17,19,21,25,17,14], '1': [4,12,4,4,4,4,14], '2': [14,17,1,2,4,8,31], '3': [31,2,4,2,1,17,14],
    '4': [2,6,10,18,31,2,2], '5': [31,16,30,1,1,17,14], '6': [6,8,16,30,17,17,14], '7': [31,1,2,4,8,8,8],
    '8': [14,17,17,14,17,17,14], '9': [14,17,17,15,1,2,12], ':': [0,12,12,0,12,12,0], ';': [0,12,12,0,12,4,8],
    '<': [2,4,8,16,8,4,2], '=': [0,31,0,31,0,0,0], '>': [8,4,2,1,2,4,8], '?': [14,17,1,2,4,0,4],
    '@': [14,17,21,21,29,16,14], 'A': [14,17,17,31,17,17,17], 'B': [30,17,17,30,17,17,30], 'C': [14,17,16,16,16,17,14],
    'D': [28,18,17,17,17,18,28], 'E': [31,16,16,30,16,16,31], 'F': [31,16,16,30,16,16,16], 'G': [14,17,16,23,17,17,15],
    'H': [17,17,17,31,17,17,17], 'I': [14,4,4,4,4,4,14], 'J': [7,2,2,2,2,18,12], 'K': [17,18,20,24,20,18,17],
    'L': [16,16,16,16,16,16,31], 'M': [17,27,21,21,17,17,17], 'N': [17,17,25,21,19,17,17], 'O': [14,17,17,17,17,17,14],
    'P': [30,17,17,30,16,16,16], 'Q': [14,17,17,17,21,18,13], 'R': [30,17,17,30,20,18,17], 'S': [15,16,16,14,1,1,30],
    'T': [31,4,4,4,4,4,4], 'U': [17,17,17,17,17,17,14], 'V': [17,17,17,17,17,10,4], 'W': [17,17,17,21,21,27,17],
    'X': [17,17,10,4,10,17,17], 'Y': [17,17,10,4,4,4,4], 'Z': [31,1,2,4,8,16,31], '[': [14,8,8,8,8,8,14],
    '\\': [16,8,4,2,1,0,0], ']': [14,2,2,2,2,2,14], '^': [4,10,17,0,0,0,0], '_': [0,0,0,0,0,0,31],
    '`': [8,4,2,0,0,0,0], 'a': [0,0,14,1,15,17,15], 'b': [16,16,22,25,17,17,30], 'c': [0,0,14,17,16,17,14],
    'd': [1,1,13,19,17,17,15], 'e': [0,0,14,17,31,16,14], 'f': [6,9,8,28,8,8,8], 'g': [0,0,15,17,15,1,14],
    'h': [16,16,22,25,17,17,17], 'i': [4,0,12,4,4,4,14], 'j': [2,0,6,2,2,18,12], 'k': [16,16,18,20,24,20,18],
    'l': [12,4,4,4,4,4,14], 'm': [0,0,26,21,21,17,17], 'n': [0,0,22,25,17,17,17], 'o': [0,0,14,17,17,17,14],
    'p': [0,0,30,17,30,16,16], 'q': [0,0,15,17,15,1,1], 'r': [0,0,22,25,16,16,16], 's': [0,0,15,16,14,1,30],
    't': [8,8,28,8,8,9,6], 'u': [0,0,17,17,17,19,13], 'v': [0,0,17,17,17,10,4], 'w': [0,0,17,17,21,21,10],
    'x': [0,0,17,10,4,10,17], 'y': [0,0,17,17,15,1,14], 'z': [0,0,31,2,4,8,31], '{': [2,4,4,8,4,4,2],
    '|': [4,4,4,4,4,4,4], '}': [8,4,4,2,4,4,8], '~': [0,0,9,22,0,0,0],
}

# Write gfx_data.h
with open('gba/include/gfx_data.h', 'w') as f:
    f.write("""#ifndef GFX_DATA_H
#define GFX_DATA_H

#include <tonc.h>

#define PAL_SPACE_BLACK 0
#define PAL_TEXT_WHITE 16
#define PAL_TEXT_CYAN 21
#define PAL_TEXT_GOLD 24
#define PAL_TEXT_RED 26
#define PAL_TEXT_GREEN 27
#define PAL_TEXT_VIOLET 28
#define PAL_BTN_BG 29
#define PAL_BTN_HOVER 30
#define PAL_BTN_BORDER 31

extern const u16 master_palette[256];

extern const u8 spr_ship[5][20 * 16];
extern const u8 spr_ast_large[24 * 24];
extern const u8 spr_ast_med_a[16 * 16];
extern const u8 spr_ast_med_b[16 * 16];
extern const u8 spr_ast_small[10 * 10];
extern const u8 spr_ast_tiny[6 * 6];

extern const u8 spr_drone[18 * 14];
extern const u8 spr_laser_standard[4 * 10];
extern const u8 spr_laser_heavy[6 * 14];
extern const u8 spr_laser_enemy[6 * 6];

extern const u8 spr_shield_bubble[24 * 24];
extern const u8 spr_pwr_shield[10 * 10];
extern const u8 spr_pwr_rapid[10 * 10];
extern const u8 spr_pwr_repair[10 * 10];

extern const u8 spr_explosion[9][24 * 24];

extern const u8 font_5x7[96][7];

#endif
""")

# Write gfx_data.c with integer hex constants
with open('gba/src/gfx_data.c', 'w') as f:
    f.write("""#include "gfx_data.h"

const u16 master_palette[256] = {
""")
    for i in range(0, 256, 8):
        chunk = palette[i:i+8]
        colors = [f"0x{rgb15(r, g, b):04X}" for r, g, b in chunk]
        f.write("    " + ", ".join(colors) + ",\n")
    f.write("};\n\n")

    f.write("const u8 spr_ship[5][20 * 16] = {\n")
    for var in ship_variants:
        f.write("    {\n")
        for y in range(16):
            row = var[y*20 : (y+1)*20]
            f.write("        " + ", ".join(f"{b}" for b in row) + ",\n")
        f.write("    },\n")
    f.write("};\n\n")

    def write_u8_array(name, data, width):
        f.write(f"const u8 {name}[{len(data)}] = {{\n")
        for y in range(0, len(data), width):
            row = data[y:y+width]
            f.write("    " + ", ".join(f"{b}" for b in row) + ",\n")
        f.write("};\n\n")

    write_u8_array("spr_ast_large", ast_large, 24)
    write_u8_array("spr_ast_med_a", ast_med_a, 16)
    write_u8_array("spr_ast_med_b", ast_med_b, 16)
    write_u8_array("spr_ast_small", ast_small, 10)
    write_u8_array("spr_ast_tiny", ast_tiny, 6)

    write_u8_array("spr_drone", drone_data, 18)
    write_u8_array("spr_laser_standard", laser_standard, 4)
    write_u8_array("spr_laser_heavy", laser_heavy, 6)
    write_u8_array("spr_laser_enemy", laser_enemy, 6)

    write_u8_array("spr_shield_bubble", shield_bubble, 24)
    write_u8_array("spr_pwr_shield", pwr_shield, 10)
    write_u8_array("spr_pwr_rapid", pwr_rapid, 10)
    write_u8_array("spr_pwr_repair", pwr_repair, 10)

    f.write("const u8 spr_explosion[9][24 * 24] = {\n")
    for frame in expl_frames:
        f.write("    {\n")
        for y in range(24):
            row = frame[y*24 : (y+1)*24]
            f.write("        " + ", ".join(f"{b}" for b in row) + ",\n")
        f.write("    },\n")
    f.write("};\n\n")

    f.write("const u8 font_5x7[96][7] = {\n")
    for code in range(32, 128):
        ch = chr(code)
        rows = font_glyphs.get(ch, [0,0,0,0,0,0,0])
        f.write("    {" + ", ".join(f"{r}" for r in rows) + f"}},\n")
    f.write("};\n\n")

print("Graphics data regenerated with hex literals!")
