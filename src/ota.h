// ota — pull-based firmware updates from GitHub Releases.
//
// Strategy: at boot (and on MQTT trigger), the watch hits a GitHub-Releases-style
// manifest endpoint, compares its compile-time FIRMWARE_VERSION against the tag
// in the manifest, and if newer, downloads the firmware.bin asset and applies
// it via the ESP32 Update library. Reboots into the new firmware automatically.
//
// Manifest URL is GitHub-API-compatible:
//   https://api.github.com/repos/<owner>/<repo>/releases/latest
// Expected JSON shape:
//   { "tag_name": "v0.4.1", "assets": [ { "name": "firmware.bin",
//     "browser_download_url": "https://..." } ] }
//
// For local LAN testing, point at a Python mock server that returns the same
// JSON shape — see scripts/mock_release_server.py.

#pragma once
#include <stdint.h>

namespace ota {

extern const char* FIRMWARE_VERSION;       // compile-time, e.g. "0.4.0"

// Returns true if an update was downloaded successfully (board will reboot
// before returning). Returns false otherwise (up to date, no WiFi, or error).
bool checkAndUpdate(const char* manifest_url, bool force = false);

}  // namespace ota
