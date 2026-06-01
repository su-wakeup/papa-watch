#include "events_store.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace events_store {

static Event    s_events[MAX_EVENTS];
static int      s_count             = 0;
static uint32_t s_last_update_ms    = 0;

static constexpr const char* NVS_NS  = "events";
static constexpr const char* NVS_KEY = "blob";        // binary blob

// Discard events whose start time is more than a day before now. Events
// without a known starts_at (== 0) stick around until evicted by capacity.
static bool isStale(const Event& e, time_t now) {
    if (!e.id) return false;                          // empty slot
    if (e.starts_at == 0) return false;
    return e.starts_at < (now - 86400);
}

static void sortByStartsAsc() {
    // Insertion sort — N=16 makes this O(N^2) = 256 cmp worst case, trivial.
    // Empty slots (id==0) sort last via the sentinel starts_at = INT64_MAX.
    for (int i = 1; i < MAX_EVENTS; i++) {
        Event key = s_events[i];
        int64_t kStart = key.id ? key.starts_at : INT64_MAX;
        int j = i - 1;
        while (j >= 0) {
            int64_t aStart = s_events[j].id ? s_events[j].starts_at : INT64_MAX;
            if (aStart <= kStart) break;
            s_events[j + 1] = s_events[j];
            j--;
        }
        s_events[j + 1] = key;
    }
    s_count = 0;
    for (int i = 0; i < MAX_EVENTS; i++) if (s_events[i].id) s_count++;
}

static void save() {
    Preferences nvs;
    if (!nvs.begin(NVS_NS, false)) return;
    nvs.putBytes(NVS_KEY, s_events, sizeof(s_events));
    nvs.end();
}

static void load() {
    Preferences nvs;
    if (!nvs.begin(NVS_NS, true)) {
        memset(s_events, 0, sizeof(s_events));
        return;
    }
    size_t got = nvs.getBytes(NVS_KEY, s_events, sizeof(s_events));
    nvs.end();
    if (got != sizeof(s_events)) {
        memset(s_events, 0, sizeof(s_events));
    }
    sortByStartsAsc();
}

void begin() {
    load();
    Serial.printf("[events] loaded %d cached event(s)\n", s_count);
}

// Find slot by event id, or pick the slot to evict (oldest expired first;
// failing that, slot with smallest id assuming id grows over time).
static int slotFor(uint32_t id) {
    int empty = -1, oldest = 0;
    int64_t oldestStart = INT64_MAX;
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (s_events[i].id == id) return i;
        if (s_events[i].id == 0 && empty < 0) empty = i;
        int64_t start = s_events[i].starts_at;
        if (s_events[i].id && start < oldestStart) {
            oldestStart = start;
            oldest = i;
        }
    }
    return empty >= 0 ? empty : oldest;
}

static void copyTruncated(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void onMqttEvent(const char* topic, const uint8_t* payload, unsigned len) {
    // Empty payload on a retained event topic = tombstone: events/<pair>/<id>
    // was cleared (worker delete or manual), so drop that id locally too. The
    // id isn't in the absent body — recover it from the topic's last segment.
    if (len == 0) {
        const char* slash = strrchr(topic, '/');
        uint32_t id = slash ? (uint32_t)strtoul(slash + 1, nullptr, 10) : 0;
        if (!id) return;
        for (int i = 0; i < MAX_EVENTS; i++) {
            if (s_events[i].id == id) {
                memset(&s_events[i], 0, sizeof(Event));
                sortByStartsAsc();
                save();
                s_last_update_ms = millis();
                Serial.printf("[events] -id=%u (tombstone, total %d)\n", id, s_count);
                return;
            }
        }
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.printf("[events] JSON parse fail: %s (topic=%s)\n",
                      err.c_str(), topic);
        return;
    }

    uint32_t id = doc["id"] | 0;
    if (!id) {
        Serial.printf("[events] no id in payload, topic=%s\n", topic);
        return;
    }

    int slot = slotFor(id);
    Event& e = s_events[slot];
    e.id         = id;
    e.starts_at  = doc["starts_at"] | (int64_t)0;
    e.ends_at    = doc["ends_at"]   | (int64_t)0;
    e.confidence = doc["confidence"] | 0.0f;
    copyTruncated(e.title,    sizeof(e.title),    doc["title"]    | "");
    copyTruncated(e.kind,     sizeof(e.kind),     doc["kind"]     | "");
    copyTruncated(e.location, sizeof(e.location), doc["location"] | "");
    copyTruncated(e.source,   sizeof(e.source),   doc["source"]   | "");

    sortByStartsAsc();
    save();
    s_last_update_ms = millis();

    Serial.printf("[events] +id=%u \"%s\" at %lld (slot %d, total %d)\n",
                  id, e.title, (long long)e.starts_at, slot, s_count);
}

void prune() {
    time_t now = time(nullptr);
    bool dirty = false;
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (isStale(s_events[i], now)) {
            memset(&s_events[i], 0, sizeof(Event));
            dirty = true;
        }
    }
    if (dirty) {
        sortByStartsAsc();
        save();
    }
}

int          upcomingCount()      { return s_count; }
uint32_t     lastUpdateMillis()   { return s_last_update_ms; }

const Event* upcomingAt(int idx) {
    if (idx < 0 || idx >= s_count) return nullptr;
    return &s_events[idx];
}

}  // namespace events_store
