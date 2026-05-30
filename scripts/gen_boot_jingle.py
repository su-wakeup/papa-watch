#!/usr/bin/env python3
"""
Synthesize a "coins falling" boot sound and emit it as a C int16 PCM array.

Each coin is rendered with classic FM bell synthesis:
  y(t) = exp(-decay*t) * sin(2π*fc*t + I(t)*sin(2π*fm*t))
where fm = mod_ratio * fc and I(t) decays faster than the carrier envelope.
This produces a metallic, inharmonic timbre that pure additive sin can't —
which is exactly the "coin clink" character.

The piece is ~1.6 seconds: 9 staggered coins, starting fast then thinning out
like a small pile cascading. A lower-pitched final ping suggests the pile
settling. Output mono 16 kHz int16, ~50 KB.
"""
import math
from pathlib import Path

SAMPLE_RATE = 16000
TOTAL_SEC   = 0.95

# One single coin. A faint, slightly delayed secondary ping suggests the
# coin tap-tap settling on a hard surface — but kept very subtle so it
# reads as ONE coin, not a cascade.
COINS = [
    # (start_sec, carrier_hz, decay_per_sec, amp)
    (0.00, 2350, 6.5, 0.98),   # main strike — long ring, full volume
    (0.13, 2050, 9.0, 0.28),   # tiny bounce — quick decay, low amp
]

MOD_RATIO  = 1.41   # inharmonic ratio → metallic timbre
I0         = 4.5    # initial modulation index (how "bright" the bell starts)

def coin_ping(duration_sec: float, fc: float, decay: float, amp: float):
    n = int(duration_sec * SAMPLE_RATE)
    out = [0.0] * n
    fm = fc * MOD_RATIO
    for i in range(n):
        t = i / SAMPLE_RATE
        # tiny attack to avoid click
        attack = min(1.0, t / 0.003)
        carrier_env = math.exp(-decay * t)
        # modulator envelope decays ~2× faster (the "brightness fades" effect)
        mod_idx = I0 * math.exp(-decay * 2.2 * t)
        phase = 2.0 * math.pi * fc * t + mod_idx * math.sin(2.0 * math.pi * fm * t)
        out[i] = amp * attack * carrier_env * math.sin(phase)
    return out

def mix():
    n_total = int(TOTAL_SEC * SAMPLE_RATE)
    buf = [0.0] * n_total
    for (start_sec, fc, decay, amp) in COINS:
        dur = TOTAL_SEC - start_sec
        ping = coin_ping(dur, fc, decay, amp)
        start_i = int(start_sec * SAMPLE_RATE)
        for i, v in enumerate(ping):
            if start_i + i < n_total:
                buf[start_i + i] += v
    # soft-clip via tanh so overlapping peaks don't square-wave clip
    buf = [math.tanh(v) for v in buf]
    # peak-normalize to 0.85 for headroom
    peak = max(abs(v) for v in buf) or 1.0
    scale = 0.85 / peak
    return [v * scale for v in buf]

def to_int16(samples):
    return [max(-32768, min(32767, int(v * 32767))) for v in samples]

def emit_c(samples_i16, sym: str, out_c: Path, out_h: Path):
    n = len(samples_i16)
    lines = []
    for i in range(0, n, 12):
        chunk = samples_i16[i:i + 12]
        lines.append("    " + ", ".join(f"{v:6d}" for v in chunk) + ",")
    body = "\n".join(lines)
    out_c.write_text(f"""#include <stdint.h>
#include <stddef.h>

const int16_t {sym}_data[] = {{
{body}
}};

const size_t   {sym}_samples     = {n};
const uint32_t {sym}_sample_rate = {SAMPLE_RATE};
""")
    out_h.write_text(f"""#pragma once
#include <stdint.h>
#include <stddef.h>

extern const int16_t  {sym}_data[];
extern const size_t   {sym}_samples;
extern const uint32_t {sym}_sample_rate;
""")

def main():
    root = Path(__file__).resolve().parents[1]
    out_dir = root / "src" / "assets"
    out_dir.mkdir(parents=True, exist_ok=True)
    samples = mix()
    i16 = to_int16(samples)
    emit_c(i16, "boot_jingle", out_dir / "boot_jingle.c", out_dir / "boot_jingle.h")
    print(f"wrote {len(i16)} samples @ {SAMPLE_RATE} Hz "
          f"= {len(i16)*2/1024:.1f} KB raw "
          f"to {out_dir/'boot_jingle.c'}")

if __name__ == "__main__":
    main()
