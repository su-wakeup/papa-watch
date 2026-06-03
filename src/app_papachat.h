// app_papachat — the "PAPA" launcher app. Shows the latest daily note PAPA
// pushed from the bot (papa_quote). Replaces the old stub.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace app_papachat {

void enter(lv_obj_t* parent);
void leave();
void tick();

}  // namespace app_papachat
