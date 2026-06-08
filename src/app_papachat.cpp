#include "app_papachat.h"
#include "papa_quote.h"
#include "voice_rec.h"
#include "voice_inbox.h"
#include <lvgl.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

LV_FONT_DECLARE(mont_light_24);

namespace app_papachat {

static lv_obj_t* s_root    = nullptr;
static lv_obj_t* s_quote   = nullptr;
static lv_obj_t* s_list    = nullptr;     // scrollable voice-mail list
static lv_obj_t* s_rec_lbl = nullptr;     // record state ("BtnA: record" / "REC 3s" / "Sent OK")
static char      s_shown[256] = "\x01";   // force first refresh
static bool      s_need_fetch = false;    // pull the inbox once after the screen is up

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

static void onItemClick(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(btn);
    voice_inbox::play(i);
    if (s_rec_lbl) lv_label_set_text(s_rec_lbl, "Playing...");
}

static void fmtRow(const voice_inbox::Item& it, char* out, size_t n) {
    int s = (int)(it.secs + 0.5f);
    time_t now = time(nullptr);
    long age = (now > (time_t)it.ts) ? (long)(now - it.ts) : 0;
    char when[16];
    if      (age < 90)    snprintf(when, sizeof(when), "now");
    else if (age < 3600)  snprintf(when, sizeof(when), "%ldm ago", age / 60);
    else if (age < 86400) snprintf(when, sizeof(when), "%ldh ago", age / 3600);
    else                  snprintf(when, sizeof(when), "%ldd ago", age / 86400);
    snprintf(out, n, LV_SYMBOL_PLAY "  %d:%02d   %s", s / 60, s % 60, when);
}

static void rebuildList() {
    if (!s_list) return;
    lv_obj_clean(s_list);
    int n = voice_inbox::count();
    if (n == 0) {
        lv_obj_t* empty = lv_label_create(s_list);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x6E4E26), 0);
        lv_label_set_text(empty, "no voice yet");
        return;
    }
    for (int i = 0; i < n; i++) {
        char row[40];
        fmtRow(voice_inbox::item(i), row, sizeof(row));
        lv_obj_t* btn = lv_button_create(s_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A1E10), 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, onItemClick, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xF5E8D0), 0);
        lv_label_set_text(lbl, row);
        lv_obj_center(lbl);
    }
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    lv_obj_t* hdr = lv_label_create(s_root);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0xC07A2E), 0);   // warm amber
    lv_label_set_text(hdr, "FROM PAPA");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 58);

    s_quote = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_quote, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_quote, lv_color_hex(0xF5E8D0), 0);   // cream
    lv_obj_set_style_text_align(s_quote, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_quote, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_quote, 300);                                    // stay off the round edge
    lv_obj_align(s_quote, LV_ALIGN_TOP_MID, 0, 86);
    s_shown[0] = '\x01';                                               // force refresh
    refresh();

    // Voice mailbox: a scrollable list of PAPA's clips; tap one to play it.
    s_list = lv_obj_create(s_root);
    lv_obj_set_size(s_list, 320, 150);
    lv_obj_align(s_list, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    rebuildList();
    s_need_fetch = true;

    s_rec_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_rec_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_rec_lbl, lv_color_hex(0x80C060), 0);
    lv_label_set_text(s_rec_lbl, "BtnA: record 5s to PAPA");
    lv_obj_align(s_rec_lbl, LV_ALIGN_BOTTOM_MID, 0, -86);

    lv_obj_t* hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6E4E26), 0);
    lv_label_set_text(hint, "tap a clip to play  ·  BtnA+B back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -62);
}

void leave() {
    if (s_root) {
        lv_obj_clean(s_root);
        s_root = nullptr; s_quote = nullptr; s_list = nullptr; s_rec_lbl = nullptr;
    }
}

void tick() {
    if (s_quote) refresh();
    if (s_need_fetch) {                 // one-shot inbox pull, after the screen is drawn
        s_need_fetch = false;
        voice_inbox::fetch();
        rebuildList();
    }
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
