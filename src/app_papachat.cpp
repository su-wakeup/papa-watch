#include "app_papachat.h"
#include "papa_quote.h"
#include "voice_rec.h"
#include <lvgl.h>
#include <string.h>

LV_FONT_DECLARE(mont_light_24);

namespace app_papachat {

static lv_obj_t* s_root  = nullptr;
static lv_obj_t* s_quote = nullptr;
static lv_obj_t* s_rec_lbl = nullptr;     // record state ("Hold A to talk" / "REC 3s" / "Sent OK")
static char      s_shown[256] = "\x01";   // force first refresh

static const char* PLACEHOLDER =
    "PAPA's note for you will\nshow up here.";

static void refresh() {
    const char* q = papa_quote::text();
    const char* show = (q && q[0]) ? q : PLACEHOLDER;
    if (strncmp(show, s_shown, sizeof(s_shown)) == 0) return;
    strncpy(s_shown, show, sizeof(s_shown) - 1);
    s_shown[sizeof(s_shown) - 1] = 0;
    lv_label_set_text(s_quote, show);
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    lv_obj_t* hdr = lv_label_create(s_root);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0xC07A2E), 0);   // warm amber
    lv_label_set_text(hdr, "FROM PAPA");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 96);

    s_quote = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_quote, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_quote, lv_color_hex(0xF5E8D0), 0);   // cream
    lv_obj_set_style_text_align(s_quote, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_quote, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_quote, 340);                                    // stay off the round edge
    lv_obj_align(s_quote, LV_ALIGN_CENTER, 0, 0);
    s_shown[0] = '\x01';                                               // force refresh
    refresh();

    s_rec_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_rec_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_rec_lbl, lv_color_hex(0x80C060), 0);
    lv_label_set_text(s_rec_lbl, "BtnA: record 5s to PAPA");
    lv_obj_align(s_rec_lbl, LV_ALIGN_BOTTOM_MID, 0, -110);

    lv_obj_t* hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6E4E26), 0);
    lv_label_set_text(hint, "BtnA+B back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -86);
}

void leave() {
    if (s_root) { lv_obj_clean(s_root); s_root = nullptr; s_quote = nullptr; s_rec_lbl = nullptr; }
}

void tick() {
    if (s_quote) refresh();
    voice_rec::tick();
    if (s_rec_lbl) {
        const char* st = voice_rec::statusText();   // shows REC.. / Uploading.. / Sent OK
        if (st[0]) lv_label_set_text(s_rec_lbl, st);
    }
}

void onButtonA() {
    voice_rec::start();
    if (s_rec_lbl) lv_label_set_text(s_rec_lbl, voice_rec::statusText());
}

}  // namespace app_papachat
