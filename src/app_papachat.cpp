#include "app_papachat.h"
#include "papa_quote.h"
#include "voice_rec.h"
#include "voice_inbox.h"
#include "note_inbox.h"
#include <lvgl.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

LV_FONT_DECLARE(mont_light_24);

namespace app_papachat {

static lv_obj_t* s_root    = nullptr;
static lv_obj_t* s_log     = nullptr;     // the chat timeline (scrollable flex column)
static lv_obj_t* s_rec_lbl = nullptr;     // composer status ("hold BtnA" / "REC 3s" / "Sent OK")
static char      s_shown[256] = "\x01";   // last-rendered note text — rebuild on change
static bool      s_need_fetch = false;    // pull notes + inbox once after the screen is up

// One row on the timeline. kind 0 = a text note (idx into note_inbox, or -1 for
// the live retained note), kind 1 = a voice clip (idx into voice_inbox; its .out
// flag decides left/right). Merged from all sources and sorted by ts so notes
// and clips, incoming and outgoing, interleave in one chronological feed.
struct Msg { uint32_t ts; int kind; int idx; };

static void fmtAge(uint32_t ts, char* out, size_t n) {
    if (ts == 0) { out[0] = 0; return; }
    time_t now = time(nullptr);
    long age = (now > (time_t)ts) ? (long)(now - ts) : 0;
    if      (age < 90)    snprintf(out, n, "now");
    else if (age < 3600)  snprintf(out, n, "%ldm ago", age / 60);
    else if (age < 86400) snprintf(out, n, "%ldh ago", age / 3600);
    else                  snprintf(out, n, "%ldd ago", age / 86400);
}

static void onBubbleClick(lv_event_t* e) {
    lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(b);
    voice_inbox::play(i);
    if (s_rec_lbl) lv_label_set_text(s_rec_lbl, "Playing...");
}

// Full-width row that pushes its single bubble to one side (incoming → left,
// outgoing → right), Telegram-style.
static lv_obj_t* makeRow(bool right) {
    lv_obj_t* row = lv_obj_create(s_log);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, right ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return row;
}

// A message bubble: warm dark for incoming, dark green for Stanley's outgoing.
static lv_obj_t* makeBubble(lv_obj_t* parent, bool out) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, 250, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(out ? 0x1E3320 : 0x241A0E), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 12, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, 3, 0);
    return b;
}

static void mkTime(lv_obj_t* bubble, uint32_t ts) {
    char when[16];
    fmtAge(ts, when, sizeof(when));
    if (!when[0]) return;
    lv_obj_t* t = lv_label_create(bubble);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x7A5A30), 0);
    lv_label_set_text(t, when);
}

static void renderTextBubble(uint32_t ts, const char* text) {
    lv_obj_t* b = makeBubble(makeRow(false), false);
    lv_obj_t* t = lv_label_create(b);
    lv_obj_set_style_text_font(t, &mont_light_24, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xF5E8D0), 0);   // cream
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, 222);                                    // wrap inside the bubble
    lv_label_set_text(t, text);
    mkTime(b, ts);
}

static void renderVoiceBubble(int idx) {
    const voice_inbox::Item& it = voice_inbox::item(idx);
    lv_obj_t* b = makeBubble(makeRow(it.out), it.out);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(b, (void*)(intptr_t)idx);
    lv_obj_add_event_cb(b, onBubbleClick, LV_EVENT_CLICKED, nullptr);

    int s = (int)(it.secs + 0.5f);
    char row[24];
    snprintf(row, sizeof(row), LV_SYMBOL_PLAY "  %d:%02d", s / 60, s % 60);
    lv_obj_t* r = lv_label_create(b);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(r, lv_color_hex(0xF5E8D0), 0);
    lv_label_set_text(r, row);
    mkTime(b, it.ts);
}

