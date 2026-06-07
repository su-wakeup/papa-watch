// The opus "ML" (speech-enhancement) decoder is a ~4 MB wasm variant that
// ogg-opus-decoder dynamic-imports ONLY when speechQualityEnhancement is set.
// We never set it, so we alias the package to this empty stub (wrangler.toml)
// to keep the Worker under Cloudflare's size limit. Never actually executed.
export class OpusMLDecoder {}
export class OpusMLDecoderWebWorker {}
