// Voice glue for the two PAPA↔Stanley legs. The watch is a dumb PCM device:
// it records/plays raw 16 kHz mono int16. All codec work lives here.
//
// PAPA→Stanley: Telegram voice notes are OGG/Opus → decode to 16 kHz PCM.
// Cloudflare bans runtime WebAssembly.compile, and ogg-opus-decoder inlines its
// wasm to compile at runtime — so we STATIC-import a precompiled module
// (src/opus_decoder.wasm, see scripts/capture-opus-wasm.mjs) and inject it via
// OpusDecoder.module, which makes the lib skip compile and only instantiate.
import "./_wasm_worker_stub.js";   // MUST precede the opus pkgs (ESM hoists imports)
import opusModule from "./opus_decoder.wasm";
import { OpusDecoder } from "opus-decoder";
import { OggOpusDecoder } from "ogg-opus-decoder";

OpusDecoder.module = opusModule;

const OUT_RATE = 16000;            // the watch plays/records raw int16 mono @16k
const MAX_SECONDS = 30;            // watch caps its download ~2MB/15s; keep clips snappy

const clamp16 = (f) => {
    const v = Math.round(f * 32767);
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
};

// OGG/Opus bytes → little-endian int16 mono PCM @16 kHz (workerd is little-endian,
// so Int16Array.buffer is already the byte order the ESP32 expects).
export async function decodeVoiceToPcm16(oggBytes) {
    const dec = new OggOpusDecoder({ sampleRate: OUT_RATE });   // libopus resamples to 16k for us
    await dec.ready;
    let channelData, samplesDecoded;
    try {
        ({ channelData, samplesDecoded } = await dec.decodeFile(oggBytes));
    } finally {
        dec.free();
    }
    const n = Math.min(samplesDecoded, OUT_RATE * MAX_SECONDS);
    const pcm = new Int16Array(n);
    if (channelData.length === 1) {
        const a = channelData[0];
        for (let i = 0; i < n; i++) pcm[i] = clamp16(a[i]);
    } else {
        const a = channelData[0], b = channelData[1];
        for (let i = 0; i < n; i++) pcm[i] = clamp16((a[i] + b[i]) * 0.5);
    }
    return { bytes: new Uint8Array(pcm.buffer, 0, n * 2), samples: n, truncated: samplesDecoded > n };
}

// Raw int16 mono PCM → a minimal 44-byte-header WAV so Telegram can play it.
export function pcm16ToWav(pcmBytes, rate = OUT_RATE) {
    const dataLen = pcmBytes.length;
    const buf = new ArrayBuffer(44 + dataLen);
    const dv = new DataView(buf);
    const str = (o, s) => { for (let i = 0; i < s.length; i++) dv.setUint8(o + i, s.charCodeAt(i)); };
    str(0, "RIFF");  dv.setUint32(4, 36 + dataLen, true);  str(8, "WAVE");
    str(12, "fmt "); dv.setUint32(16, 16, true);  dv.setUint16(20, 1, true);   // PCM
    dv.setUint16(22, 1, true);                                                 // mono
    dv.setUint32(24, rate, true); dv.setUint32(28, rate * 2, true);            // byte rate
    dv.setUint16(32, 2, true);  dv.setUint16(34, 16, true);                    // block align / bits
    str(36, "data"); dv.setUint32(40, dataLen, true);
    new Uint8Array(buf, 44).set(pcmBytes);
    return new Uint8Array(buf);
}

// Telegram getFile → download the file bytes.
export async function tgGetFileBytes(token, fileId) {
    const r = await fetch(`https://api.telegram.org/bot${token}/getFile?file_id=${encodeURIComponent(fileId)}`);
    const j = await r.json();
    if (!j.ok) throw new Error("getFile: " + JSON.stringify(j));
    const fr = await fetch(`https://api.telegram.org/file/bot${token}/${j.result.file_path}`);
    if (!fr.ok) throw new Error("download: HTTP " + fr.status);
    return new Uint8Array(await fr.arrayBuffer());
}

// Send a WAV to a chat as a playable audio message.
export async function tgSendAudioWav(token, chatId, wavBytes, caption) {
    const fd = new FormData();
    fd.append("chat_id", String(chatId));
    fd.append("audio", new Blob([wavBytes], { type: "audio/wav" }), "stanley.wav");
    if (caption) fd.append("caption", caption);
    const r = await fetch(`https://api.telegram.org/bot${token}/sendAudio`, { method: "POST", body: fd });
    const j = await r.json();
    if (!j.ok) throw new Error("sendAudio: " + JSON.stringify(j));
    return j;
}
