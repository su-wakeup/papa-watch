// app_launcher — home wheel of app icons.
//
// Three icons are visible at once: left and right at half-scale, center at
// full scale. Pressing BtnA / BtnB or swiping left/right kicks off a 240 ms
// LVGL animation: the leaving icon slides off + fades, the previous side
// icon scales up to become the new center, the old center scales down to
// become the new side, and a freshly-spawned icon glides in from the
// opposite edge. All transforms are runtime LVGL transform_scale and
// transform_translate — no per-frame asset baking.
//
// Touch:
//   - swipe LEFT  → rotate to next (same as BtnB)
//   - swipe RIGHT → rotate to prev (same as BtnA)
//   - tap CENTER  → enter the centered app
//   - tap LEFT  / RIGHT icon → peek-jump to that app (rotates one step)

#include "app_launcher.h"
#include "app.h"
#include "status_bar.h"
#include <lvgl.h>
#include <stdint.h>

LV_FONT_DECLARE(phosphor_128);
LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_32);

namespace app_launcher {

// Wheel state — index into g_apps[1..g_apps_count-1].
static int s_sel = 1;
static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_icon_left   = nullptr;
static lv_obj_t* s_icon_center = nullptr;
static lv_obj_t* s_icon_right  = nullptr;
static lv_obj_t* s_name        = nullptr;
static lv_obj_t* s_hint_a      = nullptr;
static lv_obj_t* s_hint_b      = nullptr;

static bool        s_animating = false;
static lv_obj_t*   s_leaving   = nullptr;     // icon to delete on anim complete

static constexpr int      ANIM_MS       = 240;
static constexpr int32_t  SCALE_SIDE    = 128;   // LVGL scale: 256 = 1.0x
static constexpr int32_t  SCALE_CENTER  = 256;
static constexpr uint32_t COL_AMBER     = 0xE6A050;
static constexpr uint32_t COL_DIM       = 0x705840;
static constexpr int      POS_LEFT_X    = -160;
static constexpr int      POS_CENTER_X  = 0;
static constexpr int      POS_RIGHT_X   = 160;
static constexpr int      POS_OFFSCREEN = 320;
static constexpr int      Y_OFFSET      = -10;

static int wheel_count() { return g_apps_count - 1; }   // skip launcher slot

static const App* app_at_offset(int delta) {
    int n   = wheel_count();
    int rel = ((s_sel - 1) + delta) % n;
    if (rel < 0) rel += n;
    return g_apps[1 + rel];
}

// ─── icon construction ─────────────────────────────────────────────────────

static lv_obj_t* make_icon(int x_offset, int32_t scale,
                           uint32_t color, const char* glyph) {
    lv_obj_t* lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(lbl, &phosphor_128, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    // Pivot at object's own center so scale grows symmetrically rather than
    // anchoring at top-left.
    lv_obj_set_style_transform_pivot_x(lbl, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(lbl, LV_PCT(50), 0);
    lv_obj_set_style_transform_scale(lbl, scale, 0);
    lv_label_set_text(lbl, glyph);
    lv_obj_align(lbl, LV_ALIGN_CENTER, x_offset, Y_OFFSET);
    return lbl;
}

// ─── LVGL animation exec wrappers ──────────────────────────────────────────

// Re-align rather than lv_obj_set_x so the LV_ALIGN_CENTER reference holds.
static void exec_x(void* obj, int32_t v) {
    lv_obj_align((lv_obj_t*)obj, LV_ALIGN_CENTER, (int)v, Y_OFFSET);
}
static void exec_scale(void* obj, int32_t v) {
    lv_obj_set_style_transform_scale((lv_obj_t*)obj, v, 0);
}
static void exec_opa(void* obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

static void anim_int(lv_obj_t* obj, lv_anim_exec_xcb_t exec,
                     int32_t from, int32_t to) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_start(&a);
}

static void on_rotate_complete(lv_anim_t*) {
    if (s_leaving) {
        lv_obj_delete(s_leaving);
        s_leaving = nullptr;
    }
    s_animating = false;
}

// ─── click handler (single, role-determined by current s_icon_* pointers) ──

static void rotate(int delta);              // forward

static void on_icon_click(lv_event_t* e) {
    if (s_animating) return;
    lv_obj_t* hit = (lv_obj_t*)lv_event_get_target(e);
    if      (hit == s_icon_center) app_runtime::switch_to(app_at_offset(0));
    else if (hit == s_icon_left)   rotate(-1);
    else if (hit == s_icon_right)  rotate(+1);
    // else: stale incoming icon during anim — ignored above by s_animating
}

// ─── core rotation with animation ──────────────────────────────────────────

static void rotate(int delta) {
    if (s_animating) return;
    s_animating = true;

    const int n = wheel_count();
    const int new_sel = ((s_sel - 1 + delta + n) % n) + 1;

    // Spawn the incoming icon off-screen on the far side and tag the
    // soon-to-leave icon. Then animate all four in unison.
    const int offset_for_incoming = (delta > 0) ?  2 : -2;
    const int incoming_start_x    = (delta > 0) ?  POS_OFFSCREEN : -POS_OFFSCREEN;
    const int incoming_end_x      = (delta > 0) ?  POS_RIGHT_X   :  POS_LEFT_X;

    lv_obj_t* incoming = make_icon(incoming_start_x, SCALE_SIDE, COL_DIM,
                                   app_at_offset(offset_for_incoming)->icon_utf8);
    lv_obj_set_style_opa(incoming, 0, 0);
    lv_obj_add_flag(incoming, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(incoming, on_icon_click, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* leaving;
    lv_obj_t* outgoing_center;          // currently center → becomes side
    lv_obj_t* incoming_center;          // currently side  → becomes center
    int       outgoing_center_end_x;
    int       incoming_center_start_x;
    int       leaving_offscreen_x;

    if (delta > 0) {
        leaving                  = s_icon_left;
        outgoing_center          = s_icon_center;
        incoming_center          = s_icon_right;
        outgoing_center_end_x    = POS_LEFT_X;
        incoming_center_start_x  = POS_RIGHT_X;
        leaving_offscreen_x      = -POS_OFFSCREEN;
    } else {
        leaving                  = s_icon_right;
        outgoing_center          = s_icon_center;
        incoming_center          = s_icon_left;
        outgoing_center_end_x    = POS_RIGHT_X;
        incoming_center_start_x  = POS_LEFT_X;
        leaving_offscreen_x      =  POS_OFFSCREEN;
    }

    // Leaving — slide off, fade out.
    anim_int(leaving, exec_x,
             (delta > 0) ? POS_LEFT_X : POS_RIGHT_X,
             leaving_offscreen_x);
    anim_int(leaving, exec_opa, 255, 0);

    // Outgoing center — slide to side, shrink.
    anim_int(outgoing_center, exec_x, POS_CENTER_X, outgoing_center_end_x);
    anim_int(outgoing_center, exec_scale, SCALE_CENTER, SCALE_SIDE);

    // Incoming center — slide to center, grow.
    anim_int(incoming_center, exec_x, incoming_center_start_x, POS_CENTER_X);
    anim_int(incoming_center, exec_scale, SCALE_SIDE, SCALE_CENTER);

    // Incoming — slide from off-screen edge to its new side position.
    // Hook the completion cb here so it fires once when this last animation
    // ends (all anims share ANIM_MS).
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, incoming);
        lv_anim_set_values(&a, incoming_start_x, incoming_end_x);
        lv_anim_set_duration(&a, ANIM_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, exec_x);
        lv_anim_set_completed_cb(&a, on_rotate_complete);
        lv_anim_start(&a);
    }
    anim_int(incoming, exec_opa, 0, 255);

    // Re-colour center vs side at anim *start* so the colour matches the
    // role from the first frame. (A separate colour interpolation animation
    // would be nicer but the on/off swap at t=0 reads fine against motion.)
    lv_obj_set_style_text_color(outgoing_center, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_color(incoming_center, lv_color_hex(COL_AMBER), 0);

    // Reassign role pointers so subsequent rotations stack cleanly.
    if (delta > 0) {
        s_leaving      = leaving;            // queued for delete on complete
        s_icon_left    = outgoing_center;
        s_icon_center  = incoming_center;
        s_icon_right   = incoming;
    } else {
        s_leaving      = leaving;
        s_icon_right   = outgoing_center;
        s_icon_center  = incoming_center;
        s_icon_left    = incoming;
    }

    // Click handler is on_icon_click everywhere — it dispatches by which
    // of s_icon_left/center/right matches the hit object, so we don't need
    // to re-bind anything when role pointers change.

    // Update bottom name label and selection state.
    s_sel = new_sel;
    lv_label_set_text(s_name, app_at_offset(0)->name);
}

// ─── touch gesture (whole-screen swipe) ────────────────────────────────────

static void on_screen_gesture(lv_event_t* e) {
    if (s_animating) return;
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if      (dir == LV_DIR_LEFT)  rotate(+1);
    else if (dir == LV_DIR_RIGHT) rotate(-1);
}

// ─── public lifecycle ──────────────────────────────────────────────────────

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // Whole-screen gesture sink for swipe navigation.
    lv_obj_add_event_cb(s_root, on_screen_gesture, LV_EVENT_GESTURE, nullptr);

    // Three icons — left & right small/dim, center big/amber.
    s_icon_left = make_icon(POS_LEFT_X, SCALE_SIDE, COL_DIM,
                            app_at_offset(-1)->icon_utf8);
    lv_obj_add_flag(s_icon_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_left, on_icon_click, LV_EVENT_CLICKED, nullptr);

    s_icon_right = make_icon(POS_RIGHT_X, SCALE_SIDE, COL_DIM,
                             app_at_offset(+1)->icon_utf8);
    lv_obj_add_flag(s_icon_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_right, on_icon_click, LV_EVENT_CLICKED, nullptr);

    s_icon_center = make_icon(POS_CENTER_X, SCALE_CENTER, COL_AMBER,
                              app_at_offset(0)->icon_utf8);
    lv_obj_add_flag(s_icon_center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_center, on_icon_click, LV_EVENT_CLICKED, nullptr);

    // App name label.
    s_name = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_name, &mont_light_32, 0);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0xF5E8D0), 0);
    lv_obj_align(s_name, LV_ALIGN_CENTER, 0, 90);
    lv_label_set_text(s_name, app_at_offset(0)->name);

