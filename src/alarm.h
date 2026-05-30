// alarm — single-alarm scheduler. State lives in NVS so it survives reboot;
// check() compares Stanley's local time against the configured HH:MM every
// second and triggers fire() exactly once per matching minute.

#pragma once
#include <stdint.h>

namespace alarms {

enum RepeatMode : uint8_t { REPEAT_ONCE = 0, REPEAT_DAILY = 1 };

struct Config {
    uint8_t    hour;        // 0-23 in Stanley PT
    uint8_t    minute;      // 0-59
    bool       enabled;
    RepeatMode mode;        // ONCE auto-disables after firing; DAILY stays armed
};

void load();            // pull persisted config from NVS, call once at boot
void save();            // push current config to NVS
Config& get();          // mutable reference — face_alarm edits this directly

void check();           // call once per second from main loop
void fire();            // play the alarm sound; public so a TEST button can invoke
bool isFiring();
void dismiss();

}  // namespace alarmss
