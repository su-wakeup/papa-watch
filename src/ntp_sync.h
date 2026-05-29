// ntp_sync — NTP time fetch + RX8130CE hardware RTC bridge.
//   loadFromRtc()    pulls battery-backed hardware RTC → system UTC time
//   syncTime()       fetches UTC from NTP, writes back to hardware RTC
//   writeToRtc()     manual write (called internally on successful sync)
// All time stored as UTC; display-side TZ conversion lives in main.cpp.

#pragma once
#include <Arduino.h>

namespace ntp_sync {

bool loadFromRtc();
bool syncTime(uint32_t timeout_ms = 8000);
void writeToRtc();

}  // namespace ntp_sync
