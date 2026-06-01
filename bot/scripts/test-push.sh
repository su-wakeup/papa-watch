#!/usr/bin/env bash
# Triggers the Phase-3 Telegram fan-out for any events with
# pushed_telegram = 0. Requires at least one row in subscriptions
# pointing at a real Telegram chat (see README / seed snippet).
#
# Usage:
#   bash bot/scripts/test-push.sh <INGEST_TOKEN>
set -eu

TOKEN="${1:?Usage: $0 <INGEST_TOKEN>}"
URL="https://papa-watch-bot.su-wakeup.workers.dev/push/${TOKEN}/run"

echo ">>> Triggering Telegram push sweep: $URL"
curl -sS "$URL"
echo
echo

echo ">>> events status (pushed_telegram flag):"
npx wrangler d1 execute papa-family-events --remote \
    --command "SELECT id, raw_email_id, title, kind, pushed_telegram, ROUND(confidence,2) AS conf FROM events ORDER BY id"

echo
echo ">>> subscriptions in effect:"
npx wrangler d1 execute papa-family-events --remote \
    --command "SELECT id, source_id, channel_kind, channel_id, enabled FROM subscriptions ORDER BY id"
