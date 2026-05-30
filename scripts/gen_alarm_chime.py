#!/usr/bin/env python3
"""
Synthesize a 4-second cheerful "wake up" chime for the alarm. Same FM-bell
engine as the boot coin drop, but the melody and decay envelope are tuned to
be melodic and kid-friendly rather than percussive.

Pattern: ascending C-major arpeggio with overlapping bell tones, finishing
on a sustained high-C with a sparkle. Original composition — no copyrighted
material, OK to bake into firmware and ship over public OTA.
"""
import math
from pathlib import Path

SAMPLE_RATE = 16000
TOTAL_SEC   = 4.0

# (start_sec, carrier_hz, decay_per_sec, amp)
# C major arpeggio: C5 E5 G5 C6 G5 C6 E6 C6
# Notes overlap by ~250ms so each fades into the next instead of being staccato.
NOTES = [
    (0.00,  523.25, 3.0, 0.70),   # C5
    (0.30,  659.25, 3.0, 0.72),   # E5
    (0.60,  783.99, 3.0, 0.74),   # G5
    (0.95, 1046.50, 3.0, 0.78),   # C6
    (1.40,  783.99, 3.0, 0.66),   # G5 echo
    (1.75, 1046.50, 2.8, 0.78),   # C6
    (2.10, 1318.51, 2.8, 0.80),   # E6 (peak)
    (2.55, 1046.50, 1.8, 0.85),   # C6 sustained
    # Tiny sparkle notes high up so the kid hears a "chime tail"
    (2.85, 1567.98, 6.0, 0.30),   # G6 sparkle
    (3.10, 2093.00, 6.0, 0.25),   # C7 sparkle
]

MOD_RATIO = 1.41    # inharmonic ratio → metallic bell timbre
I0        = 3.2     # modulation index — bright at attack, fades fast

def bell_note(duration_sec, fc, decay, amp):
    n = int(duration_sec * SAMPLE_RATE)
    out = [0.0] * n
    fm = fc * MOD_RATIO
    for i in range(n):
        t = i / SAMPLE_RATE
        attack  = min(1.0, t / 0.005)
        env_car = math.exp(-decay * t)
        mod_idx = I0 * math.exp(-decay * 2.0 * t)
        phase   = 2.0 * math.pi * fc * t + mod_idx * math.sin(2.0 * math.pi * fm * t)
        out[i]  = amp * attack * env_car * math.sin(phase)
    return out

def mix():
    n_total = int(TOTAL_SEC * SAMPLE_RATE)
    buf = [0.0] * n_total
    for (start_sec, fc, decay, amp) in NOTES:
        dur = TOTAL_SEC - start_sec
        note = bell_note(dur, fc, decay, amp)
        start_i = int(start_sec * SAMPLE_RATE)
        for i, v in enumerate(note):
            if start_i + i < n_total:
                buf[start_i + i] += v
    # soft-clip on overlap peaks so we don't get harsh square-wave clipping
    buf = [math.tanh(v) for v in buf]
    peak = max(abs(v) for v in buf) or 1.0
    scale = 0.85 / peak
    return [v * scale for v in buf]

def to_int16(samples):
    return [max(-32768, min(32767, int(v * 32767))) for v in samples]

def emit_c(samples_i16, sym: str, out_c: Path, out_h: Path):
    lines = []
    for i in range(0, len(samples_i16), 12):
        chunk = samples_i16[i:i + 12]
        lines.append("    " + ", ".join(f"{v:6d}" for v in chunk) + ",")
    body = "\n".join(lines)
    out_c.write_text(f"""#include <stdint.h>
#include <stddef.h>

const int16_t {sym}_data[] = {{
{body}
}};

const size_t   {sym}_samples     = {len(samples_i16)};
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
    root    = Path(__file__).resolve().parents[1]
    out_dir = root / "src" / "assets"
    out_dir.mkdir(parents=True, exist_ok=True)
    samples = mix()
    i16     = to_int16(samples)
    emit_c(i16, "alarm_chime", out_dir / "alarm_chime.c", out_dir / "alarm_chime.h")
    print(f"wrote {len(i16)} samples @ {SAMPLE_RATE} Hz "
          f"= {len(i16)*2/1024:.1f} KB raw → {out_dir / 'alarm_chime.c'}")

if __name__ == "__main__":
    main()
