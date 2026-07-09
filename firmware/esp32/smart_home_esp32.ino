/*
 * Smart Home ESP32 Dashboard — v7
 * Abdelrahman Emad, ID 235338
 *
 * Major upgrades vs v6:
 *   1. Robust voice command parser (flexible natural-language phrasing)
 *   2. Light intensity by voice ("lights at 40", "dim to 25", etc.)
 *   3. Arm / disarm by voice (disarm requires PIN)
 *   4. Mode change (home/away) by voice
 *   5. Fan on/off by voice with natural phrasing
 *   6. Sensor queries with spoken answers ("what's the temperature")
 *   7. "Goodnight" and "I'm home" macros
 *   8. "Help" voice command lists everything
 *   9. TTS mute toggle on dashboard
 *  10. Email alert on intrusion via Gmail SMTP
 *  11. NTP time sync for real timestamps
 *  12. One-email-per-intrusion cool-down
 *
 * REQUIRED LIBRARY:
 *   Install "ESP Mail Client" by Mobizt via Arduino Library Manager
 *   (Tools -> Manage Libraries -> search "ESP Mail Client")
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP_Mail_Client.h>
#include <time.h>
#include "config.h"   // WIFI_SSID, WIFI_PASSWORD, SMTP_*, EMAIL_* -- see config.example.h

// =========================================================================
// CONFIGURATION
// =========================================================================
// All Wi-Fi and Gmail SMTP credentials live in config.h (gitignored).
// Copy config.example.h to config.h and fill in your own values before
// flashing this sketch.

// NTP
const char* NTP_SERVER  = "pool.ntp.org";
const long  GMT_OFFSET  = 2 * 3600;   // Cairo = UTC+2
const int   DST_OFFSET  = 0;

// PIC UART
#define PIC_TX_PIN 17   // ESP32 TX -> PIC RX (RC7)
#define PIC_RX_PIN 16   // ESP32 RX <- PIC TX (RC6)

// =========================================================================
// GLOBALS
// =========================================================================
WebServer server(80);
SMTPSession smtp;
HardwareSerial PicSerial(2);

// State decoded from PIC status frames
struct {
  int   light = 0, temp = 0, hum = 0;
  int   mode = 0;              // 0=HOME, 1=AWAY
  int   door = 0;              // 0=locked, 1=unlocked
  int   fan = 0;               // 0=off, 1=on
  int   pir = 0;               // 0=no motion, 1=motion
  int   led = 0;               // 0..100
  int   ctrl = 0;              // 0=AUTO, 1=MANUAL, 2=VOICE
  int   armed = 0;             // 0=disarmed, 1=armed
  bool  intrusion = false;
} state;

bool intrusionEmailSent = false;
unsigned long lastIntrusionTime = 0;
int wrongPinCount = 0;             // count of $PNO seen recently
unsigned long lastPnoTime = 0;
bool hotAlertEmailSent = false;
bool humAlertEmailSent = false;

// Latest activity log received from the PIC (8 event types + head pointer)
String lastLogReport = "";

void sendAlertEmail(const String& title, const String& reason,
                    const String& color1, const String& color2,
                    const String& actionText);
void sendIntrusionEmail(const String& trigger);

struct Event { String when; String type; String msg; };
const int MAX_EVENTS = 20;
Event events[MAX_EVENTS];
int eventCount = 0;

// =========================================================================
// TIME HELPERS
// =========================================================================
String currentTimeStr() {
  struct tm t;
  if (!getLocalTime(&t, 100)) return "--:--:--";
  char b[16];
  strftime(b, sizeof(b), "%H:%M:%S", &t);
  return String(b);
}
String currentDateTimeStr() {
  struct tm t;
  if (!getLocalTime(&t, 100)) return "unknown time";
  char b[32];
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t);
  return String(b);
}

// =========================================================================
// EVENT LOG
// =========================================================================
void addEvent(const String& type, const String& msg) {
  if (eventCount >= MAX_EVENTS) {
    for (int i = 1; i < MAX_EVENTS; i++) events[i-1] = events[i];
    eventCount = MAX_EVENTS - 1;
  }
  events[eventCount++] = {currentTimeStr(), type, msg};
}

// =========================================================================
// EMAIL via Gmail SMTP
// =========================================================================
void smtpCallback(SMTP_Status status) {
  Serial.printf("[SMTP] %s\n", status.info());
}

void sendIntrusionEmail(const String& trigger) {
  sendAlertEmail("INTRUSION DETECTED", trigger, "#dc2626", "#991b1b",
                 "Check the smart home dashboard or visit the property to confirm.");
}

void sendAlertEmail(const String& title, const String& reason,
                    const String& color1, const String& color2,
                    const String& actionText) {
  Serial.println("[EMAIL] Preparing alert: " + title);

  Session_Config config;
  config.server.host_name = SMTP_HOST;
  config.server.port      = SMTP_PORT;
  config.login.email      = EMAIL_SENDER;
  config.login.password   = EMAIL_PASSWORD;
  config.login.user_domain = "";
  config.time.ntp_server   = NTP_SERVER;
  config.time.gmt_offset   = 2;
  config.time.day_light_offset = 0;

  SMTP_Message message;
  message.sender.name  = "Smart Home System";
  message.sender.email = EMAIL_SENDER;
  message.subject      = "[ALARM] " + title + " at Smart Home";
  message.addRecipient("Owner", EMAIL_RECIPIENT);

  String body  = "<div style='font-family:Arial,sans-serif;background:#f4f4f4;padding:20px'>";
  body += "<div style='max-width:520px;margin:auto;background:white;border-radius:12px;overflow:hidden;box-shadow:0 4px 20px rgba(0,0,0,0.1)'>";
  body += "<div style='background:linear-gradient(135deg," + color1 + "," + color2 + ");color:white;padding:24px;text-align:center'>";
  body += "<h1 style='margin:0;font-size:24px'>&#x1F6A8; " + title + " &#x1F6A8;</h1>";
  body += "<p style='margin:8px 0 0;opacity:0.9'>Smart Home Alert System</p>";
  body += "</div>";
  body += "<div style='padding:24px;color:#1f2937'>";
  body += "<p><strong>Time:</strong> " + currentDateTimeStr() + "</p>";
  body += "<p><strong>Cause:</strong> " + reason + "</p>";
  body += "<h3 style='margin-top:24px;color:#1f3a5f'>Current Sensor Readings</h3>";
  body += "<table style='width:100%;border-collapse:collapse'>";
  body += "<tr><td style='padding:8px;border-bottom:1px solid #e5e7eb'>&#x1F321; Temperature</td><td style='padding:8px;border-bottom:1px solid #e5e7eb'><strong>" + String(state.temp) + " &deg;C</strong></td></tr>";
  body += "<tr><td style='padding:8px;border-bottom:1px solid #e5e7eb'>&#x1F4A7; Humidity</td><td style='padding:8px;border-bottom:1px solid #e5e7eb'><strong>" + String(state.hum) + " %</strong></td></tr>";
  body += "<tr><td style='padding:8px;border-bottom:1px solid #e5e7eb'>&#x1F4A1; Light level</td><td style='padding:8px;border-bottom:1px solid #e5e7eb'><strong>" + String(state.light) + " %</strong></td></tr>";
  body += "<tr><td style='padding:8px;border-bottom:1px solid #e5e7eb'>&#x1F3E0; Mode</td><td style='padding:8px;border-bottom:1px solid #e5e7eb'><strong>" + String(state.mode ? "AWAY" : "HOME") + "</strong></td></tr>";
  body += "<tr><td style='padding:8px'>&#x1F6AA; Door</td><td style='padding:8px'><strong>" + String(state.door ? "UNLOCKED" : "LOCKED") + "</strong></td></tr>";
  body += "</table>";
  body += "<p style='margin-top:24px;padding:16px;background:#fef2f2;border-left:4px solid " + color1 + ";color:#7f1d1d'>";
  body += "<strong>Action required:</strong> " + actionText;
  body += "</p>";
  body += "<p style='margin-top:16px;font-size:12px;color:#9ca3af;text-align:center'>Smart Home Automation System &mdash; Abdelrahman Emad 235338</p>";
  body += "</div></div></div>";

  message.html.content = body.c_str();
  message.html.charSet = "utf-8";
  message.html.transfer_encoding = "quoted-printable";
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_high;

  smtp.callback(smtpCallback);

  if (!smtp.connect(&config)) {
    Serial.printf("[EMAIL] Connection error: %s\n", smtp.errorReason().c_str());
    return;
  }
  if (!MailClient.sendMail(&smtp, &message, true)) {
    Serial.printf("[EMAIL] Send error: %s\n", smtp.errorReason().c_str());
  } else {
    Serial.println("[EMAIL] Sent successfully");
    addEvent("ALERT", "Intrusion email sent to owner");
  }
}

// =========================================================================
// UART — communicate with PIC
// =========================================================================
void sendToPIC(const String& cmd) {
  PicSerial.print(cmd);
  PicSerial.print("\r\n");
  Serial.printf("[PIC<-] %s\n", cmd.c_str());
}

void parseStatusFrame(const String& line) {
  if (!line.startsWith("$S,")) return;
  int vals[11] = {0};
  int idx = 0;
  int start = 3;
  for (int i = 3; i <= (int)line.length() && idx < 11; i++) {
    if (i == (int)line.length() || line[i] == ',') {
      vals[idx++] = line.substring(start, i).toInt();
      start = i + 1;
    }
  }
  if (idx >= 10) {       // PIC sends 10 fields; intrusion is internal-only
    int prevMode = state.mode;
    int prevArmed = state.armed;
    state.light = vals[0];
    state.temp  = vals[1];
    state.hum   = vals[2];
    state.mode  = vals[3];
    state.door  = vals[4];
    state.fan   = vals[5];
    state.pir   = vals[6];
    state.led   = vals[7];
    state.ctrl  = vals[8];
    state.armed = vals[9];
    // Reset alert latches when values return to normal so future spikes re-send
    if (hotAlertEmailSent && state.temp < 38) hotAlertEmailSent = false;
    if (humAlertEmailSent && state.hum < 85)  humAlertEmailSent = false;
    if (prevMode != state.mode) addEvent("MODE", state.mode ? "AWAY mode" : "HOME mode");
    if (prevArmed != state.armed) {
      addEvent("ARMED", state.armed ? "Alarm armed" : "Alarm disarmed");
      if (!state.armed) {
        intrusionEmailSent = false;
        state.intrusion = false;
      }
    }
  }
}

void handlePicLine(const String& line) {
  if (line.startsWith("$S,")) {
    parseStatusFrame(line);
  } else if (line.startsWith("!INTRUDE")) {
    state.intrusion = true;
    // Determine the real trigger reason:
    //   - If we received 3+ $PNO messages within the last 60 seconds -> wrong PINs
    //   - Otherwise -> PIR motion (the alarm was armed and PIR fired)
    String triggerReason;
    unsigned long now = millis();
    if (wrongPinCount >= 3 && (now - lastPnoTime) < 60000) {
      triggerReason = "Three consecutive wrong PIN attempts";
    } else {
      triggerReason = "PIR motion sensor (movement detected while armed)";
    }
    addEvent("INTRUSION", triggerReason);
    if (!intrusionEmailSent) {
      intrusionEmailSent = true;
      lastIntrusionTime = now;
      sendIntrusionEmail(triggerReason);
    }
    // Reset wrong-pin counter after intrusion
    wrongPinCount = 0;
  } else if (line.startsWith("!HOTALERT")) {
    addEvent("ALERT", "Critical temperature");
    if (!hotAlertEmailSent) {
      hotAlertEmailSent = true;
      sendAlertEmail("HIGH TEMPERATURE WARNING",
                     "Temperature has reached or exceeded 40 degrees C",
                     "#ea580c", "#9a3412",
                     "Check the property - possible fire risk or HVAC failure.");
    }
  } else if (line.startsWith("!HUMALERT")) {
    addEvent("ALERT", "Critical humidity");
    if (!humAlertEmailSent) {
      humAlertEmailSent = true;
      sendAlertEmail("HIGH HUMIDITY WARNING",
                     "Humidity has reached or exceeded 90 percent",
                     "#2563eb", "#1e40af",
                     "Possible water leak, flooding, or ventilation failure.");
    }
  } else if (line.startsWith("$R,")) {
    lastLogReport = line;
    addEvent("REPORT", "Activity log received from PIC");
    Serial.println("[REPORT] " + line);
  } else if (line.startsWith("$POK")) {
    addEvent("PIN", "PIN accepted");
    wrongPinCount = 0;          // success resets the counter
  } else if (line.startsWith("$PNO")) {
    addEvent("PIN", "PIN rejected");
    wrongPinCount++;
    lastPnoTime = millis();
  }
}

String picLineBuf = "";
void pumpPicUart() {
  while (PicSerial.available()) {
    char c = PicSerial.read();
    if (c == '\n' || c == '\r') {
      if (picLineBuf.length() > 0) {
        Serial.printf("[PIC->] %s\n", picLineBuf.c_str());
        handlePicLine(picLineBuf);
        picLineBuf = "";
      }
    } else {
      picLineBuf += c;
      if (picLineBuf.length() > 120) picLineBuf = "";
    }
  }
}

// =========================================================================
// DASHBOARD HTML  (browser-side voice parsing happens here)
// =========================================================================
const char DASHBOARD_HTML[] PROGMEM =
  "<!DOCTYPE html>\n"
  "<html lang=\"en\"><head>\n"
  "<meta charset=\"UTF-8\">\n"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
  "<title>Smart Home Dashboard</title>\n"
  "<style>\n"
  "*{box-sizing:border-box;margin:0;padding:0}\n"
  "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,sans-serif;background:#0a0e1a;color:#e5e7eb;min-height:100vh;padding:20px}\n"
  ".container{max-width:1400px;margin:auto}\n"
  "header{display:flex;justify-content:space-between;align-items:center;background:#111827;padding:18px 24px;border-radius:14px;box-shadow:0 4px 24px rgba(0,0,0,0.4);margin-bottom:20px;flex-wrap:wrap;gap:12px}\n"
  "header h1{font-size:20px;font-weight:700}\n"
  "header .right{display:flex;gap:14px;align-items:center;flex-wrap:wrap}\n"
  ".armed-badge{padding:6px 14px;border-radius:20px;font-size:13px;font-weight:600}\n"
  ".armed-on{background:#dc2626;color:white}\n"
  ".armed-off{background:#374151;color:#9ca3af}\n"
  ".mute-btn{background:#1f2937;color:#e5e7eb;border:none;padding:8px 12px;border-radius:8px;cursor:pointer;font-size:14px}\n"
  ".mode-toggle{display:flex;background:#1f2937;border-radius:10px;padding:4px}\n"
  ".mode-toggle button{padding:8px 16px;background:transparent;color:#9ca3af;border:none;border-radius:6px;cursor:pointer;font-weight:600;transition:all 0.2s}\n"
  ".mode-toggle button.active.auto{background:#16a34a;color:white}\n"
  ".mode-toggle button.active.manual{background:#2563eb;color:white}\n"
  ".mode-toggle button.active.voice{background:#9333ea;color:white}\n"
  ".global-btns{display:flex;gap:8px}\n"
  ".global-btns button{padding:8px 16px;background:#1f2937;color:#e5e7eb;border:none;border-radius:8px;cursor:pointer;font-weight:600}\n"
  ".global-btns button.active{background:#2563eb}\n"
  ".grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:20px}\n"
  "@media (max-width:1100px){.grid{grid-template-columns:1fr}}\n"
  ".panel{background:#111827;border-radius:14px;padding:20px;box-shadow:0 4px 20px rgba(0,0,0,0.3)}\n"
  ".panel.disabled{opacity:0.4;pointer-events:none}\n"
  ".panel h2{font-size:16px;margin-bottom:16px;color:#9ca3af;font-weight:600}\n"
  ".stat{display:flex;justify-content:space-between;align-items:center;padding:12px 0;border-bottom:1px solid #1f2937}\n"
  ".stat:last-child{border-bottom:none}\n"
  ".stat .label{color:#9ca3af;font-size:13px}\n"
  ".stat .value{font-size:22px;font-weight:700}\n"
  ".devices{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:14px}\n"
  ".device{background:#1f2937;padding:14px;border-radius:10px;text-align:center}\n"
  ".device svg{width:36px;height:36px;margin-bottom:6px}\n"
  ".device .name{font-size:12px;color:#9ca3af}\n"
  ".device .state{font-size:14px;font-weight:700;margin-top:2px}\n"
  ".device.on .state{color:#22c55e}\n"
  ".device.off .state{color:#6b7280}\n"
  ".btn{padding:12px 20px;background:#2563eb;color:white;border:none;border-radius:10px;cursor:pointer;font-weight:600;width:100%;margin-bottom:10px}\n"
  ".btn:hover{opacity:0.85}\n"
  ".btn-secondary{background:#374151}\n"
  ".btn-danger{background:#dc2626}\n"
  ".btn-success{background:#16a34a}\n"
  ".slider{width:100%;margin:14px 0}\n"
  ".slider-row{display:flex;align-items:center;gap:10px}\n"
  ".slider-row .val{width:42px;text-align:right;font-weight:700}\n"
  ".mic-btn{width:80px;height:80px;border-radius:50%;background:#9333ea;color:white;border:none;cursor:pointer;font-size:32px;box-shadow:0 0 30px rgba(147,51,234,0.5);margin:10px auto;display:block}\n"
  ".mic-btn.listening{background:#dc2626;animation:pulse 1s infinite;box-shadow:0 0 40px rgba(220,38,38,0.7)}\n"
  "@keyframes pulse{0%,100%{transform:scale(1)}50%{transform:scale(1.1)}}\n"
  ".transcript{background:#0a0e1a;border-radius:10px;padding:14px;margin-top:10px;font-style:italic;color:#9ca3af;min-height:50px}\n"
  ".transcript.user{color:#60a5fa;font-style:normal}\n"
  ".assistant-reply{background:#0a0e1a;border-radius:10px;padding:14px;margin-top:10px;color:#22c55e;min-height:50px}\n"
  ".log{max-height:240px;overflow-y:auto;margin-top:14px}\n"
  ".log-entry{display:flex;gap:10px;padding:6px 0;font-size:12px;border-bottom:1px solid #1f2937}\n"
  ".log-entry .t{color:#6b7280;min-width:60px}\n"
  ".log-entry .tag{padding:1px 6px;border-radius:4px;font-weight:600;font-size:10px}\n"
  ".log-entry .tag.INTRUSION{background:#dc2626;color:white}\n"
  ".log-entry .tag.PIN{background:#9333ea;color:white}\n"
  ".log-entry .tag.MODE{background:#2563eb;color:white}\n"
  ".log-entry .tag.ARMED{background:#f59e0b;color:white}\n"
  ".log-entry .tag.VOICE{background:#16a34a;color:white}\n"
  ".log-entry .tag.ALERT{background:#ef4444;color:white}\n"
  ".log-entry .tag.BOOT{background:#6b7280;color:white}\n"
  ".intrusion-banner{position:fixed;top:0;left:0;right:0;background:linear-gradient(90deg,#dc2626,#991b1b);color:white;text-align:center;padding:14px;font-weight:700;z-index:1000;box-shadow:0 4px 20px rgba(220,38,38,0.5);animation:flash 1s infinite}\n"
  "@keyframes flash{0%,100%{opacity:1}50%{opacity:0.7}}\n"
  ".pin-modal-bg{position:fixed;inset:0;background:rgba(0,0,0,0.7);z-index:2000;display:none;align-items:center;justify-content:center}\n"
  ".pin-modal-bg.show{display:flex}\n"
  ".pin-modal{background:#111827;padding:30px;border-radius:14px;min-width:300px}\n"
  ".pin-modal h3{margin-bottom:16px;color:#e5e7eb}\n"
  ".pin-display{font-size:32px;text-align:center;letter-spacing:14px;margin:20px 0;color:#60a5fa;font-weight:700}\n"
  ".pin-pad{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}\n"
  ".pin-pad button{padding:18px;background:#1f2937;color:white;border:none;border-radius:10px;cursor:pointer;font-size:18px;font-weight:600}\n"
  ".pin-pad button:hover{background:#374151}\n"
  ".chart-wrap{background:#0a0e1a;border-radius:10px;padding:14px;margin-top:14px}\n"
  ".chart-legend{display:flex;justify-content:center;gap:18px;margin-bottom:8px;font-size:11px;color:#9ca3af}\n"
  ".legend-item{display:flex;align-items:center;gap:6px}\n"
  ".legend-dot{display:inline-block;width:10px;height:10px;border-radius:50%}\n"
  "canvas{width:100%;height:160px}\n"
  "\n"
  "/* Tile hover/animation */\n"
  ".device{transition:transform 0.18s ease, box-shadow 0.18s ease, background 0.18s ease;cursor:pointer}\n"
  ".device:hover{transform:translateY(-3px) scale(1.03);box-shadow:0 6px 20px rgba(59,130,246,0.25);background:#263042}\n"
  ".device.on{animation:tilePulse 1.2s ease-in-out infinite alternate}\n"
  "@keyframes tilePulse{0%{box-shadow:0 0 0 rgba(34,197,94,0)}100%{box-shadow:0 0 24px rgba(34,197,94,0.35)}}\n"
  ".device.on svg{filter:drop-shadow(0 0 6px currentColor)}\n"
  "\n"
  "/* Door icon swing when unlocking */\n"
  "#dev-door.on svg{animation:doorSwing 0.6s ease-out}\n"
  "@keyframes doorSwing{0%{transform:rotateY(0)}50%{transform:rotateY(-25deg)}100%{transform:rotateY(0)}}\n"
  "\n"
  "/* Bulb glow */\n"
  "#dev-bulb.on svg{color:#fbbf24;animation:bulbGlow 2s ease-in-out infinite alternate}\n"
  "@keyframes bulbGlow{0%{filter:drop-shadow(0 0 4px #fbbf24)}100%{filter:drop-shadow(0 0 14px #fbbf24)}}\n"
  "\n"
  "/* Fan spin */\n"
  "#dev-fan.on svg{animation:fanSpin 0.8s linear infinite}\n"
  "@keyframes fanSpin{from{transform:rotate(0)}to{transform:rotate(360deg)}}\n"
  "\n"
  "/* Motion blink */\n"
  "#dev-pir.on svg{animation:motionBlink 0.6s ease-in-out infinite alternate;color:#22c55e}\n"
  "@keyframes motionBlink{0%{opacity:0.4}100%{opacity:1}}\n"
  "\n"
  "/* Button shimmer on hover */\n"
  ".btn{transition:transform 0.15s ease, box-shadow 0.15s ease, opacity 0.2s}\n"
  ".btn:hover{transform:translateY(-2px);box-shadow:0 4px 14px rgba(37,99,235,0.4);opacity:0.95}\n"
  ".btn:active{transform:translateY(0);box-shadow:none}\n"
  "\n"
  "/* Quick commands styling */\n"
  ".qc-title{font-size:13px;color:#e5e7eb;margin:18px 0 4px;font-weight:600}\n"
  ".qc-sub{font-size:11px;color:#9ca3af;margin-bottom:10px}\n"
  ".qc-section{font-size:11px;color:#6b7280;margin:12px 0 6px;text-transform:uppercase;letter-spacing:1px;font-weight:600}\n"
  ".qc-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-bottom:8px}\n"
  ".qc-btn{padding:8px 6px;background:#1f2937;color:#e5e7eb;border:none;border-radius:8px;cursor:pointer;font-size:11px;font-weight:600;transition:all 0.18s}\n"
  ".qc-btn:hover{background:#374151;transform:translateY(-1px);box-shadow:0 4px 12px rgba(0,0,0,0.3)}\n"
  ".qc-night{background:#312e81;color:#c4b5fd}\n"
  ".qc-night:hover{background:#3730a3}\n"
  ".qc-relax{background:#064e3b;color:#86efac}\n"
  ".qc-relax:hover{background:#065f46}\n"
  ".qc-home{background:#0c4a6e;color:#7dd3fc}\n"
  ".qc-home:hover{background:#0369a1}\n"
  ".qc-away{background:#7c2d12;color:#fdba74}\n"
  ".qc-away:hover{background:#9a3412}\n"
  ".qc-party{background:#831843;color:#f9a8d4}\n"
  ".qc-party:hover{background:#9d174d}\n"
  ".qc-movie{background:#374151;color:#d1d5db}\n"
  ".qc-movie:hover{background:#4b5563}\n"
  ".qc-details{margin-top:14px;background:#0a0e1a;border-radius:8px;padding:10px;cursor:pointer}\n"
  ".qc-details summary{font-size:12px;color:#9ca3af;font-weight:600;outline:none}\n"
  ".qc-details[open] summary{color:#e5e7eb;margin-bottom:8px}\n"
  ".qc-help{font-size:11px;color:#9ca3af;line-height:1.7}\n"
  "\n"
  "/* Action toast */\n"
  ".toast{position:fixed;bottom:30px;left:50%;transform:translateX(-50%) translateY(20px);background:#1f2937;color:#e5e7eb;padding:12px 20px;border-radius:30px;box-shadow:0 8px 30px rgba(0,0,0,0.4);font-weight:600;font-size:13px;z-index:1500;opacity:0;transition:opacity 0.3s,transform 0.3s;pointer-events:none}\n"
  ".toast.show{opacity:1;transform:translateX(-50%) translateY(0)}\n"
  ".toast.success{border-left:4px solid #22c55e}\n"
  ".toast.error{border-left:4px solid #ef4444}\n"
  ".toast.info{border-left:4px solid #3b82f6}\n"
  "/* Music indicator (floating bottom-right) */\n"
  ".music-indicator{position:fixed;bottom:20px;right:20px;background:#1f2937;color:#e5e7eb;padding:10px 16px;border-radius:30px;box-shadow:0 4px 16px rgba(0,0,0,0.3);font-size:12px;font-weight:600;cursor:pointer;z-index:1400;display:none;align-items:center;gap:8px;border:1px solid #374151}\n"
  ".music-indicator.show{display:flex}\n"
  ".music-indicator .bars{display:flex;gap:2px;align-items:end;height:14px}\n"
  ".music-indicator .bar{width:3px;background:#22c55e;border-radius:2px;animation:musicBar 0.8s ease-in-out infinite alternate}\n"
  ".music-indicator .bar:nth-child(2){animation-delay:0.2s}\n"
  ".music-indicator .bar:nth-child(3){animation-delay:0.4s}\n"
  "@keyframes musicBar{from{height:4px}to{height:14px}}\n"
  "\n"
  "\n"
  "</style></head>\n"
  "<body>\n"
  "<div id=\"intrusion-banner\" class=\"intrusion-banner\" style=\"display:none\">&#x1F6A8; INTRUSION DETECTED &#x1F6A8;</div>\n"
  "<div id=\"toast\" class=\"toast\"></div>\n"
  "<div id=\"music-indicator\" class=\"music-indicator\" onclick=\"stopAllMusic()\">\n"
  "  <div class=\"bars\"><div class=\"bar\"></div><div class=\"bar\"></div><div class=\"bar\"></div></div>\n"
  "  <span id=\"music-label\">Ambient</span>\n"
  "  <span style=\"color:#9ca3af\">&times;</span>\n"
  "</div>\n"
  "<div id=\"log-modal-bg\" class=\"pin-modal-bg\">\n"
  "  <div class=\"pin-modal\" style=\"min-width:380px;max-width:520px\" id=\"log-modal-body\">\n"
  "    Loading...\n"
  "  </div>\n"
  "</div>\n"
  "<div id=\"pin-modal-bg\" class=\"pin-modal-bg\">\n"
  "  <div class=\"pin-modal\">\n"
  "    <h3 id=\"pin-title\">Enter PIN to Disarm</h3>\n"
  "    <div class=\"pin-display\" id=\"pin-display\">****</div>\n"
  "    <div class=\"pin-pad\">\n"
  "      <button onclick=\"pinKey('1')\">1</button><button onclick=\"pinKey('2')\">2</button><button onclick=\"pinKey('3')\">3</button>\n"
  "      <button onclick=\"pinKey('4')\">4</button><button onclick=\"pinKey('5')\">5</button><button onclick=\"pinKey('6')\">6</button>\n"
  "      <button onclick=\"pinKey('7')\">7</button><button onclick=\"pinKey('8')\">8</button><button onclick=\"pinKey('9')\">9</button>\n"
  "      <button onclick=\"pinClear()\" style=\"background:#dc2626\">CLR</button><button onclick=\"pinKey('0')\">0</button><button onclick=\"pinSubmit()\" style=\"background:#16a34a\">OK</button>\n"
  "    </div>\n"
  "    <button onclick=\"pinCancel()\" class=\"btn btn-secondary\" style=\"margin-top:14px\">Cancel</button>\n"
  "  </div>\n"
  "</div>\n"
  "<div class=\"container\">\n"
  "<header>\n"
  "  <h1>&#x1F3E0; Smart Home Dashboard</h1>\n"
  "  <div class=\"right\">\n"
  "    <div class=\"global-btns\">\n"
  "      <button id=\"btn-home\" onclick=\"setMode('H')\">Home</button>\n"
  "      <button id=\"btn-away\" onclick=\"setMode('A')\">Away</button>\n"
  "    </div>\n"
  "    <div class=\"mode-toggle\">\n"
  "      <button id=\"btn-auto\" class=\"active auto\" onclick=\"setCtrlMode('A')\">Auto</button>\n"
  "      <button id=\"btn-manual\" class=\"manual\" onclick=\"setCtrlMode('M')\">Manual</button>\n"
  "      <button id=\"btn-voice\" class=\"voice\" onclick=\"setCtrlMode('V')\">Voice</button>\n"
  "    </div>\n"
  "    <span id=\"armed-badge\" class=\"armed-badge armed-off\">DISARMED</span>\n"
  "    <button id=\"mute-btn\" class=\"mute-btn\" onclick=\"toggleMute()\">&#x1F50A;</button>\n"
  "  </div>\n"
  "</header>\n"
  "<div class=\"grid\">\n"
  "  <div class=\"panel\" id=\"panel-monitor\">\n"
  "    <h2>&#x1F4CA; Live Monitor</h2>\n"
  "    <div class=\"stat\"><span class=\"label\">&#x1F321; Temperature</span><span class=\"value\" id=\"v-temp\">--&deg;C</span></div>\n"
  "    <div class=\"stat\"><span class=\"label\">&#x1F4A7; Humidity</span><span class=\"value\" id=\"v-hum\">--%</span></div>\n"
  "    <div class=\"stat\"><span class=\"label\">&#x1F4A1; Light</span><span class=\"value\" id=\"v-light\">--%</span></div>\n"
  "    <div class=\"devices\">\n"
  "      <div class=\"device\" id=\"dev-door\" onclick=\"tileToggle('door')\"><svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M6 2v20h12V2H6zm10 12h-2v-2h2v2z\"/></svg><div class=\"name\">Door</div><div class=\"state\" id=\"dev-door-state\">LOCKED</div></div>\n"
  "      <div class=\"device\" id=\"dev-bulb\" onclick=\"tileToggle('bulb')\"><svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 2C7 2 5 7 5 10c0 2 1 4 3 5v3h8v-3c2-1 3-3 3-5 0-3-2-8-7-8zm-2 19h4v1h-4v-1z\"/></svg><div class=\"name\">Bulb</div><div class=\"state\" id=\"dev-bulb-state\">0%</div></div>\n"
  "      <div class=\"device\" id=\"dev-fan\" onclick=\"tileToggle('fan')\"><svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><circle cx=\"12\" cy=\"12\" r=\"3\"/><path d=\"M12 2c-1 0-2 4-2 7h4c0-3-1-7-2-7zm0 20c1 0 2-4 2-7h-4c0 3 1 7 2 7zM2 12c0 1 4 2 7 2v-4c-3 0-7 1-7 2zm20 0c0-1-4-2-7-2v4c3 0 7-1 7-2z\"/></svg><div class=\"name\">Fan</div><div class=\"state\" id=\"dev-fan-state\">OFF</div></div>\n"
  "      <div class=\"device\" id=\"dev-pir\"><svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><circle cx=\"12\" cy=\"12\" r=\"4\"/></svg><div class=\"name\">Motion</div><div class=\"state\" id=\"dev-pir-state\">CLEAR</div></div>\n"
  "    </div>\n"
  "    <div class=\"chart-wrap\">\n"
  "      <div class=\"chart-legend\">\n"
  "        <span class=\"legend-item\"><span class=\"legend-dot\" style=\"background:#ef4444\"></span>Temperature</span>\n"
  "        <span class=\"legend-item\"><span class=\"legend-dot\" style=\"background:#3b82f6\"></span>Humidity</span>\n"
  "        <span class=\"legend-item\"><span class=\"legend-dot\" style=\"background:#f59e0b\"></span>Light</span>\n"
  "      </div>\n"
  "      <canvas id=\"chart\"></canvas>\n"
  "    </div>\n"
  "    <h2 style=\"margin-top:18px\">Event Log</h2>\n"
  "    <div class=\"log\" id=\"event-log\"></div>\n"
  "  </div>\n"
  "  <div class=\"panel\" id=\"panel-manual\">\n"
  "    <h2>&#x1F39B; Manual Control</h2>\n"
  "    <div class=\"slider-row\"><span style=\"color:#9ca3af;font-size:13px\">&#x1F4A1;</span>\n"
  "      <input id=\"led-slider\" class=\"slider\" type=\"range\" min=\"0\" max=\"100\" value=\"0\">\n"
  "      <span id=\"led-val\" class=\"val\">0%</span>\n"
  "    </div>\n"
  "    <button class=\"btn\" onclick=\"sendCmd('D1')\">&#x1F513; Unlock Door</button>\n"
  "    <button class=\"btn btn-secondary\" onclick=\"sendCmd('D0')\">&#x1F512; Lock Door</button>\n"
  "    <button class=\"btn\" onclick=\"sendCmd('F1')\">&#x1F4A8; Fan ON</button>\n"
  "    <button class=\"btn btn-secondary\" onclick=\"sendCmd('F0')\">Fan OFF</button>\n"
  "    <button class=\"btn btn-success\" onclick=\"sendCmd('AA')\">Arm Alarm</button>\n"
  "    <button class=\"btn btn-danger\" onclick=\"askDisarmPin()\">Disarm Alarm</button>\n"
  "    <button class=\"btn btn-secondary\" onclick=\"showLog()\" style=\"margin-top:14px\">&#x1F4C2; View Activity Log (EEPROM)</button>\n"
  "  </div>\n"
  "  <div class=\"panel\" id=\"panel-voice\">\n"
  "    <h2>&#x1F3A4; Voice Assistant</h2>\n"
  "    <button id=\"mic-btn\" class=\"mic-btn\" onclick=\"toggleListening()\">&#x1F3A4;</button>\n"
  "    <div class=\"transcript\" id=\"transcript\">Tap the mic and speak a command</div>\n"
  "    <div class=\"assistant-reply\" id=\"assistant-reply\">&hellip;</div>\n"
  "\n"
  "    <h3 class=\"qc-title\">&#x1F4DC; Quick Commands</h3>\n"
  "    <p class=\"qc-sub\">Click a command to execute, or say it out loud</p>\n"
  "\n"
  "    <div class=\"qc-section\">&#x1F319; Scenes (macros)</div>\n"
  "    <div class=\"qc-grid\">\n"
  "      <button class=\"qc-btn qc-night\" onclick=\"runMacro('goodnight')\">&#x1F319; Goodnight</button>\n"
  "      <button class=\"qc-btn qc-relax\" onclick=\"runMacro('relax')\">&#x1F36C; Relax</button>\n"
  "      <button class=\"qc-btn qc-home\" onclick=\"askDisarmPin('welcome')\">&#x1F3E0; I&#39;m Home</button>\n"
  "      <button class=\"qc-btn qc-away\" onclick=\"runMacro('leaving')\">&#x1F6AA; Leaving</button>\n"
  "      <button class=\"qc-btn qc-party\" onclick=\"runMacro('party')\">&#x1F389; Party</button>\n"
  "      <button class=\"qc-btn qc-movie\" onclick=\"runMacro('movie')\">&#x1F3AC; Movie</button>\n"
  "    </div>\n"
  "\n"
  "    <div class=\"qc-section\">&#x1F4A1; Lighting</div>\n"
  "    <div class=\"qc-grid\">\n"
  "      <button class=\"qc-btn\" onclick=\"quickCmd('L000','Lights off')\">Off</button>\n"
  "      <button class=\"qc-btn\" onclick=\"quickCmd('L025','Lights at twenty five percent')\">25%</button>\n"
  "      <button class=\"qc-btn\" onclick=\"quickCmd('L050','Lights at fifty percent')\">50%</button>\n"
  "      <button class=\"qc-btn\" onclick=\"quickCmd('L075','Lights at seventy five percent')\">75%</button>\n"
  "      <button class=\"qc-btn\" onclick=\"quickCmd('L100','Lights at maximum')\">100%</button>\n"
  "    </div>\n"
  "\n"
  "    <div class=\"qc-section\">&#x1F50E; Sensor Queries</div>\n"
  "    <div class=\"qc-grid\">\n"
  "      <button class=\"qc-btn\" onclick=\"speakQuery('temp')\">&#x1F321; Temp?</button>\n"
  "      <button class=\"qc-btn\" onclick=\"speakQuery('hum')\">&#x1F4A7; Humidity?</button>\n"
  "      <button class=\"qc-btn\" onclick=\"speakQuery('light')\">&#x1F4A1; Light?</button>\n"
  "      <button class=\"qc-btn\" onclick=\"speakQuery('door')\">&#x1F6AA; Door?</button>\n"
  "      <button class=\"qc-btn\" onclick=\"speakQuery('alarm')\">&#x1F6A8; Alarm?</button>\n"
  "      <button class=\"qc-btn\" onclick=\"speakQuery('full')\">&#x1F4CA; Full Status</button>\n"
  "    </div>\n"
  "\n"
  "    <details class=\"qc-details\">\n"
  "      <summary>&#x2753; Full voice command reference</summary>\n"
  "      <div class=\"qc-help\">\n"
  "        <strong>Scenes:</strong> \"goodnight\", \"relax mode\", \"party mode\", \"movie mode\", \"I'm leaving\", \"I'm home\"<br/>\n"
  "        <strong>Lighting:</strong> \"lights on/off\", \"lights at 40 percent\", \"dim\", \"lights medium\", \"lights maximum\", \"brighten\"<br/>\n"
  "        <strong>Door:</strong> \"lock the door\", \"unlock the door\", \"open door\", \"close door\", \"let me in\"<br/>\n"
  "        <strong>Fan:</strong> \"fan on/off\", \"I'm hot\", \"I'm cold\", \"cool me down\", \"feeling warm\"<br/>\n"
  "        <strong>Alarm:</strong> \"arm the alarm\", \"disarm\", \"secure the house\", \"lock down\"<br/>\n"
  "        <strong>Mode:</strong> \"home mode\", \"away mode\", \"auto mode\" (reset), \"I'm back\", \"going out\"<br/>\n"
  "        <strong>Queries:</strong> \"what is the temperature\", \"humidity\", \"light level\", \"door status\", \"status report\", \"what mode\"<br/>\n"
  "        <strong>Music:</strong> \"play music\", \"stop music\", \"ambient music\"<br/>\n"
  "        <strong>Info:</strong> \"what time is it\", \"who are you\", \"view log\", \"help\"<br/>\n"
  "        <strong>Fun:</strong> \"thank you\", \"hello\"<br/>\n"
  "        <strong>PIN:</strong> say each digit: \"one two three four\"<br/>\n"
  "        <strong>Stop:</strong> \"stop\", \"be quiet\", \"silence\"<br/>\n"
  "      </div>\n"
  "    </details>\n"
  "  </div>\n"
  "</div>\n"
  "</div>\n"
  "<script>\n"
  "let state={light:0,temp:0,hum:0,mode:0,door:0,fan:0,pir:0,led:0,ctrl:0,armed:0,intrusion:false};\n"
  "let muted=false, history_=[], lastModeAnnounce=-1, lastArmedAnnounce=-1, lastFanAnnounce=-1;\n"
  "let pinBuffer=\"\", pinReason=\"disarm\";\n"
  "\n"
  "async function poll(){\n"
  "  try{\n"
  "    const r=await fetch('/status');\n"
  "    const data=await r.json();\n"
  "    Object.assign(state,data);\n"
  "    updateUI();\n"
  "    if(data.events) updateLog(data.events);\n"
  "  }catch(e){console.error(e);}\n"
  "}\n"
  "setInterval(poll,1000); poll();\n"
  "\n"
  "function updateUI(){\n"
  "  document.getElementById('v-temp').textContent=state.temp+'\\u00B0C';\n"
  "  document.getElementById('v-hum').textContent=state.hum+'%';\n"
  "  document.getElementById('v-light').textContent=state.light+'%';\n"
  "  document.getElementById('dev-door').className='device '+(state.door?'on':'off');\n"
  "  document.getElementById('dev-door-state').textContent=state.door?'UNLOCKED':'LOCKED';\n"
  "  document.getElementById('dev-bulb').className='device '+(state.led>0?'on':'off');\n"
  "  document.getElementById('dev-bulb-state').textContent=state.led+'%';\n"
  "  document.getElementById('dev-fan').className='device '+(state.fan?'on':'off');\n"
  "  document.getElementById('dev-fan-state').textContent=state.fan?'ON':'OFF';\n"
  "  document.getElementById('dev-pir').className='device '+(state.pir?'on':'off');\n"
  "  document.getElementById('dev-pir-state').textContent=state.pir?'MOTION':'CLEAR';\n"
  "  const badge=document.getElementById('armed-badge');\n"
  "  badge.className='armed-badge '+(state.armed?'armed-on':'armed-off');\n"
  "  badge.textContent=state.armed?'ARMED':'DISARMED';\n"
  "  document.getElementById('btn-home').className=state.mode===0?'active':'';\n"
  "  document.getElementById('btn-away').className=state.mode===1?'active':'';\n"
  "  document.getElementById('btn-auto').classList.toggle('active',state.ctrl===0);\n"
  "  document.getElementById('btn-manual').classList.toggle('active',state.ctrl===1);\n"
  "  document.getElementById('btn-voice').classList.toggle('active',state.ctrl===2);\n"
  "  document.getElementById('panel-manual').classList.toggle('disabled',state.ctrl!==1);\n"
  "  // Voice panel is always usable; the mic must always be clickable\n"
  "  document.getElementById('panel-voice').classList.remove('disabled');\n"
  "  if(state.ctrl===1){\n"
  "    document.getElementById('led-slider').value=state.led;\n"
  "    document.getElementById('led-val').textContent=state.led+'%';\n"
  "  }\n"
  "  document.getElementById('intrusion-banner').style.display=state.intrusion?'block':'none';\n"
  "  if(lastModeAnnounce!==-1 && lastModeAnnounce!==state.mode){\n"
  "    speak(state.mode?'Away mode activated':'Welcome home');\n"
  "  }\n"
  "  lastModeAnnounce=state.mode;\n"
  "  if(lastArmedAnnounce!==-1 && lastArmedAnnounce!==state.armed){\n"
  "    speak(state.armed?'Alarm armed':'Alarm disarmed');\n"
  "  }\n"
  "  lastArmedAnnounce=state.armed;\n"
  "\n"
  "  if(lastFanAnnounce!==-1 && lastFanAnnounce!==state.fan && state.ctrl===0){\n"
  "    speak(state.fan?'Fan turning on':'Fan turning off');\n"
  "  }\n"
  "  lastFanAnnounce=state.fan;\n"
  "  const now=Date.now();\n"
  "  history_.push({t:now,light:state.light,temp:state.temp,hum:state.hum});\n"
  "  history_=history_.filter(p=>now-p.t<60000);\n"
  "  drawChart();\n"
  "}\n"
  "\n"
  "function updateLog(events){\n"
  "  const log=document.getElementById('event-log');\n"
  "  log.innerHTML=events.map(e=>`<div class=\"log-entry\"><span class=\"t\">${e.when}</span><span class=\"tag ${e.type}\">${e.type}</span><span>${e.msg}</span></div>`).reverse().join('');\n"
  "}\n"
  "\n"
  "function drawChart(){\n"
  "  const canvas=document.getElementById('chart');\n"
  "  const ctx=canvas.getContext('2d');\n"
  "  canvas.width=canvas.offsetWidth*devicePixelRatio;\n"
  "  canvas.height=canvas.offsetHeight*devicePixelRatio;\n"
  "  ctx.scale(devicePixelRatio,devicePixelRatio);\n"
  "  const W=canvas.offsetWidth, H=canvas.offsetHeight;\n"
  "  ctx.clearRect(0,0,W,H);\n"
  "  if(history_.length<2) return;\n"
  "  const now=Date.now();\n"
  "  const drawSeries=(key,color)=>{\n"
  "    ctx.strokeStyle=color; ctx.lineWidth=2; ctx.beginPath();\n"
  "    history_.forEach((p,i)=>{\n"
  "      const x=((p.t-(now-60000))/60000)*W;\n"
  "      const y=H-(p[key]/100)*H;\n"
  "      if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);\n"
  "    });\n"
  "    ctx.stroke();\n"
  "  };\n"
  "  drawSeries('temp','#ef4444');\n"
  "  drawSeries('hum','#3b82f6');\n"
  "  drawSeries('light','#f59e0b');\n"
  "}\n"
  "\n"
  "async function sendCmd(cmd){\n"
  "  try{await fetch('/cmd?c='+encodeURIComponent(cmd));}catch(e){console.error(e);}\n"
  "}\n"
  "function setMode(m){sendCmd('M'+m);}\n"
  "function setCtrlMode(m){\n"
  "  if(m==='A'){\n"
  "    // Switching to AUTO - stop scene music and clear PIC overrides\n"
  "    stopAllMusic();\n"
  "    sendCmd('X');\n"
  "    toast('Auto mode - scenes cleared','info');\n"
  "  }\n"
  "  sendCmd('Y'+m);\n"
  "}\n"
  "function toggleMute(){\n"
  "  muted=!muted;\n"
  "  document.getElementById('mute-btn').textContent=muted?'\\u{1F507}':'\\u{1F50A}';\n"
  "  if(muted) window.speechSynthesis.cancel();\n"
  "}\n"
  "\n"
  "document.getElementById('led-slider').addEventListener('input',e=>{\n"
  "  const v=parseInt(e.target.value);\n"
  "  document.getElementById('led-val').textContent=v+'%';\n"
  "  sendCmd('L'+String(v).padStart(3,'0'));\n"
  "});\n"
  "\n"
  "function askDisarmPin(reason){\n"
  "  pinBuffer=\"\"; pinReason=reason||\"disarm\";\n"
  "  document.getElementById('pin-title').textContent=pinReason==='disarm'?'Enter PIN to Disarm':'Enter PIN';\n"
  "  document.getElementById('pin-display').textContent='****';\n"
  "  document.getElementById('pin-modal-bg').classList.add('show');\n"
  "}\n"
  "function pinKey(d){\n"
  "  if(pinBuffer.length<4){\n"
  "    pinBuffer+=d;\n"
  "    let disp=pinBuffer+'*'.repeat(4-pinBuffer.length);\n"
  "    document.getElementById('pin-display').textContent=disp;\n"
  "    if(pinBuffer.length===4) setTimeout(pinSubmit,150);\n"
  "  }\n"
  "}\n"
  "function pinClear(){pinBuffer=\"\";document.getElementById('pin-display').textContent='****';}\n"
  "function pinCancel(){pinBuffer=\"\";document.getElementById('pin-modal-bg').classList.remove('show');}\n"
  "async function pinSubmit(){\n"
  "  if(pinBuffer.length!==4) return;\n"
  "  await fetch('/pin?v='+pinBuffer);\n"
  "  document.getElementById('pin-modal-bg').classList.remove('show');\n"
  "}\n"
  "\n"
  "const SR=window.SpeechRecognition||window.webkitSpeechRecognition;\n"
  "let recognition=null, listening=false;\n"
  "if(SR){\n"
  "  recognition=new SR();\n"
  "  recognition.continuous=false;\n"
  "  recognition.interimResults=false;\n"
  "  recognition.lang='en-US';\n"
  "  recognition.onresult=e=>{\n"
  "    const txt=e.results[0][0].transcript.toLowerCase().trim();\n"
  "    document.getElementById('transcript').textContent='\"'+txt+'\"';\n"
  "    document.getElementById('transcript').className='transcript user';\n"
  "    handleVoice(txt);\n"
  "  };\n"
  "  recognition.onend=()=>{listening=false;document.getElementById('mic-btn').classList.remove('listening');};\n"
  "  recognition.onerror=()=>{listening=false;document.getElementById('mic-btn').classList.remove('listening');speak(\"Sorry I couldn't hear you, try again\");};\n"
  "}\n"
  "function toggleListening(){\n"
  "  if(!recognition){alert(\"Your browser doesn't support voice recognition. Use Chrome on a laptop.\");return;}\n"
  "  if(listening){recognition.stop();listening=false;document.getElementById('mic-btn').classList.remove('listening');}\n"
  "  else{\n"
  "    // Auto-switch to VOICE control mode so voice commands aren't fought by automation\n"
  "    if(state.ctrl!==2) sendCmd('YV');\n"
  "    recognition.start();listening=true;document.getElementById('mic-btn').classList.add('listening');\n"
  "  }\n"
  "}\n"
  "function speak(text){\n"
  "  document.getElementById('assistant-reply').textContent=text;\n"
  "  if(muted) return;\n"
  "  window.speechSynthesis.cancel();\n"
  "  const u=new SpeechSynthesisUtterance(text);\n"
  "  u.lang='en-US'; u.rate=1.05; u.pitch=1.0;\n"
  "  window.speechSynthesis.speak(u);\n"
  "}\n"
  "\n"
  "const NUMBER_WORDS={zero:0,one:1,two:2,three:3,four:4,five:5,six:6,seven:7,eight:8,nine:9,ten:10,eleven:11,twelve:12,thirteen:13,fourteen:14,fifteen:15,sixteen:16,seventeen:17,eighteen:18,nineteen:19,twenty:20,thirty:30,forty:40,fifty:50,sixty:60,seventy:70,eighty:80,ninety:90,hundred:100};\n"
  "function extractNumber(text){\n"
  "  const m=text.match(/(\\d{1,3})/);\n"
  "  if(m) return Math.min(100,parseInt(m[1]));\n"
  "  const words=text.split(/\\s+/);\n"
  "  let total=0,found=false;\n"
  "  for(const w of words){if(NUMBER_WORDS[w]!==undefined){total+=NUMBER_WORDS[w];found=true;}}\n"
  "  if(found) return Math.min(100,total);\n"
  "  return null;\n"
  "}\n"
  "function any(t,k){return k.some(x=>t.includes(x));}\n"
  "\n"
  "async function handleVoice(text){\n"
  "  if(any(text,['stop talking','shut up','be quiet','quiet','silence','enough'])){\n"
  "    window.speechSynthesis.cancel(); speak(\"OK\"); return;\n"
  "  }\n"
  "  if(any(text,['stop music','no music','silence music','music off','kill the music'])){\n"
  "    stopAllMusic(); speak(\"Music stopped\"); toast(\"Music stopped\",\"info\"); return;\n"
  "  }\n"
  "  if(any(text,['techno','play techno','techno music','party music','beat drop','drop the beat'])){\n"
  "    startTechno(); speak(\"Techno time!\"); toast(\"Techno started\",\"success\"); return;\n"
  "  }\n"
  "  if(any(text,['play music','start music','some music','put on music','ambient music'])){\n"
  "    startMusic('Voice'); speak(\"Playing ambient music\"); toast(\"Music started\",\"success\"); return;\n"
  "  }\n"
  "  if(any(text,['what can i say','what can you do','help','commands','show commands','list commands'])){\n"
  "    speakHelp(); return;\n"
  "  }\n"
  "  if(any(text,['what time is it','tell me the time','current time'])){\n"
  "    const d=new Date();\n"
  "    const h=d.getHours(); const m=d.getMinutes();\n"
  "    speak(\"It is \"+h+(m<10?\" oh \":\" \")+(m===0?\"o'clock\":m));\n"
  "    return;\n"
  "  }\n"
  "  if(any(text,['celebrate','party time','do something cool','wow me','show off'])){\n"
  "    speak(\"Tada!\"); toast(\"Surprise!\",\"success\"); return;\n"
  "  }\n"
  "  if(any(text,['who made you','who built you','who are you','what are you'])){\n"
  "    speak(\"I am Abdelrahman's smart home system, built on a PIC18F4550 microcontroller in pure assembly, with an ESP32 web companion.\");\n"
  "    return;\n"
  "  }\n"
  "  if(any(text,['thank you','thanks','appreciate it','good job','well done'])){\n"
  "    speak(\"You're welcome! Happy to help.\"); return;\n"
  "  }\n"
  "  if(any(text,['hello','hi there','hey','good morning','good evening','good afternoon'])){\n"
  "    speak(\"Hello! How can I help?\"); return;\n"
  "  }\n"
  "  if(any(text,['view log','show log','show history','show activity','recent events','what happened'])){\n"
  "    showLog(); return;\n"
  "  }\n"
  "  // ----- MACROS -----\n"
  "  if(any(text,['goodnight','good night','time for bed','bedtime'])){\n"
  "    runMacro('goodnight'); return;\n"
  "  }\n"
  "  if(any(text,['relax mode','relaxing mode','relax','chill mode','chill out','wind down'])){\n"
  "    runMacro('relax'); return;\n"
  "  }\n"
  "  if(any(text,['party mode','party time','let us party','dance time','celebrate'])){\n"
  "    runMacro('party'); return;\n"
  "  }\n"
  "  if(any(text,['movie mode','movie time','cinema mode','watching a movie','let us watch','film time'])){\n"
  "    runMacro('movie'); return;\n"
  "  }\n"
  "  if(any(text,[\"i'm leaving\",'im leaving','going out','leaving home','goodbye','i am leaving','bye bye'])){\n"
  "    runMacro('leaving'); return;\n"
  "  }\n"
  "  if(any(text,[\"i'm home\",'im home','welcome home','i am home','back home',\"i'm back\"])){\n"
  "    speak(\"Please enter your PIN to disarm.\"); askDisarmPin('welcome'); return;\n"
  "  }\n"
  "  // ----- PIN by spoken digits -----\n"
  "  let digits=text.match(/\\d/g);\n"
  "  if(!digits){\n"
  "    const wm=text.match(/\\b(zero|one|two|three|four|five|six|seven|eight|nine)\\b/g);\n"
  "    if(wm && wm.length===4) digits=wm.map(w=>String(NUMBER_WORDS[w]));\n"
  "  }\n"
  "  if(digits && digits.length===4 && /^[0-9]{4}$/.test(digits.join(''))){\n"
  "    const pin=digits.join('');\n"
  "    speak(\"Submitting PIN\");\n"
  "    await fetch('/pin?v='+pin); return;\n"
  "  }\n"
  "  // ----- ARM / DISARM -----\n"
  "  if(any(text,['disarm','deactivate security','turn off the alarm','disable alarm','stop the alarm'])){\n"
  "    speak(\"Please enter your PIN to disarm.\"); askDisarmPin('disarm'); return;\n"
  "  }\n"
  "  if(any(text,['arm the','activate security','lock down','secure the house','arm alarm','enable security'])){\n"
  "    speak(\"Arming the alarm\"); await sendCmd('AA'); return;\n"
  "  }\n"
  "  // ----- AUTO / RESET (exit all scenes, return to clean automatic mode) -----\n"
  "  if(any(text,['auto mode','automatic mode','set auto','go auto','default mode','reset mode','exit mode','normal mode','clear scene','reset all','back to normal','exit scene'])){\n"
  "    speak(\"Exiting all modes. Returning to automatic.\");\n"
  "    toast(\"Auto mode - cleared all scenes\", 'info');\n"
  "    stopAllMusic();\n"
  "    chimeAction();\n"
  "    await sendCmd('X');     // clear all manual overrides on PIC\n"
  "    await sendCmd('YA');    // control mode back to AUTO\n"
  "    return;\n"
  "  }\n"
  "  // ----- MODE -----\n"
  "  if(any(text,['away mode','set mode away','go away','set to away'])){\n"
  "    speak(\"Setting away mode\"); await sendCmd('MA'); return;\n"
  "  }\n"
  "  if(any(text,['home mode','set mode home','i am back','go home','set to home'])){\n"
  "    speak(\"Setting home mode\"); await sendCmd('MH'); return;\n"
  "  }\n"
  "  // ----- LIGHTS -----\n"
  "  if(any(text,['light','lights','bulb','lamp','brightness','dim','illumination'])){\n"
  "    if(any(text,['off','out','kill','extinguish','no light','darkness'])){\n"
  "      speak(\"Turning lights off\"); await sendCmd('L000'); return;\n"
  "    }\n"
  "    if(any(text,['maximum','full','bright','all the way','brightest','super bright'])){\n"
  "      speak(\"Lights at maximum\"); await sendCmd('L100'); return;\n"
  "    }\n"
  "    if(any(text,['medium','half'])){\n"
  "      speak(\"Lights at fifty percent\"); await sendCmd('L050'); return;\n"
  "    }\n"
  "    if(any(text,['dim'])){\n"
  "      const n=extractNumber(text);\n"
  "      if(n!==null){speak(\"Dimming lights to \"+n+\" percent\");await sendCmd('L'+String(n).padStart(3,'0'));}\n"
  "      else{speak(\"Dimming lights to twenty five percent\");await sendCmd('L025');}\n"
  "      return;\n"
  "    }\n"
  "    if(any(text,['brighten','brighter','more light'])){\n"
  "      speak(\"Brightening lights\"); await sendCmd('L080'); return;\n"
  "    }\n"
  "    const n=extractNumber(text);\n"
  "    if(n!==null){speak(\"Setting lights to \"+n+\" percent\");await sendCmd('L'+String(n).padStart(3,'0'));return;}\n"
  "    if(any(text,['on'])){speak(\"Lights on\");await sendCmd('L100');return;}\n"
  "  }\n"
  "  // ----- DOOR -----\n"
  "  if(any(text,['unlock','open the door','open door','let me in','let us in'])){\n"
  "    speak(\"Unlocking the door\"); await sendCmd('D1'); return;\n"
  "  }\n"
  "  if(any(text,['lock the door','close the door','lock door','lock up','shut the door'])){\n"
  "    speak(\"Locking the door\"); await sendCmd('D0'); return;\n"
  "  }\n"
  "  // ----- FAN -----\n"
  "  if(any(text,['fan'])){\n"
  "    if(any(text,['off','stop','kill'])){speak(\"Turning the fan off\");await sendCmd('F0');return;}\n"
  "    if(any(text,['on','start','run','spin'])){speak(\"Turning the fan on\");await sendCmd('F1');return;}\n"
  "  }\n"
  "  if(any(text,[\"i'm hot\",'too hot','cool me','feeling warm','heating up','sweating'])){\n"
  "    speak(\"Turning the fan on\"); await sendCmd('F1'); return;\n"
  "  }\n"
  "  if(any(text,[\"i'm cold\",'too cold','freezing','chilly','cool down too much'])){\n"
  "    speak(\"Turning the fan off\"); await sendCmd('F0'); return;\n"
  "  }\n"
  "  // ----- QUERIES -----\n"
  "  if(any(text,['temperature','how hot','how warm','how cold','what is the temp'])){\n"
  "    speak(\"The temperature is \"+state.temp+\" degrees celsius\"); return;\n"
  "  }\n"
  "  if(any(text,['humidity','how humid','how dry','moisture'])){\n"
  "    speak(\"The humidity is \"+state.hum+\" percent\"); return;\n"
  "  }\n"
  "  if(any(text,['light level','how bright','how dark','how much light','brightness level'])){\n"
  "    speak(\"The light level is \"+state.light+\" percent\"); return;\n"
  "  }\n"
  "  if(any(text,['is the door','door status','door state','door locked'])){\n"
  "    speak(\"The door is \"+(state.door?\"unlocked\":\"locked\")); return;\n"
  "  }\n"
  "  if(any(text,['is the fan','fan status','fan state'])){\n"
  "    speak(\"The fan is \"+(state.fan?\"on\":\"off\")); return;\n"
  "  }\n"
  "  if(any(text,['is the alarm','alarm status','alarm state','security status'])){\n"
  "    speak(\"The alarm is \"+(state.armed?\"armed\":\"disarmed\")); return;\n"
  "  }\n"
  "  if(any(text,['what mode','current mode','which mode','mode am i in'])){\n"
  "    const m = state.mode ? 'away' : 'home';\n"
  "    const c = state.ctrl===0?'auto':state.ctrl===1?'manual':'voice';\n"
  "    speak(\"You are in \"+m+\" mode with \"+c+\" control\");\n"
  "    return;\n"
  "  }\n"
  "  if(any(text,['status report','give me a status',\"what's happening\",'status','everything','tell me everything','full report'])){\n"
  "    speak(\"Temperature \"+state.temp+\" degrees. Humidity \"+state.hum+\" percent. Light \"+state.light+\" percent. Door is \"+(state.door?\"unlocked\":\"locked\")+\". Alarm is \"+(state.armed?\"armed\":\"disarmed\")+\".\");\n"
  "    return;\n"
  "  }\n"
  "  // No match\n"
  "  speak(\"Sorry I didn't understand that. Try saying help for a list of commands.\");\n"
  "}\n"
  "\n"
  "function speakHelp(){\n"
  "  speak(\"I understand many commands. For scenes try goodnight, relax mode, party mode, movie mode, or I'm leaving. For lights say lights at any percent, off, maximum, or brighten. For sensors ask what is the temperature, humidity, or status report. For security say arm or disarm. You can also say play music, stop music, view log, what time is it, or thank you. Say stop to silence me.\");\n"
  "}\n"
  "\n"
  "\n"
  "\n"
  "// ===========================================================\n"
  "// Sound effects via Web Audio API (no audio files needed)\n"
  "// ===========================================================\n"
  "let audioCtx=null;\n"
  "function getAudio(){\n"
  "  if(!audioCtx){\n"
  "    try { audioCtx = new (window.AudioContext||window.webkitAudioContext)(); }\n"
  "    catch(e){ console.warn('Web Audio not supported'); }\n"
  "  }\n"
  "  return audioCtx;\n"
  "}\n"
  "function beep(freq, dur, type, gain){\n"
  "  const ctx=getAudio();\n"
  "  if(!ctx) return;\n"
  "  if(ctx.state==='suspended') ctx.resume();\n"
  "  const osc=ctx.createOscillator();\n"
  "  const g=ctx.createGain();\n"
  "  osc.type=type||'sine';\n"
  "  osc.frequency.value=freq;\n"
  "  g.gain.value=0;\n"
  "  g.gain.linearRampToValueAtTime(gain||0.12, ctx.currentTime+0.01);\n"
  "  g.gain.linearRampToValueAtTime(0, ctx.currentTime+dur);\n"
  "  osc.connect(g); g.connect(ctx.destination);\n"
  "  osc.start();\n"
  "  osc.stop(ctx.currentTime+dur+0.05);\n"
  "}\n"
  "function chimeSuccess(){ beep(600,0.08,'sine'); setTimeout(()=>beep(900,0.12,'sine'),70); }\n"
  "function chimeAction(){ beep(440,0.06,'sine'); }\n"
  "function chimeError(){ beep(200,0.15,'square',0.08); }\n"
  "function chimeUnlock(){ beep(523,0.08,'sine'); setTimeout(()=>beep(659,0.08,'sine'),80); setTimeout(()=>beep(784,0.12,'sine'),160); }\n"
  "function chimeLock(){ beep(523,0.08,'sine'); setTimeout(()=>beep(392,0.12,'sine'),80); }\n"
  "function chimeAlarm(){ beep(800,0.12,'square',0.18); setTimeout(()=>beep(800,0.12,'square',0.18),180); setTimeout(()=>beep(800,0.12,'square',0.18),360); }\n"
  "function chimeWelcome(){ beep(523,0.1,'sine'); setTimeout(()=>beep(659,0.1,'sine'),100); setTimeout(()=>beep(784,0.1,'sine'),200); setTimeout(()=>beep(1046,0.18,'sine'),300); }\n"
  "\n"
  "// ===========================================================\n"
  "// Ambient music via Web Audio API (synthesized, no files)\n"
  "// ===========================================================\n"
  "let musicNodes=null;\n"
  "function startMusic(label){\n"
  "  stopMusic();\n"
  "  const ctx=getAudio();\n"
  "  if(!ctx) return;\n"
  "  if(ctx.state==='suspended') ctx.resume();\n"
  "  const osc1=ctx.createOscillator();\n"
  "  const osc2=ctx.createOscillator();\n"
  "  const osc3=ctx.createOscillator();\n"
  "  osc1.type='sine'; osc2.type='sine'; osc3.type='triangle';\n"
  "  osc1.frequency.value=174;\n"
  "  osc2.frequency.value=261.6;\n"
  "  osc3.frequency.value=349.2;\n"
  "  const lfo=ctx.createOscillator();\n"
  "  const lfoGain=ctx.createGain();\n"
  "  lfo.frequency.value=0.15;\n"
  "  lfoGain.gain.value=2;\n"
  "  lfo.connect(lfoGain);\n"
  "  lfoGain.connect(osc3.frequency);\n"
  "  const filter=ctx.createBiquadFilter();\n"
  "  filter.type='lowpass';\n"
  "  filter.frequency.value=800;\n"
  "  filter.Q.value=0.7;\n"
  "  const mainGain=ctx.createGain();\n"
  "  mainGain.gain.value=0;\n"
  "  mainGain.gain.linearRampToValueAtTime(0.12, ctx.currentTime+1.5);\n"
  "  osc1.connect(filter); osc2.connect(filter); osc3.connect(filter);\n"
  "  filter.connect(mainGain); mainGain.connect(ctx.destination);\n"
  "  osc1.start(); osc2.start(); osc3.start(); lfo.start();\n"
  "  musicNodes={osc1,osc2,osc3,lfo,mainGain,ctx};\n"
  "  document.getElementById('music-label').textContent = label || 'Ambient';\n"
  "  document.getElementById('music-indicator').classList.add('show');\n"
  "}\n"
  "function stopMusic(){\n"
  "  if(!musicNodes) return;\n"
  "  const {osc1,osc2,osc3,lfo,mainGain,ctx} = musicNodes;\n"
  "  mainGain.gain.linearRampToValueAtTime(0, ctx.currentTime+0.8);\n"
  "  setTimeout(()=>{\n"
  "    try{osc1.stop(); osc2.stop(); osc3.stop(); lfo.stop();}catch(e){}\n"
  "  }, 900);\n"
  "  musicNodes=null;\n"
  "  document.getElementById('music-indicator').classList.remove('show');\n"
  "}\n"
  "\n"
  "// ===========================================================\n"
  "// TECHNO music - 128 BPM with kick, bass, hi-hat\n"
  "// ===========================================================\n"
  "let technoTimer=null;\n"
  "let technoStep=0;\n"
  "function startTechno(){\n"
  "  stopTechno();\n"
  "  const ctx=getAudio();\n"
  "  if(!ctx) return;\n"
  "  if(ctx.state==='suspended') ctx.resume();\n"
  "  // 128 BPM = 469ms per beat. Use 16th notes (4 per beat) = ~117ms\n"
  "  const stepMs=117;\n"
  "  technoStep=0;\n"
  "  // Bass pattern (note frequencies for a minor groove)\n"
  "  const bassNotes=[65.4,65.4,98,65.4,65.4,87.3,98,82.4]; // C2 C2 G2 C2 C2 F2 G2 E2\n"
  "  technoTimer=setInterval(()=>{\n"
  "    const t=ctx.currentTime;\n"
  "    const beat=technoStep%4;          // 0..3 within a beat\n"
  "    const measureStep=technoStep%16;  // 0..15 in a measure\n"
  "    // KICK on every downbeat (every 4 steps)\n"
  "    if(beat===0){\n"
  "      const k=ctx.createOscillator();\n"
  "      const kg=ctx.createGain();\n"
  "      k.type='sine';\n"
  "      k.frequency.setValueAtTime(140,t);\n"
  "      k.frequency.exponentialRampToValueAtTime(50,t+0.08);\n"
  "      kg.gain.setValueAtTime(0.6,t);\n"
  "      kg.gain.exponentialRampToValueAtTime(0.001,t+0.18);\n"
  "      k.connect(kg); kg.connect(ctx.destination);\n"
  "      k.start(t); k.stop(t+0.2);\n"
  "    }\n"
  "    // BASS on the off-beats (between kicks) for groove\n"
  "    if(beat===2){\n"
  "      const noteIdx=Math.floor(measureStep/2)%bassNotes.length;\n"
  "      const b=ctx.createOscillator();\n"
  "      const bg=ctx.createGain();\n"
  "      const bf=ctx.createBiquadFilter();\n"
  "      b.type='sawtooth';\n"
  "      b.frequency.value=bassNotes[noteIdx];\n"
  "      bf.type='lowpass';\n"
  "      bf.frequency.value=400;\n"
  "      bf.Q.value=8;\n"
  "      bg.gain.setValueAtTime(0.18,t);\n"
  "      bg.gain.exponentialRampToValueAtTime(0.001,t+0.12);\n"
  "      b.connect(bf); bf.connect(bg); bg.connect(ctx.destination);\n"
  "      b.start(t); b.stop(t+0.15);\n"
  "    }\n"
  "    // HI-HAT on off-beats (steps 1 and 3 of each beat = 8th-note offbeats)\n"
  "    if(beat===1||beat===3){\n"
  "      const bufSize=ctx.sampleRate*0.05;\n"
  "      const buf=ctx.createBuffer(1,bufSize,ctx.sampleRate);\n"
  "      const data=buf.getChannelData(0);\n"
  "      for(let i=0;i<bufSize;i++) data[i]=Math.random()*2-1;\n"
  "      const src=ctx.createBufferSource();\n"
  "      src.buffer=buf;\n"
  "      const hf=ctx.createBiquadFilter();\n"
  "      hf.type='highpass';\n"
  "      hf.frequency.value=7000;\n"
  "      const hg=ctx.createGain();\n"
  "      hg.gain.setValueAtTime(0.08,t);\n"
  "      hg.gain.exponentialRampToValueAtTime(0.001,t+0.05);\n"
  "      src.connect(hf); hf.connect(hg); hg.connect(ctx.destination);\n"
  "      src.start(t);\n"
  "    }\n"
  "    // SYNTH STAB every 8 steps for variety\n"
  "    if(measureStep===0||measureStep===8){\n"
  "      const s=ctx.createOscillator();\n"
  "      const sg=ctx.createGain();\n"
  "      const sf=ctx.createBiquadFilter();\n"
  "      s.type='square';\n"
  "      s.frequency.value=261.6; // C4\n"
  "      sf.type='lowpass';\n"
  "      sf.frequency.setValueAtTime(2000,t);\n"
  "      sf.frequency.exponentialRampToValueAtTime(400,t+0.3);\n"
  "      sf.Q.value=12;\n"
  "      sg.gain.setValueAtTime(0.1,t);\n"
  "      sg.gain.exponentialRampToValueAtTime(0.001,t+0.3);\n"
  "      s.connect(sf); sf.connect(sg); sg.connect(ctx.destination);\n"
  "      s.start(t); s.stop(t+0.32);\n"
  "    }\n"
  "    technoStep++;\n"
  "  }, stepMs);\n"
  "  document.getElementById('music-label').textContent = 'Techno';\n"
  "  document.getElementById('music-indicator').classList.add('show');\n"
  "}\n"
  "function stopTechno(){\n"
  "  if(technoTimer){ clearInterval(technoTimer); technoTimer=null; }\n"
  "  document.getElementById('music-indicator').classList.remove('show');\n"
  "}\n"
  "// Unified stop that kills both ambient and techno\n"
  "function stopAllMusic(){ stopMusic(); stopTechno(); }\n"
  "\n"
  "// ===========================================================\n"
  "// Toast notification\n"
  "// ===========================================================\n"
  "function toast(msg, type){\n"
  "  const t=document.getElementById('toast');\n"
  "  t.textContent=msg;\n"
  "  t.className='toast show ' + (type||'info');\n"
  "  clearTimeout(window._toastTimer);\n"
  "  window._toastTimer=setTimeout(()=>t.className='toast '+(type||'info'),2200);\n"
  "}\n"
  "\n"
  "// ===========================================================\n"
  "// Quick-command shortcuts (clickable buttons)\n"
  "// ===========================================================\n"
  "async function quickCmd(cmd, spoken){\n"
  "  await sendCmd(cmd);\n"
  "  if(spoken) speak(spoken);\n"
  "  toast(spoken || ('Command: ' + cmd), 'success');\n"
  "}\n"
  "\n"
  "function speakQuery(kind){\n"
  "  let text='';\n"
  "  switch(kind){\n"
  "    case 'temp': text=\"The temperature is \"+state.temp+\" degrees celsius\"; break;\n"
  "    case 'hum':  text=\"The humidity is \"+state.hum+\" percent\"; break;\n"
  "    case 'light':text=\"The light level is \"+state.light+\" percent\"; break;\n"
  "    case 'door': text=\"The door is \"+(state.door?\"unlocked\":\"locked\"); break;\n"
  "    case 'alarm':text=\"The alarm is \"+(state.armed?\"armed\":\"disarmed\"); break;\n"
  "    case 'full': text=\"Temperature \"+state.temp+\" degrees. Humidity \"+state.hum+\" percent. Light \"+state.light+\" percent. Door is \"+(state.door?\"unlocked\":\"locked\")+\". Alarm is \"+(state.armed?\"armed\":\"disarmed\")+\".\";break;\n"
  "  }\n"
  "  speak(text);\n"
  "  toast(text.substring(0,60), 'info');\n"
  "}\n"
  "\n"
  "// Click device tiles to toggle (only meaningful in Manual mode)\n"
  "async function tileToggle(which){\n"
  "  if(state.ctrl!==1){\n"
  "    toast(\"Switch to Manual mode first to use tile controls\",\"info\");\n"
  "    return;\n"
  "  }\n"
  "  switch(which){\n"
  "    case 'door':\n"
  "      if(state.door) await sendCmd('D0'); else await sendCmd('D1');\n"
  "      toast(state.door?\"Locking door\":\"Unlocking door\",\"success\");\n"
  "      break;\n"
  "    case 'bulb':\n"
  "      if(state.led>0) await sendCmd('L000'); else await sendCmd('L100');\n"
  "      toast(state.led>0?\"Lights off\":\"Lights on\",\"success\");\n"
  "      break;\n"
  "    case 'fan':\n"
  "      if(state.fan) await sendCmd('F0'); else await sendCmd('F1');\n"
  "      toast(state.fan?\"Fan off\":\"Fan on\",\"success\");\n"
  "      break;\n"
  "  }\n"
  "}\n"
  "\n"
  "async function runMacro(name){\n"
  "  switch(name){\n"
  "    case 'goodnight':\n"
  "      speak(\"Goodnight. Locking up, dimming the lights, and arming the alarm.\");\n"
  "      toast(\"Goodnight scene activated\", 'success');\n"
  "      chimeLock();\n"
  "      stopAllMusic();\n"
  "      await sendCmd('YM');\n"
  "      await sendCmd('L000');\n"
  "      await sendCmd('F0');\n"
  "      await sendCmd('D0');\n"
  "      await sendCmd('MA');\n"
  "      await sendCmd('AA');\n"
  "      break;\n"
  "    case 'relax':\n"
  "      speak(\"Activating relax mode. Soft lights, ambient music, gentle airflow.\");\n"
  "      toast(\"Relax scene activated\", 'success');\n"
  "      chimeSuccess();\n"
  "      stopTechno();\n"
  "      await sendCmd('YM');\n"
  "      await sendCmd('L030');\n"
  "      await sendCmd('F0');\n"
  "      await sendCmd('D0');\n"
  "      await sendCmd('MH');\n"
  "      startMusic('Relax');\n"
  "      break;\n"
  "    case 'party':\n"
  "      speak(\"Party mode! Let's go!\");\n"
  "      toast(\"Party scene activated - TECHNO TIME\", 'success');\n"
  "      chimeSuccess();\n"
  "      stopMusic();\n"
  "      await sendCmd('YM');\n"
  "      await sendCmd('L100');\n"
  "      await sendCmd('F1');\n"
  "      await sendCmd('MH');\n"
  "      startTechno();\n"
  "      break;\n"
  "    case 'movie':\n"
  "      speak(\"Movie mode. Lights dimmed for the screen.\");\n"
  "      toast(\"Movie scene activated\", 'success');\n"
  "      chimeAction();\n"
  "      stopTechno();\n"
  "      await sendCmd('YM');\n"
  "      await sendCmd('L015');\n"
  "      await sendCmd('F0');\n"
  "      await sendCmd('D0');\n"
  "      await sendCmd('MH');\n"
  "      startMusic('Movie');\n"
  "      break;\n"
  "    case 'leaving':\n"
  "      speak(\"Leaving home. Locking up and arming security.\");\n"
  "      toast(\"Away scene activated\", 'success');\n"
  "      chimeLock();\n"
  "      stopAllMusic();\n"
  "      await sendCmd('YA');\n"
  "      await sendCmd('L000');\n"
  "      await sendCmd('F0');\n"
  "      await sendCmd('D0');\n"
  "      await sendCmd('MA');\n"
  "      await sendCmd('AA');\n"
  "      break;\n"
  "  }\n"
  "}\n"
  "\n"
  "async function showLog(){\n"
  "  try {\n"
  "    const r = await fetch('/log');\n"
  "    const data = await r.json();\n"
  "    const events = data.events || [];\n"
  "    let html = '<h3 style=\"color:#e5e7eb;margin-bottom:14px\">&#x1F4C2; Activity Log (from EEPROM)</h3>';\n"
  "    html += '<p style=\"color:#9ca3af;font-size:12px;margin-bottom:14px\">These events survived the last power cycle. Most recent first.</p>';\n"
  "    if (events.length === 0) {\n"
  "      html += '<p style=\"color:#9ca3af\">No events recorded yet.</p>';\n"
  "    } else {\n"
  "      html += '<table style=\"width:100%;color:#e5e7eb;border-collapse:collapse\">';\n"
  "      html += '<tr style=\"border-bottom:1px solid #374151;color:#9ca3af\"><th style=\"text-align:left;padding:8px\">Slot</th><th style=\"text-align:left;padding:8px\">Event</th></tr>';\n"
  "      // Display newest-first\n"
  "      for (let i = events.length - 1; i >= 0; i--) {\n"
  "        const e = events[i];\n"
  "        let color = '#e5e7eb';\n"
  "        if (e.type === 4) color = '#ef4444';        // intrusion red\n"
  "        else if (e.type === 9 || e.type === 10) color = '#f59e0b';  // env alert orange\n"
  "        else if (e.type === 2) color = '#22c55e';   // PIN OK green\n"
  "        else if (e.type === 3) color = '#f97316';   // PIN fail orange\n"
  "        html += '<tr style=\"border-bottom:1px solid #1f2937\"><td style=\"padding:8px;color:#9ca3af\">#' + e.slot + '</td>';\n"
  "        html += '<td style=\"padding:8px;color:' + color + ';font-weight:600\">' + e.name + '</td></tr>';\n"
  "      }\n"
  "      html += '</table>';\n"
  "    }\n"
  "    html += '<button onclick=\"closeLog()\" class=\"btn\" style=\"margin-top:14px\">Close</button>';\n"
  "    document.getElementById('log-modal-body').innerHTML = html;\n"
  "    document.getElementById('log-modal-bg').classList.add('show');\n"
  "  } catch (e) {\n"
  "    alert('Failed to fetch log: ' + e);\n"
  "  }\n"
  "}\n"
  "\n"
  "function closeLog(){\n"
  "  document.getElementById('log-modal-bg').classList.remove('show');\n"
  "}\n"
  "</script>\n"
  "</body></html>\n"
  ;

