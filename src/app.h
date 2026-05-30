// app — top-level OS contract. Each "app" (Watch, Stopwatch, Settings, …) is
// a namespace that implements the four hooks below and lives behind a single
// entry in g_apps[]. The launcher and main.cpp talk to apps only through this
// table — they never include app_xxx.h directly. New apps slot in by appending
// to the registry.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

struct App {
    const char* name;          // display label, e.g. "WATCH"
    const char* icon_utf8;     // single Phosphor codepoint as UTF-8
    void (*enter)(lv_obj_t* parent);
    void (*leave)();
    void (*tick)();            // called once per main loop iteration while active
};

extern const App* const g_apps[];
extern const int        g_apps_count;

namespace app_runtime {
    // Switch from current screen to the given app. Calls current->leave(),
    // wipes the screen, calls app->enter(active_screen).
    void switch_to(const App* app);

    // Convenience: switch_to(launcher).
    void back_to_launcher();

    // Forward tick to whichever app is currently active.
    void tick();

    const App* current();
}
