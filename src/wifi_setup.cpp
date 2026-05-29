// wifi_setup — implementation. See header for purpose. Visual polish deferred.

#include "wifi_setup.h"
#include <WiFi.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <ctype.h>

namespace wifi_setup {

// ── NVS storage ──────────────────────────────────────────
static Preferences  prefs;
static const char*  NVS_NS    = "wifi";
static const char*  KEY_SSID  = "ssid";
static const char*  KEY_PASS  = "pass";

static void saveCreds(const String& ssid, const String& pass) {
    prefs.begin(NVS_NS, false);
    prefs.putString(KEY_SSID, ssid);
    prefs.putString(KEY_PASS, pass);
    prefs.end();
}
static bool loadCreds(String& ssid, String& pass) {
    prefs.begin(NVS_NS, true);
    ssid = prefs.getString(KEY_SSID, "");
    pass = prefs.getString(KEY_PASS, "");
    prefs.end();
    return ssid.length() > 0;
}
void forgetSaved() {
    prefs.begin(NVS_NS, false);
    prefs.clear();
    prefs.end();
}

bool   isConnected() { return WiFi.status() == WL_CONNECTED; }
String currentSSID() { return WiFi.SSID(); }

// ── canvas + geometry ────────────────────────────────────
static M5Canvas g_canvas(nullptr);
static bool     g_canvas_ready = false;
static constexpr int CX = 234, CY = 234, R = 232, W = 468, H = 468;

static void ensureCanvas() {
    if (g_canvas_ready) return;
    g_canvas.setPsram(true);
    g_canvas.setColorDepth(16);
    g_canvas.createSprite(W, H);
    g_canvas_ready = true;
}

// ── shared draw primitives (placeholder-grade) ───────────
static constexpr uint16_t C_BG     = TFT_BLACK;
static constexpr uint16_t C_FG     = TFT_WHITE;
static constexpr uint16_t C_DIM    = 0x8410;
static constexpr uint16_t C_KEY    = 0x2124;
static constexpr uint16_t C_KEY_HI = 0x07E0;   // green when pressed/cursor
static constexpr uint16_t C_OK     = 0x07E0;
static constexpr uint16_t C_ERR    = 0xF800;

static void drawHeader(const char* title) {
    g_canvas.fillScreen(C_BG);
    g_canvas.drawCircle(CX, CY, R, 0x2104);
    g_canvas.setTextColor(C_FG, C_BG);
    g_canvas.setFont(&fonts::FreeSans12pt7b);
    g_canvas.setTextDatum(top_center);
    g_canvas.drawString(title, CX, 22);
}

static void drawStatusOverlay(const char* line1, const char* line2 = nullptr,
                              uint16_t color = C_FG) {
    g_canvas.fillScreen(C_BG);
    g_canvas.drawCircle(CX, CY, R, 0x2104);
    g_canvas.setTextDatum(middle_center);
    g_canvas.setTextColor(color, C_BG);
    g_canvas.setFont(&fonts::FreeSans18pt7b);
    g_canvas.drawString(line1, CX, CY - (line2 ? 18 : 0));
    if (line2) {
        g_canvas.setFont(&fonts::FreeSans12pt7b);
        g_canvas.setTextColor(C_DIM, C_BG);
        g_canvas.drawString(line2, CX, CY + 22);
    }
    g_canvas.pushSprite(&M5.Display, 0, 0);
}

// ── SSID scan ────────────────────────────────────────────
struct ScannedNet { String ssid; int32_t rssi; bool secure; };

static std::vector<ScannedNet> scanNets() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, false);   // sync, hide hidden
    std::vector<ScannedNet> out;
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        if (s.length() == 0) continue;
        out.push_back({ s, WiFi.RSSI(i),
                        WiFi.encryptionType(i) != WIFI_AUTH_OPEN });
    }
    WiFi.scanDelete();
    std::sort(out.begin(), out.end(),
              [](const ScannedNet& a, const ScannedNet& b){ return a.rssi > b.rssi; });
    // dedupe by SSID (keep highest-RSSI entry)
    std::vector<ScannedNet> deduped;
    for (auto& n : out) {
        bool seen = false;
        for (auto& d : deduped) if (d.ssid == n.ssid) { seen = true; break; }
        if (!seen) deduped.push_back(n);
    }
    return deduped;
}

