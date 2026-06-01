# App icon PNGs — spec for the launcher carousel

These feed the launcher's image-zoom carousel (the "silky" version). Drop the
finished PNGs in this folder using the exact names below; I convert them to
LVGL image C arrays and wire them into the wheel.

## The 7 icons (one per wheel app)

| file              | app        | suggested glyph        |
|-------------------|------------|------------------------|
| `watch.png`       | WATCH      | a watch / clock face   |
| `stopwatch.png`   | STOPWATCH  | stopwatch              |
| `schedule.png`    | SCHEDULE   | calendar               |
| `aichat.png`      | AI CHAT    | robot / chat-sparkle   |
| `papa.png`        | PAPA       | heart / chat heart     |
| `compass.png`     | SUNDIAL    | compass / sun-dial     |
| `settings.png`    | SETTINGS   | gear                   |

(HOME is not in the wheel, so no icon needed for it.)

## Format — please match exactly

- **PNG, RGBA, transparent background** (alpha channel, NOT a black/white fill).
- **128 × 128 px**, square. (This is the center size at 1.0×; side icons are the
  same image shown at 0.5× = 64 px, so keep it legible when shrunk by half.)
- **~12 px transparent margin** all around → artwork lives in the center ~104×104.
- **Single colour = WHITE (`#FFFFFF`) line/solid art.** Don't bake in amber or
  colour. I recolour at runtime: center icon → amber `#E6A050`, side icons →
  dim `#705840`. White-on-transparent is what lets me tint + dim per slot and
  keep the watch's look. (If you really want full-colour icons, that's possible
  too, but then I can't dim the sides — say so and I'll handle it.)
- **Consistent set**: same stroke weight + same style across all 7 (all line,
  or all solid). Simple enough to read at 64 px.

## If generating with Nano Banana / Gemini image

Prompt shape that tends to work:

> minimalist **white line icon** of a `<subject>`, single colour, **transparent
> background**, centered, thick even strokes legible at small size, no text, no
> shadow, no background

Generate each, export, then crop to a square and resize to 128×128 with the
margin above. If the export has a solid background instead of transparency,
leave it — tell me and I'll knock the background out during conversion.

## Handoff

Drop the 7 files here and ping me. I run the PNG→LVGL conversion, swap the
carousel from font glyphs to `lv_image` + `lv_image_set_scale` (continuous
smooth zoom, no size "jump"), and flash. Pure asset swap on your side.
