// Loaded for its side effect BEFORE the opus decoder packages: their WebWorker
// variants re-export a browser `Worker` global at module-load. We never use them,
// but the bare reference throws in workerd. A dummy class satisfies the load.
globalThis.Worker = globalThis.Worker || class {};