    // BtnA / BtnB hint labels (near physical buttons).
    s_hint_a = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint_a, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_hint_a, lv_color_hex(0x705840), 0);
    lv_label_set_text(s_hint_a, "< A");
    lv_obj_align(s_hint_a, LV_ALIGN_BOTTOM_LEFT, 36, -22);

    s_hint_b = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint_b, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_hint_b, lv_color_hex(0x705840), 0);
    lv_label_set_text(s_hint_b, "B >");
    lv_obj_align(s_hint_b, LV_ALIGN_BOTTOM_RIGHT, -36, -22);

    // Status pill is only on the home screen.
    status_bar::create(s_root);

    s_animating = false;
    s_leaving = nullptr;
}

void leave() {
    // Stop any in-flight anims before deleting their targets.
    if (s_icon_left)   lv_anim_delete(s_icon_left,   nullptr);
    if (s_icon_right)  lv_anim_delete(s_icon_right,  nullptr);
    if (s_icon_center) lv_anim_delete(s_icon_center, nullptr);
    if (s_leaving)     lv_anim_delete(s_leaving,     nullptr);

    if (s_root) {
        lv_obj_clean(s_root);
        s_root = nullptr;
    }
    s_icon_left = s_icon_center = s_icon_right = s_name = nullptr;
    s_hint_a = s_hint_b = nullptr;
    s_leaving = nullptr;
    s_animating = false;
    status_bar::destroy();
}

void tick() {}

void rotate_left()      { if (!s_animating) rotate(-1); }
void rotate_right()     { if (!s_animating) rotate(+1); }
void activate_center()  { if (!s_animating) app_runtime::switch_to(app_at_offset(0)); }

}  // namespace app_launcher
