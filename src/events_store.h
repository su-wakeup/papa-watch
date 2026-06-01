// events_store — in-memory cache of calendar events the watch has heard
// about over MQTT, with NVS persistence so the schedule app has something
// to show before the broker reconnects.
//
// Events arrive on `events/<pair_id>/<event_id>` topics with retain=true,
// so a fresh subscription replays the full backlog. We dedupe by event.id
// (the worker's D1 events.id), keep up to MAX_EVENTS sorted ascending by
// starts_at, and drop entries that are over a day stale.
//
// The schedule app (#44) reads upcomingCount() / upcomingAt() to render.

#pragma once

#include <stdint.h>

namespace events_store {

constexpr int MAX_EVENTS = 16;

struct Event {
    uint32_t id;                  // 0 = empty slot
    int64_t  starts_at;           // unix epoch seconds; 0 if unknown
    int64_t  ends_at;             // 0 if open-ended
    char     title[48];
    char     kind[12];
    char     location[32];
    char     source[24];
    float    confidence;
};

void begin();                     // load any persisted events from NVS

// Dispatch from heart_relay's MQTT callback. `topic` looks like
// `events/<pair>/<id>`; we parse `payload` as JSON, upsert, and re-sort.
void onMqttEvent(const char* topic, const uint8_t* payload, unsigned len);

// Prune expired (starts_at older than 24h before now). Cheap, OK to call
// from the per-second tick.
void prune();

int          upcomingCount();     // count of non-empty slots after prune
const Event* upcomingAt(int idx); // sorted by starts_at ascending; nullptr OOB

// Updated when any new event arrives (regardless of whether it changed
// the list); used by app_schedule to refresh its "Updated X min ago" line
// and to skip redraws when nothing's new.
uint32_t lastUpdateMillis();

}  // namespace events_store
