// config.example.h -- Template for smart_home_esp32.ino secrets.
//
// Copy this file to config.h (same folder) and fill in your real values.
// config.h is gitignored and must never be committed.

// Wi-Fi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Gmail SMTP credentials (use a Gmail App Password, not your account password)
// Generate one at: Google Account -> Security -> 2-Step Verification -> App passwords
#define SMTP_HOST        "smtp.gmail.com"
#define SMTP_PORT        465
#define EMAIL_SENDER     "your_email@gmail.com"
#define EMAIL_PASSWORD   "YOUR_16_CHAR_APP_PASSWORD"
#define EMAIL_RECIPIENT  "recipient@example.com"