// =========================================================================
// HTTP HANDLERS
// =========================================================================
void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}
void handleStatus() {
  String j = "{\"light\":" + String(state.light) +
             ",\"temp\":"  + String(state.temp)  +
             ",\"hum\":"   + String(state.hum)   +
             ",\"mode\":"  + String(state.mode)  +
             ",\"door\":"  + String(state.door)  +
             ",\"fan\":"   + String(state.fan)   +
             ",\"pir\":"   + String(state.pir)   +
             ",\"led\":"   + String(state.led)   +
             ",\"ctrl\":"  + String(state.ctrl)  +
             ",\"armed\":" + String(state.armed) +
             ",\"intrusion\":" + (state.intrusion ? "true" : "false");
  j += ",\"events\":[";
  for (int i = 0; i < eventCount; i++) {
    if (i) j += ",";
    j += "{\"when\":\"" + events[i].when + "\",\"type\":\"" + events[i].type + "\",\"msg\":\"" + events[i].msg + "\"}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}
void handleCmd() {
  if (!server.hasArg("c")) { server.send(400, "text/plain", "missing c"); return; }
  String c = server.arg("c");
  sendToPIC("$" + c);
  addEvent("VOICE", "Cmd: $" + c);
  server.send(200, "text/plain", "ok");
}
void handlePin() {
  if (!server.hasArg("v")) { server.send(400, "text/plain", "missing v"); return; }
  String v = server.arg("v");
  sendToPIC("$P" + v);
  addEvent("PIN", "PIN entered");
  server.send(200, "text/plain", "ok");
}

void handleLog() {
  // Ask the PIC for its activity log; the response will arrive asynchronously
  // and be stored in lastLogReport via handlePicLine().
  sendToPIC("$R");
  // Wait briefly for the PIC reply (typically <50ms)
  unsigned long start = millis();
  String before = lastLogReport;
  while (lastLogReport == before && (millis() - start) < 500) {
    pumpPicUart();
    delay(10);
  }

  // Parse the raw $R,<head>,<e0>,<e1>,...,<e7>
  // Build a JSON array of human-readable events
  String json = "{\"raw\":\"" + lastLogReport + "\",\"events\":[";
  if (lastLogReport.startsWith("$R,")) {
    String s = lastLogReport.substring(3);
    int parts[16];
    int n = 0;
    int start2 = 0;
    for (int i = 0; i <= (int)s.length() && n < 16; i++) {
      if (i == (int)s.length() || s[i] == ',') {
        parts[n++] = s.substring(start2, i).toInt();
        start2 = i + 1;
      }
    }
    // parts[0] = head pointer, parts[1..8] = event slots
    // Events in chronological order: starting at head, wrapping around
    int head = parts[0];
    bool first = true;
    for (int i = 0; i < 8; i++) {
      int idx = (head + i) % 8;
      int eventType = parts[1 + idx];
      if (eventType == 0 || eventType == 255) continue;   // empty slot
      if (!first) json += ",";
      first = false;
      const char* name = "UNKNOWN";
      switch (eventType) {
        case 1: name = "Boot"; break;
        case 2: name = "PIN accepted"; break;
        case 3: name = "PIN rejected"; break;
        case 4: name = "INTRUSION"; break;
        case 5: name = "Alarm armed"; break;
        case 6: name = "Alarm disarmed"; break;
        case 7: name = "Home mode"; break;
        case 8: name = "Away mode"; break;
        case 9: name = "Heat alert"; break;
        case 10: name = "Humidity alert"; break;
      }
      json += "{\"slot\":" + String(idx) + ",\"type\":" + String(eventType) +
              ",\"name\":\"" + String(name) + "\"}";
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// =========================================================================
// SETUP / LOOP
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\n=== Smart Home ESP32 Dashboard v7 ===");

  PicSerial.begin(9600, SERIAL_8N1, PIC_RX_PIN, PIC_TX_PIN);

  // Wi-Fi connect with diagnostic output
  Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());
  WiFi.mode(WIFI_STA);

  // Request a fixed IP so Chrome's pre-existing mic permission stays valid
  /*IPAddress local_IP(192, 168, 100, 127);
  IPAddress gateway(192, 168, 100, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(8, 8, 4, 4);
  if (!WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
    Serial.println("Static IP config failed, falling back to DHCP");
  }*/

  WiFi.disconnect(true);   // forget any previous credentials
  delay(500);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to '%s' (password length: %d)\n", WIFI_SSID, (int)strlen(WIFI_PASSWORD));

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(1000);
    wl_status_t s = WiFi.status();
    const char* sname = "unknown";
    switch (s) {
      case WL_IDLE_STATUS:      sname = "IDLE";          break;
      case WL_NO_SSID_AVAIL:    sname = "NO_SSID_AVAIL"; break;
      case WL_SCAN_COMPLETED:   sname = "SCAN_COMPLETED";break;
      case WL_CONNECTED:        sname = "CONNECTED";     break;
      case WL_CONNECT_FAILED:   sname = "CONNECT_FAILED";break;
      case WL_CONNECTION_LOST:  sname = "CONNECTION_LOST"; break;
      case WL_DISCONNECTED:     sname = "DISCONNECTED";  break;
    }
    Serial.printf("  attempt %d: status=%d (%s)\n", ++attempts, (int)s, sname);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n=== WiFi connect FAILED after 40 attempts ===");
    Serial.println("Scanning for nearby networks:");
    int n = WiFi.scanNetworks();
    if (n == 0) {
      Serial.println("  No networks found at all!");
    } else {
      for (int i = 0; i < n; i++) {
        Serial.printf("  %d: %s (%d dBm) %s\n", i,
                      WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "[open]" : "[secured]");
      }
    }
    Serial.println("Restarting in 10 seconds...");
    delay(10000);
    ESP.restart();
  }

  Serial.printf("\nConnected. IP = %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Signal strength: %d dBm\n", WiFi.RSSI());

  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  Serial.print("Waiting for NTP");
  struct tm t;
  for (int i = 0; i < 10; i++) {
    if (getLocalTime(&t, 500)) break;
    Serial.print(".");
  }
  Serial.println(" got time " + currentDateTimeStr());

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/cmd", handleCmd);
  server.on("/pin", handlePin);
  server.on("/log", handleLog);
  server.begin();
  Serial.println("HTTP server running");

  MailClient.networkReconnect(true);
  addEvent("BOOT", "ESP32 online at " + WiFi.localIP().toString());
}

void loop() {
  server.handleClient();
  pumpPicUart();
}
