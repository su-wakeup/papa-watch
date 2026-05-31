# CLAUDE.md — papa-watch

A personal project, not a product. M5Stack StopWatch firmware modded into a
father–son wrist watch + a Cloudflare Worker bot that bridges Telegram ↔ MQTT
↔ watch. User is in Shenzhen; wife and son (Stanley, ~10, US passport, attends
Lakeside Elementary) live in Los Gatos, CA. This watch exists because the
family is mostly apart. Treat it accordingly — emotional weight matters here.

## Stack

* **Firmware** — PlatformIO + Arduino-ESP32 + LVGL 9.2 (PSRAM ping-pong
  buffers, ~438 KB × 2). Sources live flat in `src/`. Hardware: ESP32-S3,
  466×466 round AMOLED, BMI270 IMU, **no magnetometer** (that's why the
  compass is implemented as a sundial). Steady-state free heap ≈ 150–250 KB;
  watchdog in `main.cpp` reboots if it dips below 30 KB.
* **Bot** — Cloudflare Worker at `bot/src/index.js`. The `HeartRelay` Durable
  Object holds the long-lived MQTT-over-WSS subscription that the stateless
  Worker can't. Deploy with `npx wrangler deploy` from `bot/`.
* **Broker** — free public `broker.emqx.io` over WSS, QoS 1, topics
  `stopwatch/<pair>/from-{dad,son}`.
* **OTA** — tag `v*.*.*` on `github.com/su-wakeup/papa-watch` → CI builds
  firmware → GitHub Release → the watch pulls.

## Conventions

* `face_*` — watch faces (subpages of the Watch app's tileview).
* `app_*` — top-level launcher apps.
* `assets/reference/` — third-party material we mine for assets (LILYGO
  smartwatch demo etc.). Lift PNGs and fonts, do not port whole subsystems.
* Memory is precious. Don't allocate per-frame. Don't redraw what didn't change.
* Don't write multi-paragraph header comments. One short line when WHY is
  non-obvious; never WHAT.
* **Time-zone calls**: never call `setenv("TZ",...)+tzset()` per-tick directly.
  ESP32 newlib's `tzset()` leaks ~40 B/call; at face-update rates this
  triggers the heap watchdog within hours. Always go through
  `tz_helper::localtime_in(tz, epoch, &tm_out)` — it caches the offset per
  zone for 60 s. (Also: `timezone` global is not exported by ESP32 newlib,
  and `tm_gmtoff` isn't portable — recover the offset by diffing
  `localtime_r` and `gmtime_r` of the same epoch.)

## Talking to the user

* **Reply in simplified Chinese.** Bilingual; English in code is fine.
* User is a robotics-startup founder splitting time China/Silicon Valley, on
  L1A, no green card. Often tired in the evenings. Prefer warm tone, concrete
  proposals, ASCII mockups, option cards. Avoid abstract brainstorming when
  the question is concrete.
* For the watch specifically: build functional plumbing first with placeholder
  visuals; UI/UX polish is a separate dedicated pass, not interleaved.
* Watch-face status indicators must be **face-specific** (Casio LCD dots,
  mech-face brass icons, etc.) — never a fixed-position overlay. Past
  feedback: fixed overlays "look like stickers and break the face's integrity."

---

## Behavioral guidelines (adapted from Andrej Karpathy's CLAUDE.md)

These bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think Before Coding

State assumptions explicitly. If multiple interpretations exist, present
them — don't pick silently. If a simpler approach exists, say so. If something
is unclear, stop and name what's confusing.

### 2. Simplicity First

Minimum code that solves the problem. No features beyond what was asked, no
abstractions for single-use code, no "flexibility" or "configurability" that
wasn't requested, no error handling for impossible scenarios. If you wrote
200 lines and it could be 50, rewrite it.

### 3. Surgical Changes

Touch only what you must. Don't "improve" adjacent code, comments, or
formatting. Don't refactor things that aren't broken. Match existing style
even if you'd do it differently. Remove imports/symbols **your** changes made
unused — leave pre-existing dead code alone unless asked. Every changed line
should trace directly to the user's request.

### 4. Goal-Driven Execution

Transform tasks into verifiable goals: "Fix the bug" → "Write a test that
reproduces it, then make it pass." For multi-step tasks, state a brief plan
with per-step verification before running.

---

These guidelines are working if: fewer unnecessary changes in diffs, fewer
rewrites due to overcomplication, and clarifying questions come **before**
implementation rather than after mistakes.
