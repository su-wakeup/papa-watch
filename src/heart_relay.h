// heart_relay — MQTT bridge for cross-timezone heartbeat between Stanley and Dad.
//   topics:   stopwatch/<pair_id>/from-son  (publish, Stanley → Dad)
//             stopwatch/<pair_id>/from-dad  (subscribe, Dad → Stanley)
//   payload:  {"t": <unix-seconds>}  (timestamp, used for dedupe)
//   broker:   broker.emqx.io:1883  (public, China-friendly)
//
// Call begin() after WiFi is up. Call tick() each loop iteration. Provide a
// receive callback via setOnReceive(); it fires once per incoming heart.

#pragma once
#include <Arduino.h>

namespace heart_relay {

void begin(const char* pair_id);
void tick();
bool sendHeart();
bool isConnected();

// Callback signature: payload is NOT null-terminated; use `length`.
// Use the payload to distinguish hearts from /commands (e.g. {"cmd":"ota"}).
typedef void (*MessageCb)(const char* payload, unsigned int length);
void setOnReceive(MessageCb cb);

}  // namespace heart_relay