static void buildTimeline() {
    if (!s_log) return;
    lv_obj_clean(s_log);

    Msg msgs[15 + 1 + 20];
    const int CAP = (int)(sizeof(msgs) / sizeof(msgs[0]));
    int m = 0;

    // text notes — fetched history
    int nn = note_inbox::count();
    for (int i = 0; i < nn && m < CAP; i++)
        msgs[m++] = { note_inbox::item(i).ts, 0, i };

    // the live retained note, only if the fetched history doesn't already hold it
    const char* latest = papa_quote::text();
    if (latest && latest[0] && m < CAP) {
        bool dup = false;
        for (int i = 0; i < nn; i++)
            if (strncmp(latest, note_inbox::item(i).text, 240) == 0) { dup = true; break; }
        if (!dup) msgs[m++] = { papa_quote::ts(), 0, -1 };
    }

    // voice clips, both directions
    int nv = voice_inbox::count();
    for (int i = 0; i < nv && m < CAP; i++)
        msgs[m++] = { voice_inbox::item(i).ts, 1, i };

    // insertion sort ascending by ts → oldest on top, newest at the bottom.
    for (int a = 1; a < m; a++) {
        Msg key = msgs[a];
        int b = a - 1;
        while (b >= 0 && msgs[b].ts > key.ts) { msgs[b + 1] = msgs[b]; b--; }
        msgs[b + 1] = key;
    }

    if (m == 0) {
        lv_obj_t* empty = lv_label_create(s_log);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x6E4E26), 0);
        lv_label_set_text(empty, "No messages from PAPA yet.");
        return;
    }

    for (int k = 0; k < m; k++) {
        if (msgs[k].kind == 0) {
            const char* text = msgs[k].idx >= 0 ? note_inbox::item(msgs[k].idx).text
                                                : papa_quote::text();
            renderTextBubble(msgs[k].ts, text);
        } else {
            renderVoiceBubble(msgs[k].idx);
        }
    }

    // jump to the newest message, like opening a chat.
    lv_obj_update_layout(s_log);
    lv_obj_scroll_to_y(s_log, LV_COORD_MAX, LV_ANIM_OFF);
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // ── chat title (pinned) ──
    lv_obj_t* hdr = lv_label_create(s_root);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0xC07A2E), 0);   // warm amber
    lv_label_set_text(hdr, "PAPA");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 40);

    // ── timeline (scrollable): one chronological feed of bubbles ──
    s_log = lv_obj_create(s_root);
    lv_obj_set_size(s_log, 332, 300);
    lv_obj_align(s_log, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_opa(s_log, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_log, 0, 0);
    lv_obj_set_style_pad_all(s_log, 4, 0);
    lv_obj_set_flex_flow(s_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_log, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_log, 8, 0);
    lv_obj_set_scroll_dir(s_log, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_log, LV_SCROLLBAR_MODE_OFF);

    strncpy(s_shown, (papa_quote::text()[0] ? papa_quote::text() : "\x01"),
            sizeof(s_shown) - 1);
    s_shown[sizeof(s_shown) - 1] = 0;
    buildTimeline();
    s_need_fetch = true;

    // ── composer (pinned): record a voice reply to PAPA ──
    s_rec_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_rec_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_rec_lbl, lv_color_hex(0x80C060), 0);
    lv_label_set_text(s_rec_lbl, "hold BtnA to reply");
    lv_obj_align(s_rec_lbl, LV_ALIGN_BOTTOM_MID, 0, -80);

    lv_obj_t* hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6E4E26), 0);
    lv_label_set_text(hint, "tap a voice to play  |  BtnA+B back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -56);
}

void leave() {
    if (s_root) {
        lv_obj_clean(s_root);
        s_root = nullptr; s_log = nullptr; s_rec_lbl = nullptr;
    }
}

void tick() {
    // Rebuild the feed when PAPA's note text changes (cheap; only on change).
    if (s_log) {
        const char* q = papa_quote::text();
        const char* cur = (q && q[0]) ? q : "\x01";
        if (strncmp(cur, s_shown, sizeof(s_shown)) != 0) {
            strncpy(s_shown, cur, sizeof(s_shown) - 1);
            s_shown[sizeof(s_shown) - 1] = 0;
            buildTimeline();
        }
    }
    if (s_need_fetch) {                 // one-shot pull, after the screen is drawn
        s_need_fetch = false;
        note_inbox::fetch();
        voice_inbox::fetch();
        buildTimeline();
    }
    voice_rec::tick();
    if (s_rec_lbl) {
        const char* st = voice_rec::statusText();   // REC.. / Uploading.. / Sent OK
        if (st[0]) lv_label_set_text(s_rec_lbl, st);
    }
}

void onButtonA() {
    voice_rec::start();
    if (s_rec_lbl) lv_label_set_text(s_rec_lbl, voice_rec::statusText());
}

}  // namespace app_papachat
