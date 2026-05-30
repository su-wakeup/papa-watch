#include "heart_overlay.h"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

LV_IMAGE_DECLARE(img_heart);

namespace heart_overlay {

// timing
static constexpr uint32_t POP_MS    = 380;        // grow 0 → 100%
static constexpr uint32_t BREATHE   = 1800;       // gentle pulsing
static constexpr uint32_t FADE_MS   = 1300;       // grow + fade out
static constexpr uint32_t TOTAL_MS  = POP_MS + BREATHE + FADE_MS;

// the heart image is 200×200; "1.0 scale" in LVGL is 256.
static constexpr int BASE_SCALE = 256;

static lv_obj_t* s_img       = nullptr;
static uint32_t  s_start_ms  = 0;
static bool      s_active    = false;

void create() {
    s_img = lv_image_create(lv_layer_top());
    lv_image_set_src(s_img, &img_heart);
    lv_obj_align(s_img, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_pivot(s_img, 100, 100);          // center of 200×200
    lv_obj_set_style_img_opa(s_img, LV_OPA_TRANSP, 0);
    lv_image_set_scale(s_img, 1);                 // ~0 (LVGL min is 1)
}

void trigger() {
    if (!s_img) return;
    s_start_ms = millis();
    s_active   = true;
}

void update() {
    if (!s_active || !s_img) return;
    uint32_t t = millis() - s_start_ms;

    int      scale;
    lv_opa_t opa;

    if (t < POP_MS) {
        // ease-out cubic, growing from 0 to 100%
        float p = (float)t / POP_MS;
        float eased = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
        scale = (int)(BASE_SCALE * eased) + 1;
        opa   = LV_OPA_COVER;
    }
    else if (t < POP_MS + BREATHE) {
        // gentle pulse: 100% ± 4% over ~3 cycles
        float p = (float)(t - POP_MS) / BREATHE;
        scale = BASE_SCALE + (int)(10.0f * sinf(p * 2.0f * 3.14159265f * 2.0f));
        opa   = LV_OPA_COVER;
    }
    else if (t < TOTAL_MS) {
        // grow + fade
        float p = (float)(t - POP_MS - BREATHE) / FADE_MS;
        scale = BASE_SCALE + (int)(48.0f * p);
        opa   = (lv_opa_t)((1.0f - p) * 255.0f);
    }
    else {
        scale   = 1;
        opa     = LV_OPA_TRANSP;
        s_active = false;
    }

    lv_image_set_scale(s_img, scale);
    lv_obj_set_style_img_opa(s_img, opa, 0);
}

bool active() { return s_active; }

}  // namespace heart_overlay
