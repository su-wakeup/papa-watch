import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { readFileSync } from "node:fs";

const MAGIC = [0,97,115,109];
const captured = [];
const toBytes = (a) => a instanceof Uint8Array ? a : (a instanceof ArrayBuffer ? new Uint8Array(a) : null);
const grab = (arg) => { const b = toBytes(arg); if (b && b.length>4 && b[0]===0&&b[1]===97&&b[2]===115&&b[3]===109) captured.push(b.slice()); };
const origCompile = WebAssembly.compile.bind(WebAssembly);
WebAssembly.compile = (b, ...r) => { grab(b); return origCompile(b, ...r); };
const origInst = WebAssembly.instantiate.bind(WebAssembly);
WebAssembly.instantiate = (b, ...r) => { grab(b); return origInst(b, ...r); };

const { OggOpusDecoder } = await import("ogg-opus-decoder");
const dec = new OggOpusDecoder();
await dec.ready;
const ogg = readFileSync("/tmp/sample.ogg");
const out = await dec.decodeFile(ogg);
console.log("decoded:", out.samplesDecoded, "samples @", out.sampleRate, "Hz, channels:", out.channelData.length);
console.log("captured wasm modules:", captured.map(b=>b.length));
// keep the largest = the opus decoder (the small one is the puff inflater)
captured.sort((a,b)=>b.length-a.length);
const dst = fileURLToPath(new URL("../src/opus_decoder.wasm", import.meta.url));
writeFileSync(dst, Buffer.from(captured[0]));
console.log("wrote", dst, captured[0].length, "bytes, valid:", WebAssembly.validate(captured[0]));
