#!/usr/bin/env python3
"""
MQTT → Telegram relay for Stanley's hearts.

Runs as a long-lived subscriber on a machine that's always on (the user's
Mac at home, an Oracle Free Tier VM, etc.). It subscribes to
`stopwatch/<pair>/from-son` on broker.emqx.io and forwards each heart to
PAPA via the Telegram bot.

Why this script exists separately from the Cloudflare Worker bot: Workers
are request/response and can't maintain a long-lived MQTT subscription.
The proper fix lives in post-trip task #57 (Durable Object subscriber);
this is the bridge in the meantime.

Setup (one-time):
    pip install paho-mqtt requests

Run (foreground, leave terminal open):
    TG_TOKEN="..." TG_CHAT_ID="..." PAIR_ID="stanley-dad-2026" \
        python3 bot/relay_to_telegram.py

Auto-restart on Mac (launchd):
    See bot/relay_to_telegram.plist (template at bottom of this file).
"""
import json
import os
import sys
import time
from datetime import datetime, timezone

try:
    import paho.mqtt.client as mqtt
    import requests
except ImportError:
    print("missing deps — run: pip install paho-mqtt requests", file=sys.stderr)
    sys.exit(1)

BROKER       = os.environ.get("MQTT_BROKER", "broker.emqx.io")
BROKER_PORT  = int(os.environ.get("MQTT_PORT", "1883"))
PAIR_ID      = os.environ["PAIR_ID"]
TOPIC_SUB    = f"stopwatch/{PAIR_ID}/from-son"
TG_TOKEN     = os.environ["TG_TOKEN"]
TG_CHAT_ID   = os.environ["TG_CHAT_ID"]

def send_telegram(text: str) -> bool:
    """Push one message to PAPA's Telegram. Returns True on success."""
    url = f"https://api.telegram.org/bot{TG_TOKEN}/sendMessage"
    try:
        r = requests.post(url, json={"chat_id": TG_CHAT_ID, "text": text},
                          timeout=10)
        if r.status_code != 200:
            print(f"[tg] FAIL {r.status_code}: {r.text}", file=sys.stderr)
            return False
        return True
    except Exception as e:
        print(f"[tg] EXC: {e}", file=sys.stderr)
        return False

def format_heart(payload: bytes) -> str:
    """Heart payload from watch is {'t': unix_ts}. Render a friendly line."""
    try:
        doc = json.loads(payload.decode("utf-8"))
        ts  = int(doc.get("t", time.time()))
        local = datetime.fromtimestamp(ts).strftime("%H:%M")
        return f"💗 Stanley sent you a heart  ({local} his local)"
    except Exception:
        return "💗 Stanley sent you a heart"

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"[mqtt] connected → subscribing {TOPIC_SUB}")
        client.subscribe(TOPIC_SUB, qos=1)
    else:
        print(f"[mqtt] connect rc={rc}", file=sys.stderr)

def on_message(client, userdata, msg):
    now_iso = datetime.now(timezone.utc).isoformat(timespec="seconds")
    print(f"[mqtt] {now_iso} {msg.topic} {msg.payload[:80]!r}")
    text = format_heart(msg.payload)
    send_telegram(text)

def main():
    client_id = f"papa-relay-{int(time.time())}"
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    c.on_connect = on_connect
    c.on_message = on_message
    while True:
        try:
            c.connect(BROKER, BROKER_PORT, keepalive=60)
            c.loop_forever()
        except Exception as e:
            print(f"[mqtt] loop_forever crashed: {e}", file=sys.stderr)
            time.sleep(5)

if __name__ == "__main__":
    main()
