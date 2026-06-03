// status_bar — WiFi + MQTT + unread indicator. Only used by the launcher.
// Watch faces integrate the same info into their own face-specific style
// (LCD's retro phosphor dots, Mech's brass amber pair). Out-of-app screens
// like Stopwatch / Alarm / Sundial / World Clock intentionally show nothing
// here — those are full-focus contexts and the system info would visually
// "sticker over" the carefully composed face.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace status_bar {

void create(lv_obj_t* parent);
void destroy();
void update(bool wifi_connected, bool mqtt_connected, int unread_count);
void setBattery(int pct, bool charging);     // percentage only on the face
int  battPctFromMv(int mv);                  // Li-ion OCV→SoC (beats M5's linear map)

}  // namespace status_bar
