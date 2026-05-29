#include "ntp_sync.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>

namespace ntp_sync {

// ── status overlay (direct to display, no canvas) ────────
static void showStatus(const char* line1, const char* line2, uint16_t col) {
    auto& d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.drawCircle(234, 234, 232, 0x2104);
    d.setTextDatum(middle_center);
    d.setTextColor(col, TFT_BLACK);
    d.setFont(&fonts::FreeSans18pt7b);
    d.drawString(line1, 234, 234 - (line2 ? 18 : 0));
    if (line2) {
        d.setFont(&fonts::FreeSans12pt7b);
        d.setTextColor(0x8410, TFT_BLACK);
        d.drawString(line2, 234, 234 + 22);
    }
}

// ── load battery-backed RTC → system clock ───────────────
bool loadFromRtc() {
    if (!M5.Rtc.isEnabled()) return false;
    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year < 2024 || dt.date.year > 2099) {
        Serial.printf("[rtc] chip year=%d, not yet set\n", dt.date.year);
        return false;
    }
    struct tm t = {};
    t.tm_year = dt.date.year - 1900;
    t.tm_mon  = dt.date.month - 1;
    t.tm_mday = dt.date.date;
    t.tm_hour = dt.time.hours;
    t.tm_min  = dt.time.minutes;
    t.tm_sec  = dt.time.seconds;
    setenv("TZ", "UTC0", 1); tzset();
    time_t utc_t = mktime(&t);
    struct timeval tv = { utc_t, 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("[rtc] loaded: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  dt.date.year, dt.date.month, dt.date.date,
                  dt.time.hours, dt.time.minutes, dt.time.seconds);
    return true;
}

void writeToRtc() {
    if (!M5.Rtc.isEnabled()) return;
    time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    m5::rtc_datetime_t dt;
    dt.date.year    = utc.tm_year + 1900;
    dt.date.month   = utc.tm_mon + 1;
    dt.date.date    = utc.tm_mday;
    dt.date.weekDay = utc.tm_wday;
    dt.time.hours   = utc.tm_hour;
    dt.time.minutes = utc.tm_min;
    dt.time.seconds = utc.tm_sec;
    M5.Rtc.setDateTime(dt);
    Serial.printf("[rtc] wrote: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  dt.date.year, dt.date.month, dt.date.date,
                  dt.time.hours, dt.time.minutes, dt.time.seconds);
}

bool syncTime(uint32_t timeout_ms) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ntp] skip: WiFi not connected");
        return false;
    }

    // (suppressed status overlay — keeps the boot screen clean)
    // UTC base; configTime(gmt_offset, dst_offset, servers...)
    // China-mainland-friendly servers first, then global pool as fallback.
    configTime(0, 0,
               "ntp.aliyun.com",
               "cn.pool.ntp.org",
               "pool.ntp.org");

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        time_t now = time(nullptr);
        if (now > 1700000000) {           // any time after Nov 2023 = sane
            writeToRtc();
            struct tm utc;
            gmtime_r(&now, &utc);
            Serial.printf("[ntp] synced UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                          utc.tm_year+1900, utc.tm_mon+1, utc.tm_mday,
                          utc.tm_hour, utc.tm_min, utc.tm_sec);
            return true;
        }
        delay(100);
    }
    Serial.println("[ntp] timeout");
    return false;
}

}  // namespace ntp_sync
