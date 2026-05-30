// app_stub — placeholder used by every app whose UI isn't built yet.
// Shows the Phosphor icon at the top and the app name underneath, so when
// the user taps an icon in the launcher they at least land somewhere named.
// As each real app gets built, swap its entry in g_apps[] from these stubs
// to the real namespace functions.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace app_stub {

// Generic enter that draws "icon + name" centered. Caller passes the
// same icon_utf8 and name strings that live in the App struct.
void enter(lv_obj_t* parent, const char* icon_utf8, const char* name);
void leave();
void tick();

}  // namespace app_stub
