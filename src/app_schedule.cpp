#include "app_schedule.h"
#include "events_store.h"
#include "tz_helper.h"
#include <lvgl.h>
#include <Arduino.h>
#include <stdio.h>
#include <time.h>

LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);
LV_FONT_DECLARE(mont_light_32);
LV_FONT_DECLARE(phosphor_64);

namespace app_schedule {

// Stanley's wall-clock zone (matches face_lcd / face_mech / face_world).
// We format DTSTART in PT so the watch agrees with the original email.
static constexpr const char* TZ_STAN = "PST8PDT,M3.2.0,M11.1.0";

// Colours (matched to face_mech sepia palette so the app feels native).
static constexpr uint32_t COL_BG       = 0x110D08;     // near-black warm
static constexpr uint32_t COL_AMBER    = 0xE6A050;
static constexpr uint32_t COL_INK      = 0xF5E8D0;
static constexpr uint32_t COL_DIM      = 0x9C8264;
static constexpr uint32_t COL_LINE     = 0x3A2818;

static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_subtitle    = nullptr;
static lv_obj_t* s_list        = nullptr;       // scrollable container
static lv_obj_t* s_empty       = nullptr;       // empty-state group
static uint32_t  s_last_render_signature = 0;

// Wire one event into a card-shaped lv_obj. Caller appends it to s_list.
static void buildEventCard(lv_obj_t* parent, const events_store::Event& e) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(96));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_pad_top(card, 6, 0);
    lv_obj_set_style_pad_bottom(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_LINE), 0);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    // Format date / time using tz_helper (cached PT offset, no setenv leak).
    char date_line[48] = "—";
    char time_line[48] = "";
    if (e.starts_at) {
        struct tm tm;
        time_t epoch = (time_t)e.starts_at;
        tz_helper::localtime_in(TZ_STAN, epoch, &tm);
        static const char* DAYS[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* MONS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                     "Jul","Aug","Sep","Oct","Nov","Dec"};
        snprintf(date_line, sizeof(date_line), "%s, %s %d",
                 DAYS[tm.tm_wday & 7], MONS[tm.tm_mon & 15], tm.tm_mday);
        int h12 = tm.tm_hour % 12; if (h12 == 0) h12 = 12;
        snprintf(time_line, sizeof(time_line), "%d:%02d %s",
                 h12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    }

    // Date line — small amber tag at top of card.
    lv_obj_t* date_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(date_lbl, &mont_light_14, 0);
    lv_obj_set_style_text_color(date_lbl, lv_color_hex(COL_AMBER), 0);
    lv_label_set_text(date_lbl, date_line);
    lv_obj_align(date_lbl, LV_ALIGN_TOP_LEFT, 4, 2);

    // Title — main line.
    lv_obj_t* title_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(title_lbl, &mont_light_20, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(COL_INK), 0);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_lbl, lv_pct(94));
    lv_label_set_text(title_lbl, e.title[0] ? e.title : "(untitled)");
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 4, 22);

    // Time + location.
    char meta[80];
    if (time_line[0] && e.location[0]) {
        snprintf(meta, sizeof(meta), "%s  @  %s", time_line, e.location);
    } else if (time_line[0]) {
        snprintf(meta, sizeof(meta), "%s", time_line);
    } else if (e.location[0]) {
        snprintf(meta, sizeof(meta), "@ %s", e.location);
    } else {
        meta[0] = '\0';
    }
    if (meta[0]) {
        lv_obj_t* meta_lbl = lv_label_create(card);
        lv_obj_set_style_text_font(meta_lbl, &mont_light_14, 0);
        lv_obj_set_style_text_color(meta_lbl, lv_color_hex(COL_DIM), 0);
        lv_label_set_long_mode(meta_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(meta_lbl, lv_pct(94));
        lv_label_set_text(meta_lbl, meta);
        lv_obj_align(meta_lbl, LV_ALIGN_TOP_LEFT, 4, 48);
    }

    // Source attribution. (em-dash isn't in the current font subset → tofu;
    // use ASCII "from" prefix instead, plus an italic-feeling lowercase tone.)
    if (e.source[0]) {
        char src_buf[40];
        snprintf(src_buf, sizeof(src_buf), "from %s", e.source);
        lv_obj_t* src_lbl = lv_label_create(card);
        lv_obj_set_style_text_font(src_lbl, &mont_light_14, 0);
        lv_obj_set_style_text_color(src_lbl, lv_color_hex(COL_DIM), 0);
        lv_label_set_long_mode(src_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(src_lbl, lv_pct(94));
        lv_label_set_text(src_lbl, src_buf);
        lv_obj_align(src_lbl, LV_ALIGN_TOP_LEFT, 4, meta[0] ? 66 : 48);
    }

    // Manually size based on rough text height. mont_light_14 ≈ 18 px,
    // mont_light_20 ≈ 24. Date (22) + title (24) + meta (18) + src (18)
    // + 12 pad = ~94 px. Keep simple; LV_SIZE_CONTENT often misses on
    // absolutely-positioned children.
    int h = 28;                            // date band
    h += 26;                               // title
    if (meta[0])   h += 20;
    if (e.source[0]) h += 20;
    h += 12;                               // bottom pad
    lv_obj_set_height(card, h);
}

static void renderList() {
    if (!s_list) return;

    int n = events_store::upcomingCount();
    uint32_t sig = (uint32_t)n ^ (events_store::lastUpdateMillis() & 0xffff0000);
    if (sig == s_last_render_signature) return;
    s_last_render_signature = sig;

    lv_obj_clean(s_list);

    if (n == 0) {
        if (!s_empty) {
            s_empty = lv_obj_create(s_list);
            lv_obj_set_size(s_empty, lv_pct(100), lv_pct(100));
            lv_obj_set_style_bg_opa(s_empty, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(s_empty, 0, 0);
            lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* icon = lv_label_create(s_empty);
            lv_obj_set_style_text_font(icon, &phosphor_64, 0);
            lv_obj_set_style_text_color(icon, lv_color_hex(COL_LINE), 0);
            // calendar-blank glyph (matches the launcher icon).
            lv_label_set_text(icon, "\xEE\x84\x8A");
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, -28);

            lv_obj_t* hint = lv_label_create(s_empty);
            lv_obj_set_style_text_font(hint, &mont_light_20, 0);
            lv_obj_set_style_text_color(hint, lv_color_hex(COL_DIM), 0);
            lv_label_set_text(hint, "No events yet");
            lv_obj_align(hint, LV_ALIGN_CENTER, 0, 32);
        }
        return;
    }
    s_empty = nullptr;

    for (int i = 0; i < n; i++) {
        const events_store::Event* e = events_store::upcomingAt(i);
        if (!e) break;
        buildEventCard(s_list, *e);
    }
}

static void renderSubtitle() {
    if (!s_subtitle) return;
    int n = events_store::upcomingCount();
    char buf[40];
    if (n == 0) {
        snprintf(buf, sizeof(buf), "waiting for events");
    } else {
        snprintf(buf, sizeof(buf), "%d upcoming", n);
    }
    lv_label_set_text(s_subtitle, buf);
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // Title.
    lv_obj_t* title = lv_label_create(s_root);
    lv_obj_set_style_text_font(title, &mont_light_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_AMBER), 0);
    lv_label_set_text(title, "SCHEDULE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    // Subtitle (live count).
    s_subtitle = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_subtitle, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_subtitle, lv_color_hex(COL_DIM), 0);
    lv_obj_align(s_subtitle, LV_ALIGN_TOP_MID, 0, 60);

    // Scrollable list region.
    s_list = lv_obj_create(s_root);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_size(s_list, 410, 340);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);

    s_last_render_signature = 0;
    renderSubtitle();
    renderList();
}

void leave() {
    if (s_root) lv_obj_clean(s_root);
    s_root = s_subtitle = s_list = s_empty = nullptr;
    s_last_render_signature = 0;
}

void tick() {
    // Cheap when there's nothing new (renderList early-exits on signature match).
    events_store::prune();
    renderSubtitle();
    renderList();
}

}  // namespace app_schedule
