// app_registry — defines g_apps[] and implements app_runtime. This is the
// only file that knows about every concrete app namespace; the rest of the
// codebase talks to apps through the App struct in app.h.

#include "app.h"
#include "app_launcher.h"
#include "app_stub.h"
#include "app_stopwatch.h"
#include "app_settings.h"
#include "app_sundial.h"
#include "app_schedule.h"
#include "app_papachat.h"
#include "face_manager.h"
#include "icons/icons.h"
#include <lvgl.h>

// ── per-app enter/leave/tick trampolines ─────────────────────────────────

static void launcher_enter(lv_obj_t* p)  { app_launcher::enter(p); }
static void launcher_leave()             { app_launcher::leave(); }
static void launcher_tick()              { app_launcher::tick(); }
static void launcher_btn_a()             { app_launcher::rotate_left(); }
static void launcher_btn_b()             { app_launcher::rotate_right(); }

static void watch_enter(lv_obj_t*)       { face_manager::create(); }
static void watch_leave()                { face_manager::destroy(); }
static void watch_tick()                 { face_manager::update(); }
static void watch_btn_a()                { face_manager::buttonA(); }   // spin globe on world tile
static void watch_btn_b()                { face_manager::buttonB(); }

static void stopwatch_enter(lv_obj_t* p) { app_stopwatch::enter(p); }
static void stopwatch_leave()             { app_stopwatch::leave(); }
static void stopwatch_tick()              { app_stopwatch::tick(); }
static void stopwatch_btn_a()             { app_stopwatch::press_run(); }
static void stopwatch_btn_b()             { app_stopwatch::press_aux(); }

static void settings_enter(lv_obj_t* p)  { app_settings::enter(p); }
static void settings_leave()              { app_settings::leave(); }
static void settings_tick()               { app_settings::tick(); }
static void settings_btn_a()              { app_settings::scroll_up(); }
static void settings_btn_b()              { app_settings::scroll_down(); }

static void compass_enter(lv_obj_t* p)   { app_sundial::enter(p); }
static void compass_leave()               { app_sundial::leave(); }
static void compass_tick()                { app_sundial::tick(); }

static void schedule_enter(lv_obj_t* p)  { app_schedule::enter(p); }
static void schedule_leave()              { app_schedule::leave(); }
static void schedule_tick()               { app_schedule::tick(); }

static void papachat_enter(lv_obj_t* p)  { app_papachat::enter(p); }
static void papachat_leave()              { app_papachat::leave(); }
static void papachat_tick()               { app_papachat::tick(); }
static void papachat_btn_a()              { app_papachat::onButtonA(); }   // record to PAPA

// Stub trampolines for apps whose UIs aren't built yet. Each just forwards
// to app_stub::enter with its own name + Phosphor glyph so a tap at least
// lands somewhere named. As each real app gets built, swap its trio of
// trampolines below for the real namespace's enter/leave/tick. Stubs
// have no A/B button handlers (just back via A+B).
#define STUB_APP(idname, icon_lit, label_lit) \
    static void idname##_enter(lv_obj_t* p) { app_stub::enter(p, icon_lit, label_lit); } \
    static void idname##_leave()             { app_stub::leave(); } \
    static void idname##_tick()              { app_stub::tick(); }

STUB_APP(aichat,    "\xEE\x9D\xA2", "AI CHAT")
// (papachat has a real implementation now — see papachat_enter/leave/tick above)
// (compass has a real implementation — see compass_enter/leave/tick above)
// (settings has a real implementation now — see settings_enter/leave/tick above)

// ── registry (UTF-8 codepoints are Phosphor regular) ─────────────────────
//   watch          U+E4E6   → "\xEE\x93\xA6"
//   timer          U+E492   → "\xEE\x92\x92"
//   calendar-blank U+E10A   → "\xEE\x84\x8A"
//   robot          U+E762   → "\xEE\x9D\xA2"
//   chat-circle-d  U+E16C   → "\xEE\x85\xAC"
//   compass        U+E1C8   → "\xEE\x87\x88"
//   gear-six       U+E272   → "\xEE\x89\xB2"
//   house          U+E2C2   → "\xEE\x8B\x82"  (launcher's own icon, unused by the grid)

static const App APP_LAUNCHER  = { "HOME",      "\xEE\x8B\x82", launcher_enter,  launcher_leave,  launcher_tick,  launcher_btn_a,  launcher_btn_b };
static const App APP_WATCH     = { "WATCH",     "\xEE\x93\xA6", watch_enter,     watch_leave,     watch_tick,     watch_btn_a,     watch_btn_b,     &icon_watch,     &icon_watch_mono     };
static const App APP_STOPWATCH = { "STOPWATCH", "\xEE\x92\x92", stopwatch_enter, stopwatch_leave, stopwatch_tick, stopwatch_btn_a, stopwatch_btn_b, &icon_stopwatch, &icon_stopwatch_mono };
static const App APP_SCHEDULE  = { "SCHEDULE",  "\xEE\x84\x8A", schedule_enter,  schedule_leave,  schedule_tick,  nullptr,         nullptr,         &icon_schedule,  &icon_schedule_mono  };
static const App APP_AICHAT    = { "AI CHAT",   "\xEE\x9D\xA2", aichat_enter,    aichat_leave,    aichat_tick,    nullptr,         nullptr,         &icon_aichat,    &icon_aichat_mono    };
static const App APP_PAPACHAT  = { "PAPA",      "\xEE\x85\xAC", papachat_enter,  papachat_leave,  papachat_tick,  papachat_btn_a,  nullptr,         &icon_papa,      &icon_papa_mono      };
static const App APP_COMPASS   = { "SUNDIAL",   "\xEE\x87\x88", compass_enter,   compass_leave,   compass_tick,   nullptr,         nullptr,         &icon_compass,   &icon_compass_mono   };  // no A/B; touch only
static const App APP_SETTINGS  = { "SETTINGS",  "\xEE\x89\xB2", settings_enter,  settings_leave,  settings_tick,  settings_btn_a,  settings_btn_b,  &icon_settings,  &icon_settings_mono  };

const App* const g_apps[] = {
    &APP_LAUNCHER,   // [0] reserved — home grid, skipped by launcher iteration
    &APP_WATCH,
    &APP_STOPWATCH,
    &APP_SCHEDULE,
    &APP_AICHAT,
    &APP_PAPACHAT,
    &APP_COMPASS,
    &APP_SETTINGS,
};
const int g_apps_count = sizeof(g_apps) / sizeof(g_apps[0]);

// ── runtime ──────────────────────────────────────────────────────────────

namespace app_runtime {

static const App* s_current = nullptr;

void switch_to(const App* app) {
    if (!app || app == s_current) return;
    if (s_current && s_current->leave) s_current->leave();
    if (app->enter) app->enter(lv_screen_active());
    s_current = app;
}

void back_to_launcher() { switch_to(g_apps[0]); }

void tick() {
    if (s_current && s_current->tick) s_current->tick();
}

const App* current() { return s_current; }

}  // namespace app_runtime
