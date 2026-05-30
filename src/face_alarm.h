// face_alarm — Watch sub-tile #3. iPhone-style HH:MM rollers, master ON/OFF
// switch, a TEST button to preview the alarm sound, and a preview line.
// The actual scheduling/firing lives in src/alarm.{h,cpp}; this file only
// edits the persisted config and rebinds when the user navigates back.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace face_alarm {

void create(lv_obj_t* parent);
void update();
void destroy();

}  // namespace face_alarm
