#include "heart_relay.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

namespace heart_relay {

static constexpr const char* BROKER_HOST = "broker.emqx.io";
static constexpr int         BROKER_PORT = 1883;

static String        s_pair_id;
static String        s_topic_pub;
static String        s_topic_sub;
static String        s_client_id;
static WiFiClient    s_net;
static PubSubClient  s_mqtt(s_net);
static MessageCb     s_on_recv = nullptr;
static uint32_t      s_last_reconnect_attempt = 0;
static uint32_t      s_last_recv_ts           = 0;

static void onMessage(char* topic, byte* payload, unsigned int length) {
    // dedupe by payload timestamp (cheap parse)
    uint32_t ts = 0;
    for (unsigned i = 0; i < length; i++) {
        char c = (char)payload[i];
        if (c >= '0' && c <= '9') ts = ts * 10 + (c - '0');
        else if (ts > 0) break;        // stop at first non-digit after a run
    }
    if (ts && ts == s_last_recv_ts) {
        Serial.printf("[mqtt] RX dup ts=%lu, ignored\n", (unsigned long)ts);
        return;
    }
    s_last_recv_ts = ts;
    Serial.printf("[mqtt] RX %s len=%u ts=%lu\n", topic, length, (unsigned long)ts);
    if (s_on_recv) s_on_recv((const char*)payload, length);
}

static void reconnect() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (s_mqtt.connected()) return;

    Serial.printf("[mqtt] connecting %s:%d as %s ...\n",
                  BROKER_HOST, BROKER_PORT, s_client_id.c_str());
    if (s_mqtt.connect(s_client_id.c_str())) {
        Serial.println("[mqtt] connected");
        s_mqtt.subscribe(s_topic_sub.c_str(), 1);   // QoS 1
        Serial.printf("[mqtt] sub: %s\n", s_topic_sub.c_str());
        Serial.printf("[mqtt] pub: %s\n", s_topic_pub.c_str());
    } else {
        Serial.printf("[mqtt] connect failed rc=%d\n", s_mqtt.state());
    }
}

void begin(const char* pair_id) {
    s_pair_id   = pair_id;
    s_topic_pub = String("stopwatch/") + pair_id + "/from-son";
    s_topic_sub = String("stopwatch/") + pair_id + "/from-dad";
    s_client_id = String("stopwatch-son-") +
                  String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);

    s_mqtt.setServer(BROKER_HOST, BROKER_PORT);
    s_mqtt.setCallback(onMessage);
    s_mqtt.setKeepAlive(30);
    s_mqtt.setBufferSize(256);

    reconnect();
}

void tick() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!s_mqtt.connected()) {
        if (millis() - s_last_reconnect_attempt > 5000) {
            s_last_reconnect_attempt = millis();
            reconnect();
        }
    } else {
        s_mqtt.loop();
    }
}

bool sendHeart() {
    if (!s_mqtt.connected()) {
        Serial.println("[mqtt] sendHeart: not connected");
        return false;
    }
    char buf[40];
    snprintf(buf, sizeof(buf), "{\"t\":%lu}", (unsigned long)time(nullptr));
    bool ok = s_mqtt.publish(s_topic_pub.c_str(), buf);
    Serial.printf("[mqtt] TX %s payload=%s ok=%d\n",
                  s_topic_pub.c_str(), buf, (int)ok);
    return ok;
}

bool isConnected() { return s_mqtt.connected(); }

void setOnReceive(MessageCb cb) { s_on_recv = cb; }

}  // namespace heart_relay
