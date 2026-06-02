// app_launcher — home wheel of app icons.
//
// Three icons are visible at once: left and right small/dim, center big/amber.
// BtnA / BtnB or a swipe kicks off a 240 ms animation: the leaving icon slides
// off + fades, the previous side icon slides into the center AND scales up, the
// old center slides to the side AND scales down, and a freshly-spawned icon
// glides in from the opposite edge.
//
// Icons are A8 lv_image masks (see src/icons/), scaled with lv_image_set_scale
// and recoloured per slot (amber center, dim sides). Image scaling renders in
// the blit — it does NOT allocate a per-object layer buffer the way
// transform_scale on a big font glyph did (that ~144 KB alloc is what hung the
// v0.9.12 build on boot). So the size can interpolate smoothly without OOM.
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
static constexpr int32_t  SCALE_SIDE    = 128;   // 256 = 1.0x, so 128 = 0.5x
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

static const lv_image_dsc_t* icon_of(const App* a) {
    return (const lv_image_dsc_t*)a->icon_img;
}

// ─── icon construction ─────────────────────────────────────────────────────

// An lv_image scaled from its own center pivot, positioned by translate_x.
// The object box is always the native image size, so scaling (a render-time
// transform around the pivot) keeps the icon centered on its slot — no drift.
static lv_obj_t* make_icon(int x, int32_t scale, uint32_t color,
                           const lv_image_dsc_t* dsc) {
    lv_obj_t* img = lv_image_create(s_root);
    lv_image_set_src(img, dsc);
    lv_image_set_pivot(img, dsc->header.w / 2, dsc->header.h / 2);
    lv_image_set_scale(img, scale);
    lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, Y_OFFSET);
    lv_obj_set_style_translate_x(img, x, 0);
    return img;
}

static void set_icon_color(lv_obj_t* img, uint32_t color) {
    lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
}

// ─── LVGL animation exec wrappers ──────────────────────────────────────────

static void exec_x(void* obj, int32_t v) {
    lv_obj_set_style_translate_x((lv_obj_t*)obj, (int)v, 0);
}
static void exec_scale(void* obj, int32_t v) {
    lv_image_set_scale((lv_obj_t*)obj, (uint32_t)v);
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
                                   icon_of(app_at_offset(offset_for_incoming)));
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

    // Outgoing center — slide to side slot, shrink.
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

    // Recolour the two role-changers. Snapped at anim start — reads cleanly
    // against the simultaneous slide+scale.
    set_icon_color(outgoing_center, COL_DIM);
    set_icon_color(incoming_center, COL_AMBER);

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
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if      (dir == LV_DIR_LEFT)  rotate(+1);
    else if (dir == LV_DIR_RIGHT) rotate(-1);
}

// ─── public lifecycle ──────────────────────────────────────────────────────

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // The off-screen icons overflow the viewport, which would make the root
    // scrollable — then a swipe gets eaten as an elastic scroll (rubber-banding
    // the whole screen) and LV_EVENT_GESTURE never fires. Pin it down.
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // Whole-screen gesture sink for swipe navigation.
    lv_obj_add_event_cb(s_root, on_screen_gesture, LV_EVENT_GESTURE, nullptr);

    // Three icons — left & right small/dim, center big/amber.
    s_icon_left = make_icon(POS_LEFT_X, SCALE_SIDE, COL_DIM,
                            icon_of(app_at_offset(-1)));
    lv_obj_add_flag(s_icon_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_left, on_icon_click, LV_EVENT_CLICKED, nullptr);

    s_icon_right = make_icon(POS_RIGHT_X, SCALE_SIDE, COL_DIM,
                             icon_of(app_at_offset(+1)));
    lv_obj_add_flag(s_icon_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_right, on_icon_click, LV_EVENT_CLICKED, nullptr);

    s_icon_center = make_icon(POS_CENTER_X, SCALE_CENTER, COL_AMBER,
                              icon_of(app_at_offset(0)));
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
