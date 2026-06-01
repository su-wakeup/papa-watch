// app_schedule — vertical scrollable list of upcoming events the watch
// has heard about over MQTT (see events_store + heart_relay's events
// topic subscription). One card per event with date / title / time +
// location / source attribution. Empty state shows a hint while we wait
// for the first MQTT retained-replay.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace app_schedule {

void enter(lv_obj_t* parent);
void leave();
void tick();      // re-render if events_store advertised an update

}  // namespace app_schedule
