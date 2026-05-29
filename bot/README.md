# papa-watch-bot

Cloudflare Worker that bridges Telegram (and later 飞书) bot commands to the MQTT
topic Stanley's watch subscribes to.

## Architecture

```
dad on Telegram ──webhook──► Worker ──MQTT WS──► broker.emqx.io ──► Stanley's watch
                                                                       ↓ lub-dub
```

## Setup

```bash
cd bot
npx wrangler login          # opens browser; one-time per dev machine

# Secrets (each prompts once for the value):
npx wrangler secret put TELEGRAM_BOT_TOKEN
npx wrangler secret put WEBHOOK_SECRET     # any random string, used in URL + header

npx wrangler deploy
# → prints the worker URL, e.g. https://papa-watch-bot.<your-user>.workers.dev
```

Tell Telegram where to push webhooks:

```bash
TOKEN=...                                       # your bot token
SECRET=...                                      # the WEBHOOK_SECRET you used above
URL="https://papa-watch-bot.<your-user>.workers.dev/tg/$SECRET"

curl -s "https://api.telegram.org/bot$TOKEN/setWebhook" \
     -d "url=$URL" \
     -d "secret_token=$SECRET"
```

Send `/heart` to the bot in Telegram. Watch should buzz lub-dub within ~500ms.

## Commands

| Command | Effect |
|---|---|
| `/heart` | Sends a heart with default ❤ |
| `/heart 💕` | Custom emoji |
| `/heart 想你了` | Free-form note (will display on watch) |
| `/ota` | Triggers watch to fetch latest firmware from GitHub Releases |
| `/help` | Lists commands |

## Allowlisting senders

By default anyone who finds the bot can send hearts. To restrict to mom + sister + you:

```bash
npx wrangler secret put ALLOWED_USERS
# value: yourtgusername,momusername,sisusername
```
