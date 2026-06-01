#!/usr/bin/env bash
# Triggers a Phase-2 LLM extraction sweep against any raw_emails rows with
# processed=0, then shows the resulting events table.
#
# Usage:
#   bash bot/scripts/test-extract.sh <INGEST_TOKEN>
set -eu

TOKEN="${1:?Usage: $0 <INGEST_TOKEN>}"
URL="https://papa-watch-bot.su-wakeup.workers.dev/extract/${TOKEN}/run"

echo ">>> Triggering extract sweep: $URL"
curl -sS "$URL"
echo
echo

echo ">>> raw_emails status:"
npx wrangler d1 execute papa-family-events --remote \
    --command "SELECT id, processed, from_address, subject FROM raw_emails ORDER BY id"

echo
echo ">>> events:"
npx wrangler d1 execute papa-family-events --remote \
    --command "SELECT id, raw_email_id, title, kind, datetime(starts_at,'unixepoch') AS starts_utc, location, ROUND(confidence,2) AS conf FROM events ORDER BY id"
