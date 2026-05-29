# papa-watch

A father-son dual-timezone watch built on the M5Stack StopWatch (ESP32-S3, 1.75" round AMOLED).

Stanley wears it in California. Dad lives in Beijing. Long-press the screen, a heart
goes over MQTT to dad's phone. Dad sends one back, watch buzzes lub-dub.

## Hardware

[M5Stack StopWatch (C152)](https://docs.m5stack.com/en/core/StopWatch) — ESP32-S3R8,
16 MB flash, 8 MB OPI PSRAM, 466×466 AMOLED, BMI270 IMU, RX8130CE RTC, ES8311 codec,
CST820B touch, 450 mAh battery, magnetic mount.

## Build

```bash
pio run -e m5stopwatch              # compile
pio run -e m5stopwatch -t upload    # flash via USB-C
pio device monitor                  # serial
```

Requires PlatformIO. First boot brings up the on-watch Wi-Fi config (mini QWERTY
keyboard with dual-button fallback). Credentials persist in NVS.

## Releases (OTA)

Pushing a tag `vX.Y.Z` triggers `.github/workflows/release.yml`, which builds the
firmware and attaches `firmware.bin` to the GitHub Release. The watch periodically
checks `https://api.github.com/repos/<owner>/papa-watch/releases/latest` and
self-updates when a newer semver appears.

## Project structure

```
src/
├── main.cpp              state machine + main loop
├── wifi_setup.{h,cpp}    SSID scan + mini QWERTY keyboard
├── ntp_sync.{h,cpp}      NTP + RX8130CE RTC bridge
├── heart_relay.{h,cpp}   MQTT bridge to dad
├── ota.{h,cpp}           pull-based firmware updates
├── lvgl_port.{h,cpp}     LVGL → M5GFX display + touch glue
├── face_lcd.{h,cpp}      Casio-style backlit-LCD watch face
└── fonts/                generated DSEG7 / DSEG14 LVGL fonts
include/
└── lv_conf.h             LVGL configuration
assets/dseg/              source TTFs (Keshikan DSEG, CC license)
scripts/
└── mock_release_server.py  local LAN OTA test stand-in for GitHub
```

## License

The watch firmware is MIT.
DSEG fonts in `assets/dseg/` are CC-BY-SA — see `assets/dseg/.../DSEG-LICENSE.txt`.
