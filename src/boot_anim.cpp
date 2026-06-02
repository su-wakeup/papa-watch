#include "boot_anim.h"
#include <M5Unified.h>
#include <LittleFS.h>
#include "assets/boot_jingle.h"

namespace boot_anim {

static constexpr int FRAMES    = 26;
static constexpr int FRAME_W   = 330;   // matches data/boot/*.png; scaled to fill
static constexpr int FRAME_MS  = 72;    // ~26 frames over ~1.9 s
static constexpr int CHIME_AT  = 11;    // frame where the coin lights up

void play() {
    if (!LittleFS.begin(false)) {           // assets are flashed read-only
        Serial.println("[boot_anim] LittleFS mount failed — skipping ceremony");
        return;
    }
    Serial.println("[boot_anim] playing ceremony");
    // Upscale the tightly-cropped coin frame to fill the panel — no black border.
    const float scale = (float)M5.Display.width() / FRAME_W;

    M5.Display.fillScreen(TFT_BLACK);
    char path[24];
    for (int i = 0; i < FRAMES; i++) {
        // The coin's chime lands with the light, not after the animation.
        if (i == CHIME_AT) {
            M5.Speaker.playRaw(boot_jingle_data, boot_jingle_samples,
                               boot_jingle_sample_rate);
        }
        snprintf(path, sizeof(path), "/boot/b%02d.png", i);
        // M5GFX has no LittleFS DataWrapper, so hand it the File as a Stream.
        File f = LittleFS.open(path, "r");
        if (f) {
            M5.Display.drawPng(&f, 0, 0, 0, 0, 0, 0, scale, scale);
            f.close();
        }
        delay(FRAME_MS);
    }

    // The baked engraving is below the art's resolution, so render the
    // inscription crisply on top of the held final frame instead.
    const char* line = "For Stanley, my little legend.";
    const int tx = M5.Display.width()  / 2;
    const int ty = M5.Display.height() * 72 / 100;
    M5.Display.setFont(&fonts::FreeSerifItalic12pt7b);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(M5.Display.color888(0, 0, 0));          // shadow
    M5.Display.drawString(line, tx + 2, ty + 2);
    M5.Display.setTextColor(M5.Display.color888(0xF7, 0xEC, 0xCF)); // cream
    M5.Display.drawString(line, tx, ty);

    delay(900);                              // hold the inscribed coin a beat
}

}  // namespace boot_anim
