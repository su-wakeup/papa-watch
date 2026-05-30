#include "face_boot.h"
#include <Arduino.h>
#include <lvgl.h>

LV_FONT_DECLARE(pacifico_64);

namespace face_boot {

// Each letter of "Stanley" gets a rainbow hue
static const char*    LETTERS[7]   = {"S","t","a","n","l","e","y"};
static const uint32_t HUES[7]      = {
    0xFF4060,   // S — coral red
    0xFF9100,   // t — orange
    0xFFD800,   // a — yellow
    0x4ED658,   // n — leaf green
    0x42C8FF,   // l — sky cyan
    0x9C7BFF,   // e — soft violet
    0xFF6FAE,   // y — bubblegum pink
};

static lv_obj_t* s_scr           = nullptr;
static lv_obj_t* s_letters[7]    = {};
static lv_obj_t* s_tag           = nullptr;
static uint32_t  s_start_ms      = 0;
static bool      s_done          = false;

// Timeline (ms relative to s_start_ms) — leisurely so the eye can follow
// each letter being "drawn" left-to-right via horizontal scale + opacity.
static constexpr uint32_t LETTER_DELAY      = 280;   // stagger between letters
static constexpr uint32_t LETTER_FADE_MS    = 520;   // per-letter draw duration
static constexpr uint32_t LETTERS_DONE_MS   = LETTER_DELAY * 6 + LETTER_FADE_MS;
static constexpr uint32_t TAG_START_MS      = LETTERS_DONE_MS + 350;
static constexpr uint32_t TAG_FADE_MS       = 900;
static constexpr uint32_t HOLD_END_MS       = TAG_START_MS + TAG_FADE_MS + 4200;
static constexpr uint32_t FADEOUT_MS        = 900;
static constexpr uint32_t TOTAL_MS          = HOLD_END_MS + FADEOUT_MS;

void create() {
    s_scr = lv_screen_active();
    lv_obj_clean(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    // ── flex row for "Stanley" letters, centered ──
    // Auto-sized width so the 7-letter run never gets clipped at the screen edge.
    lv_obj_t* row = lv_obj_create(s_scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 100);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 0, 0);

    for (int i = 0; i < 7; i++) {
        s_letters[i] = lv_label_create(row);
        lv_obj_set_style_text_font(s_letters[i], &pacifico_64, 0);
        lv_obj_set_style_text_color(s_letters[i], lv_color_hex(HUES[i]), 0);
        lv_obj_set_style_text_opa(s_letters[i], LV_OPA_TRANSP, 0);
        lv_label_set_text(s_letters[i], LETTERS[i]);
        // NB: we INTENTIONALLY don't use transform_scale_x for the "stroke
        // draw" effect. Transforming text forces LVGL to render to an
        // off-screen buffer (~38KB per letter at this font size). With our
        // ~95KB internal LVGL pool, animating 3+ letters simultaneously
        // overflows the pool and the later letters silently fail to render
        // (boot animation appears to "stop at Sta"). Plain opacity ramp is
        // visually similar (left-to-right appearance) and costs zero buffer.
    }

    // ── tagline below: clean Montserrat to contrast with script hero ──
    s_tag = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_tag, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_tag, lv_color_hex(0xCFCFCF), 0);
    lv_obj_set_style_text_opa(s_tag, LV_OPA_TRANSP, 0);
    lv_label_set_text(s_tag, "Papa loves you, forever");
    lv_obj_align(s_tag, LV_ALIGN_CENTER, 0, 85);

    // Don't capture start time here — setup() does a lot more work (NTP, MQTT
    // bootstrap, etc.) before loop() gets a chance to actually render. If we
    // started counting now, by first render we'd already be 1-2s in and the
    // letters would pop out half-drawn. Capture in update() on first frame.
    s_start_ms = 0;
    s_done = false;
}

static inline lv_opa_t lerpOpa(uint32_t t, uint32_t dur) {
    if (t >= dur) return LV_OPA_COVER;
    return (lv_opa_t)((t * 255U) / dur);
}

void update() {
    if (s_done || !s_scr) return;
    if (s_start_ms == 0) {           // first frame — start the clock now
        s_start_ms = millis();
        return;
    }
    uint32_t t = millis() - s_start_ms;

    // Per-letter staggered opacity ramp (left-to-right appearance). Eased
    // out so each letter "lands" rather than linearly fades.
    for (int i = 0; i < 7; i++) {
        uint32_t start = i * LETTER_DELAY;
        lv_opa_t opa;
        if (t < start) {
            opa = LV_OPA_TRANSP;
        } else if (t < start + LETTER_FADE_MS) {
            float p = (float)(t - start) / LETTER_FADE_MS;
            float eased = 1.0f - (1.0f - p) * (1.0f - p);
            opa = (lv_opa_t)(255.0f * eased);
        } else {
            opa = LV_OPA_COVER;
        }
        lv_obj_set_style_text_opa(s_letters[i], opa, 0);
    }

    // Tagline fade-in
    if (t >= TAG_START_MS) {
        lv_obj_set_style_text_opa(s_tag, lerpOpa(t - TAG_START_MS, TAG_FADE_MS), 0);
    }

    // Whole-screen fade-out
    if (t >= HOLD_END_MS) {
        uint32_t ft = t - HOLD_END_MS;
        lv_opa_t opa = (ft >= FADEOUT_MS) ? LV_OPA_TRANSP
                                          : (lv_opa_t)(255U - (ft * 255U) / FADEOUT_MS);
        for (int i = 0; i < 7; i++) {
            lv_obj_set_style_text_opa(s_letters[i], opa, 0);
        }
        lv_obj_set_style_text_opa(s_tag, opa, 0);
        if (ft >= FADEOUT_MS) s_done = true;
    }
}

bool finished() { return s_done; }

void destroy() {
    if (s_scr) {
        lv_obj_clean(s_scr);
    }
    s_scr = nullptr;
    for (int i = 0; i < 7; i++) s_letters[i] = nullptr;
    s_tag = nullptr;
}

}  // namespace face_boot
