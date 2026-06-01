#include "ota.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

namespace ota {

const char* FIRMWARE_VERSION = "0.9.13";

// strip leading 'v' if present and parse semantic version into 3 ints
static void parseSemver(const char* s, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (!s) return;
    if (*s == 'v' || *s == 'V') s++;
    sscanf(s, "%d.%d.%d", &out[0], &out[1], &out[2]);
}

// returns >0 if a > b, <0 if a < b, 0 if equal
static int semverCmp(const char* a, const char* b) {
    int A[3], B[3];
    parseSemver(a, A);
    parseSemver(b, B);
    for (int i = 0; i < 3; i++) {
        if (A[i] != B[i]) return A[i] - B[i];
    }
    return 0;
}

static bool fetchManifest(const char* url, String& out_tag, String& out_asset_url) {
    HTTPClient http;
    bool ok = false;

    // TLS receive buffer sized for full GitHub Releases JSON (~3-8 KB)
    WiFiClientSecure client;
    if (strncmp(url, "https://", 8) == 0) {
        client.setInsecure();
        ok = http.begin(client, url);
    } else {
        ok = http.begin(url);
    }
    if (!ok) {
        Serial.printf("[ota] http.begin failed for %s\n", url);
        return false;
    }
    http.setTimeout(15000);
    http.addHeader("User-Agent", "M5StopWatch-OTA");
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[ota] manifest GET %s → %d\n", url, code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[ota] JSON parse error: %s\n", err.c_str());
        return false;
    }

    const char* tag = doc["tag_name"];
    if (!tag) {
        Serial.println("[ota] manifest missing tag_name");
        return false;
    }
    out_tag = tag;

    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        const char* name = asset["name"];
        if (name && strcmp(name, "firmware.bin") == 0) {
            const char* dl = asset["browser_download_url"];
            if (dl) {
                out_asset_url = dl;
                return true;
            }
        }
    }
    Serial.println("[ota] no firmware.bin asset found in manifest");
    return false;
}

static bool downloadAndApply(const char* url) {
    HTTPClient http;
    bool ok = false;
    WiFiClientSecure secure;

    if (strncmp(url, "https://", 8) == 0) {
        secure.setInsecure();
        ok = http.begin(secure, url);
    } else {
        ok = http.begin(url);
    }
    if (!ok) return false;
    http.setTimeout(20000);
    http.addHeader("User-Agent", "M5StopWatch-OTA");

    // GitHub Releases serves through redirects → enable follow
    const char* follow_headers[] = { "Location" };
    http.collectHeaders(follow_headers, 1);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[ota] firmware GET → %d\n", code);
        http.end();
        return false;
    }

    int size = http.getSize();
    Serial.printf("[ota] downloading %d bytes...\n", size);
    if (size <= 0) {
        Serial.println("[ota] missing Content-Length");
        http.end();
        return false;
    }

    if (!Update.begin(size, U_FLASH)) {
        Serial.printf("[ota] Update.begin failed: %s\n", Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    if (written != (size_t)size) {
        Serial.printf("[ota] WARNING wrote %u of %d bytes\n",
                      (unsigned)written, size);
    }

    if (!Update.end(true)) {
        Serial.printf("[ota] Update.end failed: %s\n", Update.errorString());
        http.end();
        return false;
    }
    http.end();

    Serial.println("[ota] update applied, restarting in 1s...");
    delay(1000);
    ESP.restart();
    return true;     // unreachable
}

bool checkAndUpdate(const char* manifest_url, bool force) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ota] skip: no WiFi");
        return false;
    }
    Serial.printf("[ota] current=%s  checking %s ...\n",
                  FIRMWARE_VERSION, manifest_url);

    String latest_tag, asset_url;
    if (!fetchManifest(manifest_url, latest_tag, asset_url)) return false;

    int cmp = semverCmp(latest_tag.c_str(), FIRMWARE_VERSION);
    Serial.printf("[ota] latest=%s  cmp=%d  force=%d\n",
                  latest_tag.c_str(), cmp, (int)force);
    if (!force && cmp <= 0) {
        Serial.println("[ota] up to date");
        return false;
    }

    Serial.printf("[ota] updating to %s ← %s\n",
                  latest_tag.c_str(), asset_url.c_str());
    return downloadAndApply(asset_url.c_str());
}

}  // namespace ota