// ── SSID list view ───────────────────────────────────────
// Returns index into `nets`, or -1 if user wants to rescan (long-press anywhere).
static int pickSSID(const std::vector<ScannedNet>& nets) {
    int scroll = 0;
    const int ITEM_H = 46;
    const int LIST_Y = 70;
    const int VISIBLE = 6;
    int item_count = nets.size();

    auto render = [&]() {
        drawHeader("Select Wi-Fi");
        int n_show = std::min(VISIBLE, item_count - scroll);
        for (int i = 0; i < n_show; i++) {
            const auto& net = nets[i + scroll];
            int y = LIST_Y + i * ITEM_H;
            g_canvas.fillRoundRect(54, y, 360, ITEM_H - 6, 8, C_KEY);
            g_canvas.setTextDatum(middle_left);
            g_canvas.setTextColor(C_FG, C_KEY);
            g_canvas.setFont(&fonts::FreeSans12pt7b);
            String name = net.ssid;
            if (name.length() > 18) name = name.substring(0, 16) + "..";
            g_canvas.drawString(name, 68, y + (ITEM_H - 6) / 2);

            // RSSI bars (4 levels)
            int bars = 1;
            if (net.rssi > -70) bars = 2;
            if (net.rssi > -60) bars = 3;
            if (net.rssi > -50) bars = 4;
            int bx = 360, by = y + ITEM_H - 18;
            for (int b = 0; b < 4; b++) {
                int hh = 4 + b * 3;
                uint16_t col = (b < bars) ? C_FG : 0x4208;
                g_canvas.fillRect(bx + b * 5, by - hh + 14, 3, hh, col);
            }
            // lock glyph
            if (net.secure) {
                g_canvas.drawRect(386, y + 14, 9, 8, C_FG);
                g_canvas.drawRect(388, y + 10, 5, 5, C_FG);
            }
        }
        // scroll hints
        if (scroll > 0)
            g_canvas.fillTriangle(CX-8, 56, CX+8, 56, CX, 46, C_DIM);
        if (scroll + VISIBLE < item_count)
            g_canvas.fillTriangle(CX-8, LIST_Y + VISIBLE*ITEM_H + 4,
                                  CX+8, LIST_Y + VISIBLE*ITEM_H + 4,
                                  CX,   LIST_Y + VISIBLE*ITEM_H + 14, C_DIM);
        // bottom hint
        g_canvas.setTextDatum(bottom_center);
        g_canvas.setTextColor(C_DIM, C_BG);
        g_canvas.setFont(&fonts::Font0);
        g_canvas.drawString("hold to rescan", CX, H - 14);

        g_canvas.pushSprite(&M5.Display, 0, 0);
    };

    int  cursor = 0;        // for dual-button mode
    bool need_redraw = true;
    int  touch_start_y = -1;

    while (true) {
        if (need_redraw) { render(); need_redraw = false; }
        M5.update();

        // touch: tap → select; vertical drag → scroll; hold → rescan
        auto t = M5.Touch.getDetail();
        if (t.wasPressed()) touch_start_y = t.y;
        if (t.wasHold()) return -1;   // signal: rescan
        if (t.wasReleased()) {
            int dy = t.y - touch_start_y;
            if (abs(dy) < 20) {
                int idx = (t.y - LIST_Y) / ITEM_H;
                if (idx >= 0 && idx < VISIBLE && idx + scroll < item_count) {
                    return idx + scroll;
                }
            } else {
                // scroll
                int delta = -dy / ITEM_H;
                scroll = std::max(0, std::min(scroll + delta, item_count - VISIBLE));
                if (item_count <= VISIBLE) scroll = 0;
                need_redraw = true;
            }
        }
        // dual-btn fallback: BtnA = next item, BtnB = select
        if (M5.BtnA.wasPressed()) {
            cursor = (cursor + 1) % item_count;
            // keep cursor in scroll window
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + VISIBLE) scroll = cursor - VISIBLE + 1;
            need_redraw = true;
            Serial.printf("[wifi-cfg] cursor → %d (%s)\n", cursor,
                          nets[cursor].ssid.c_str());
        }
        if (M5.BtnB.wasPressed()) {
            return cursor;
        }
        delay(15);
    }
}

// ── mini QWERTY + dual-button keyboard ───────────────────
//
// Layout state machine:
//   layer 0  lowercase qwerty
//   layer 1  uppercase qwerty (SHIFT)
//   layer 2  numbers + symbols
//
// Returns false if user backs out, true if user submits.
struct KeyRect { int x, y, w, h; char ch; const char* label; uint8_t kind; };
// kind: 0=char  1=SHIFT  2=BACKSPACE  3=SYM  4=ENTER  5=SPACE

static const char* ROW1_LOW = "qwertyuiop";
static const char* ROW2_LOW = "asdfghjkl";
static const char* ROW3_LOW = "zxcvbnm";
static const char* ROW1_UP  = "QWERTYUIOP";
static const char* ROW2_UP  = "ASDFGHJKL";
static const char* ROW3_UP  = "ZXCVBNM";
static const char* ROW1_SYM = "1234567890";
static const char* ROW2_SYM = "-_/:;()$&@";
static const char* ROW3_SYM = ".,?!'\"";

