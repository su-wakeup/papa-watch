// wifi_setup — first-boot Wi-Fi onboarding for StopWatch
//   1) tryAutoConnect()         attempt last-saved credentials (NVS)
//   2) runInteractiveConfig()   scan → SSID list → mini QWERTY (+ dual-btn fallback)
//                                → save NVS on success → return when connected
// Visual layer is placeholder-grade; polish is a separate UI/UX track (see memory).

#pragma once
#include <M5Unified.h>
#include <Arduino.h>

namespace wifi_setup {

bool   tryAutoConnect(uint32_t timeout_ms = 10000);   // returns true if connected
void   runInteractiveConfig();                         // blocks until connected
bool   isConnected();
String currentSSID();
void   forgetSaved();                                  // wipe NVS, force reconfig

}  // namespace wifi_setup
