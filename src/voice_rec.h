// voice_rec — record a short 16kHz mono PCM clip from the mic and HTTP-POST it
// (Stanley→PAPA leg, roadmap #6 ③). The watch uploads raw PCM; the Worker will
// encode Opus + sendVoice to Telegram. Shares the ES8311 codec with the speaker,
// so it ends the speaker while recording and restores it after.
#pragma once

namespace voice_rec {

void        start();        // begin a fixed-length recording (no-op if busy)
void        tick();         // call each main loop; auto-stops + uploads when full
bool        active();       // recording or uploading
const char* statusText();   // short UI string ("REC 3.2s" / "Uploading..." / "Sent OK")

}  // namespace voice_rec