static std::vector<KeyRect> buildLayout(int layer) {
    std::vector<KeyRect> ks;
    const int KW = 44, KH = 50, GAP = 2;
    const int Y1 = 178, Y2 = Y1 + KH + GAP, Y3 = Y2 + KH + GAP, Y4 = Y3 + KH + GAP;

    auto row = [&](const char* s, int y, int total_w_keys) {
        int w = total_w_keys * (KW + GAP) - GAP;
        int x0 = (W - w) / 2;
        for (int i = 0; s[i]; i++) {
            ks.push_back({ x0 + i*(KW+GAP), y, KW, KH, s[i], nullptr, 0 });
        }
    };

    const char* r1 = (layer == 0) ? ROW1_LOW : (layer == 1) ? ROW1_UP : ROW1_SYM;
    const char* r2 = (layer == 0) ? ROW2_LOW : (layer == 1) ? ROW2_UP : ROW2_SYM;
    const char* r3 = (layer == 0) ? ROW3_LOW : (layer == 1) ? ROW3_UP : ROW3_SYM;

    row(r1, Y1, strlen(r1));
    row(r2, Y2, strlen(r2));
    // row3: SHIFT + letters + BKSP
    int n3 = strlen(r3);
    int row3_w = (n3 + 2) * (KW + GAP) - GAP + 20;   // wider shift/bksp
    int x = (W - row3_w) / 2;
    ks.push_back({ x, Y3, KW + 10, KH, 0, layer == 1 ? "*" : "^", 1 });  // SHIFT
    x += KW + 10 + GAP;
    for (int i = 0; i < n3; i++) {
        ks.push_back({ x, Y3, KW, KH, r3[i], nullptr, 0 });
        x += KW + GAP;
    }
    ks.push_back({ x, Y3, KW + 10, KH, 0, "<x", 2 });  // BKSP

    // row4: SYM/ABC + SPACE + ENTER, centered, narrower (round bottom)
    int sym_w = 60, ent_w = 60, sp_w = 160;
    int row4_total = sym_w + sp_w + ent_w + 2 * GAP;
    int x4 = (W - row4_total) / 2;
    ks.push_back({ x4, Y4, sym_w, KH, 0, layer == 2 ? "abc" : "?123", 3 });
    ks.push_back({ x4 + sym_w + GAP, Y4, sp_w, KH, ' ', "_", 5 });
    ks.push_back({ x4 + sym_w + GAP + sp_w + GAP, Y4, ent_w, KH, 0, "OK", 4 });
    return ks;
}

static void drawKeyboardKey(const KeyRect& k, bool highlight, bool pressed) {
    uint16_t bg = pressed ? C_KEY_HI : (highlight ? 0x3186 : C_KEY);
    uint16_t fg = pressed ? C_BG : C_FG;
    g_canvas.fillRoundRect(k.x, k.y, k.w, k.h, 6, bg);
    g_canvas.setTextDatum(middle_center);
    g_canvas.setTextColor(fg, bg);
    g_canvas.setFont(&fonts::FreeSans12pt7b);
    if (k.ch && !k.label) {
        char s[2] = { k.ch, 0 };
        g_canvas.drawString(s, k.x + k.w/2, k.y + k.h/2);
    } else if (k.label) {
        g_canvas.drawString(k.label, k.x + k.w/2, k.y + k.h/2);
    }
}

