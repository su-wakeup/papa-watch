#!/usr/bin/env bash
# Smoke-test for the family-events /ingest endpoint.
#
# Usage:
#   bash bot/scripts/test-ingest.sh <INGEST_TOKEN>
#
# POSTs a sample Lakeside-style email, then queries D1 to confirm the row
# landed with source_id auto-assigned to the Lakeside seed.
set -eu

TOKEN="${1:?Usage: $0 <INGEST_TOKEN>}"
URL="https://papa-watch-bot.su-wakeup.workers.dev/ingest/${TOKEN}/email"
JSON='{"from":"office@lakesidelosgatos.com","subject":"Spring Concert","body":"Spring concert June 7th 6 PM auditorium"}'

echo ">>> POSTing to $URL"
curl -sS -X POST -H "content-type: application/json" -d "$JSON" "$URL"
echo
echo

echo ">>> D1 raw_emails (latest 3 rows):"
npx wrangler d1 execute papa-family-events --remote \
    --command "SELECT id, source_id, from_address, subject, bytes FROM raw_emails ORDER BY id DESC LIMIT 3"
