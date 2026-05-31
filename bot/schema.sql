-- papa-watch family events — D1 schema
--
-- Phase 1 holds raw inbound material (`raw_emails` rows + R2 body).
-- Phase 2 will populate `events` via LLM extraction.
-- Phase 3 will use `subscriptions` to fan extracted events out to
-- Telegram / MQTT / Calendar.
--
-- Apply with:
--   npx wrangler d1 execute papa-family-events --remote --file=schema.sql
-- Or local-only for dry runs:
--   npx wrangler d1 execute papa-family-events --local  --file=schema.sql

-- ─── sources ──────────────────────────────────────────────────────────────
-- Where did this content come from? Matched by email substring against
-- inbound `from` address so future ingests auto-assign.
CREATE TABLE IF NOT EXISTS sources (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL,                       -- "Lakeside Elementary"
    kind        TEXT    NOT NULL,                       -- school | teacher | family | system
    email_match TEXT,                                   -- e.g. "@lakesidelosgatos.com"
    created_at  INTEGER NOT NULL DEFAULT (unixepoch())
);

-- ─── raw_emails ───────────────────────────────────────────────────────────
-- Every inbound email lands here unmodified. Body lives in R2 because D1
-- rows are size-bounded; `r2_key` points back to the object.
-- `processed = 0` means Phase-2 LLM hasn't seen this row yet.
CREATE TABLE IF NOT EXISTS raw_emails (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    source_id    INTEGER REFERENCES sources(id),
    from_address TEXT    NOT NULL,
    to_address   TEXT,
    subject      TEXT,
    received_at  INTEGER NOT NULL,                      -- epoch seconds
    r2_key       TEXT    NOT NULL,                      -- "raw/<id>.txt"
    bytes        INTEGER,
    processed    INTEGER NOT NULL DEFAULT 0,
    created_at   INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX IF NOT EXISTS idx_raw_emails_processed ON raw_emails(processed);

-- ─── events ───────────────────────────────────────────────────────────────
-- Structured events. Populated by Phase-2 LLM extractor from raw_emails.
-- starts_at / ends_at are epoch UTC; the watch / calendar consumers do
-- the local-time formatting themselves.
CREATE TABLE IF NOT EXISTS events (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    raw_email_id    INTEGER REFERENCES raw_emails(id),
    title           TEXT    NOT NULL,
    kind            TEXT,                               -- performance | field-trip | deadline | meeting | holiday
    starts_at       INTEGER,
    ends_at         INTEGER,
    location        TEXT,
    description     TEXT,
    confidence      REAL,                               -- LLM confidence 0..1
    pushed_telegram INTEGER NOT NULL DEFAULT 0,
    pushed_mqtt     INTEGER NOT NULL DEFAULT 0,
    created_at      INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX IF NOT EXISTS idx_events_starts_at ON events(starts_at);

-- ─── subscriptions ────────────────────────────────────────────────────────
-- Fan-out routing: which channels want which sources. One row per
-- (source, channel) pair. Phase 3 reads this to know where to push.
CREATE TABLE IF NOT EXISTS subscriptions (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    source_id    INTEGER REFERENCES sources(id),
    channel_kind TEXT    NOT NULL,                      -- telegram | mqtt | calendar | imessage
    channel_id   TEXT    NOT NULL,                      -- chat_id | mqtt topic | ICS URL
    enabled      INTEGER NOT NULL DEFAULT 1,
    created_at   INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX IF NOT EXISTS idx_subscriptions_source ON subscriptions(source_id, enabled);

-- ─── seed: known sources ──────────────────────────────────────────────────
-- Lakeside Elementary is Stanley's school; we'll add more as you forward
-- their first email and learn their actual sender domain.
INSERT OR IGNORE INTO sources (id, name, kind, email_match) VALUES
    (1, 'Lakeside Elementary', 'school', '@lakesidelosgatos.com'),
    (2, 'System (manual / curl)', 'system', NULL);