static bool enterPassword(const String& ssid, String& out_pass) {
    int      layer = 0;
    String   pass = "";
    int      cursor = 0;             // for dual-btn
    int      pressed_idx = -1;       // visual press flash
    uint32_t press_until = 0;
    bool     need_redraw = true;

    auto layout = buildLayout(layer);

    auto render = [&]() {
        drawHeader("Password");
        // SSID display
        g_canvas.setTextDatum(top_center);
        g_canvas.setTextColor(C_DIM, C_BG);
        g_canvas.setFont(&fonts::Font0);
        String s = ssid;
        if (s.length() > 22) s = s.substring(0, 20) + "..";
        g_canvas.drawString(s, CX, 50);

        // password masked field
        int fx = 50, fy = 90, fw = W - 100, fh = 56;
        g_canvas.fillRoundRect(fx, fy, fw, fh, 6, 0x1082);
        g_canvas.drawRoundRect(fx, fy, fw, fh, 6, C_DIM);
        g_canvas.setTextDatum(middle_left);
        g_canvas.setTextColor(C_FG, 0x1082);
        g_canvas.setFont(&fonts::FreeSans18pt7b);
        String masked = "";
        for (size_t i = 0; i < pass.length(); i++) masked += "*";
        g_canvas.drawString(masked, fx + 12, fy + fh/2);
        // blink cursor
        if ((millis() / 500) % 2) {
            int cur_x = fx + 12 + g_canvas.textWidth(masked.c_str()) + 2;
            g_canvas.drawFastVLine(cur_x, fy + 10, fh - 20, C_FG);
        }

        // keys
        for (int i = 0; i < (int)layout.size(); i++) {
            bool hi = (i == cursor);
            bool pr = (i == pressed_idx) && millis() < press_until;
            drawKeyboardKey(layout[i], hi, pr);
        }

        // bottom hint
        g_canvas.setTextDatum(bottom_center);
        g_canvas.setTextColor(C_DIM, C_BG);
        g_canvas.setFont(&fonts::Font0);
        g_canvas.drawString("Y=next  B=select  hold B=submit", CX, H - 6);

        g_canvas.pushSprite(&M5.Display, 0, 0);
    };

    auto applyKey = [&](const KeyRect& k) {
        switch (k.kind) {
            case 0:  // char
                pass += k.ch;
                if (layer == 1) { layer = 0; layout = buildLayout(layer); }
                break;
            case 1:  // SHIFT
                layer = (layer == 0) ? 1 : (layer == 1) ? 0 : 0;
                layout = buildLayout(layer);
                break;
            case 2:  // BACKSPACE
                if (pass.length()) pass.remove(pass.length() - 1);
                break;
            case 3:  // SYM toggle
                layer = (layer == 2) ? 0 : 2;
                layout = buildLayout(layer);
                break;
            case 4:  // ENTER → submit
                out_pass = pass;
                return true;
            case 5:  // SPACE
                pass += ' ';
                break;
        }
        return false;
    };

    while (true) {
        if (need_redraw || millis() < press_until + 50 ||
            (millis() / 500) % 2 != (millis() - 16) / 500 % 2) {
            render();
            need_redraw = false;
        }
        M5.update();

        auto t = M5.Touch.getDetail();
        if (t.wasReleased()) {
            for (int i = 0; i < (int)layout.size(); i++) {
                const auto& k = layout[i];
                if (t.x >= k.x && t.x < k.x + k.w &&
                    t.y >= k.y && t.y < k.y + k.h) {
                    pressed_idx = i;
                    press_until = millis() + 120;
                    if (applyKey(k)) return true;
                    cursor = i;
                    need_redraw = true;
                    Serial.printf("[kbd] tap '%c' kind=%d  pass.len=%d\n",
                                  k.ch ? k.ch : '?', k.kind, pass.length());
                    break;
                }
            }
        }

        // dual-btn fallback
        if (M5.BtnA.wasPressed()) {
            cursor = (cursor + 1) % layout.size();
            need_redraw = true;
        }
        if (M5.BtnB.wasPressed()) {
            const auto& k = layout[cursor];
            pressed_idx = cursor; press_until = millis() + 120;
            if (applyKey(k)) return true;
            need_redraw = true;
        }
        if (M5.BtnB.pressedFor(800)) {  // long-press B → submit
            out_pass = pass;
            return true;
        }
        delay(15);
    }
}

// ── connect with status overlay ──────────────────────────
static bool connectWithFeedback(const String& ssid, const String& pass,
                                uint32_t timeout_ms = 12000) {
    drawStatusOverlay("Connecting", ssid.c_str());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (WiFi.status() == WL_CONNECTED) {
            drawStatusOverlay("Connected", ssid.c_str(), C_OK);
            delay(1200);
            return true;
        }
        if (WiFi.status() == WL_CONNECT_FAILED ||
            WiFi.status() == WL_NO_SSID_AVAIL) {
            break;
        }
        M5.update();
        delay(80);
    }
    drawStatusOverlay("Failed", "wrong password?", C_ERR);
    delay(1800);
    return false;
}

// ── top-level orchestration ──────────────────────────────
bool tryAutoConnect(uint32_t timeout_ms) {
    String ssid, pass;
    if (!loadCreds(ssid, pass)) return false;
    Serial.printf("[wifi] auto-connect saved: %s\n", ssid.c_str());

    ensureCanvas();
    drawStatusOverlay("Connecting", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (WiFi.status() == WL_CONNECTED) return true;
        M5.update();
        delay(80);
    }
    return false;
}

void runInteractiveConfig() {
    ensureCanvas();

    while (true) {
        drawStatusOverlay("Scanning Wi-Fi", "...");
        auto nets = scanNets();
        Serial.printf("[wifi] scanned %u SSIDs\n", (unsigned)nets.size());
        if (nets.empty()) {
            drawStatusOverlay("No Wi-Fi found", "rescanning...");
            delay(1500);
            continue;
        }

        int idx = pickSSID(nets);
        if (idx < 0) continue;   // rescan request

        String pass;
        if (!enterPassword(nets[idx].ssid, pass)) continue;

        if (connectWithFeedback(nets[idx].ssid, pass)) {
            saveCreds(nets[idx].ssid, pass);
            return;
        }
        // else loop back to scan
    }
}

}  // namespace wifi_setup
