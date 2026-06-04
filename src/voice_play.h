// voice_play — download a raw 16kHz mono int16 PCM clip over HTTP and play it
// through the speaker. The bot pushes {"cmd":"voice","url":..} over MQTT; the
// Worker will serve decoded PCM (Opus→PCM happens server-side). Watch stays a
// dumb PCM player. request() just queues; tick() (from the main loop) does the
// blocking download + playRaw so it never runs inside the MQTT callback.
#pragma once

namespace voice_play {

void request(const char* url);   // queue a clip URL
void tick();                     // call once per main loop
bool busy();                     // downloading or still playing

}  // namespace voice_play
