# Smart Home Automation System — PIC18F4550 + ESP32

A complete smart home controller with bare-metal MPASM assembly firmware on a PIC18F4550, a Wi-Fi-connected ESP32 companion, a real-time browser dashboard, voice control, and Gmail-backed email alerts. Built for the *Microprocessor Systems* course at The British University in Egypt.

<p>
  <img alt="Assembly" src="https://img.shields.io/badge/PIC18F4550-MPASM-A8B9CC?logo=microchip&logoColor=white">
  <img alt="ESP32" src="https://img.shields.io/badge/ESP32-C%2B%2B-E7352C?logo=espressif&logoColor=white">
  <img alt="Dashboard" src="https://img.shields.io/badge/Dashboard-HTML%2FJS-F7DF1E?logo=javascript&logoColor=black">
  <img alt="License" src="https://img.shields.io/badge/License-MIT-yellow.svg">
</p>

**17/17 functional test cases passing.** Full write-up: [Smart_Home_Automation_Report.pdf](Smart_Home_Automation_Report.pdf).

## Overview

The PIC18F4550 is the system's central controller, wired directly to every sensor and actuator with no abstraction layer in between. It runs standalone — sensors, keypad PIN entry, the alarm, and EEPROM-backed event logging all work with zero network connectivity. An ESP32 companion sits below it purely as a Wi-Fi gateway: it exposes a live dashboard, accepts voice commands via the browser's Web Speech API, and emails the homeowner through Gmail SMTP when something goes wrong. The two boards talk over a custom text-based UART protocol; if the ESP32 is unplugged, the PIC keeps running exactly as before.

## What it does

- **Climate awareness** — DHT11 reports temperature/humidity; a relay-driven fan engages at 32°C, or earlier (28°C) if humidity exceeds 70%, modeling real heat-index behavior. A 2°C hysteresis deadband prevents relay chatter.
- **Ambient + welcome lighting** — an LDR through the 10-bit ADC drives a software-PWM bulb that brightens as the room darkens (8-sample moving average filters sensor noise). Motion at the PIR boosts the bulb to 80% for 3 seconds when HOME + AUTO are active.
- **Smart door lock** — an SG90 servo driven by 50 Hz software PWM generated in the *high-priority* Timer0 ISR, so it can't be preempted by UART traffic. Auto re-locks 5 seconds after unlock.
- **Multi-source PIN entry** — a 4-digit PIN can be entered via physical keypad, the dashboard PIN-pad, or spoken digit-by-digit ("one two three four"); all three converge on the same comparison routine.
- **Three-strikes intrusion detection** — each wrong PIN triggers a 10-second buzzer warning; three failures force AWAY mode, arm the alarm, lock the door, and send an intrusion email.
- **PIR-based alarm** — an 8-level confidence counter (4 consecutive readings required) filters out the transient IR spikes that fluorescent lighting causes in unfiltered PIR sensors.
- **Three control modes** — AUTO (PIC's smart logic owns every actuator), MANUAL (dashboard owns), VOICE (voice owns). Switching to AUTO instantly clears all overrides.
- **Persistent activity log** — an 8-slot circular buffer in EEPROM survives power cycles and resets.
- **Critical alerts** — `!HOTALERT` at 40°C and `!HUMALERT` at 90% humidity, each firing a color-coded HTML email exactly once per episode.
- **Voice-controlled dashboard** — 60+ recognized phrases covering scene macros (Goodnight, Relax, Party, Movie, Leaving), sensor queries, light intensity by percent, and mode switching.
- **Browser-synthesized audio** — the Web Audio API generates ambient drone music for Relax/Movie and a 128 BPM techno pattern (kick + bass + hi-hat + synth stab) for Party mode, with zero audio files stored anywhere.

## Architecture

```
┌────────────┐        ┌──────────────────────┐        UART (9600 8N1)       ┌────────────────────┐
│  Sensors    │───────▶│   PIC18F4550          │◀────────────────────────────▶│   ESP32 DevKit       │
│  LDR·DHT11  │        │   4 MHz · MPASM       │   $S,... status (1 Hz)       │   Wi-Fi gateway       │
│  PIR·Keypad │        │   Timer0 (HIGH prio)  │   $D1/$F1/$AA/... commands   │   HTTP server         │
└────────────┘        │   → 50 Hz servo PWM    │   !INTRUDE / !HOTALERT /     │   Web Speech API       │
┌────────────┐        │   Timer2 (LOW prio)    │   !HUMALERT alerts           │   Gmail SMTP           │
│  Actuators  │◀───────│   → 100 Hz LED PWM     │                              └─────────┬───────────┘
│  Bulb·Servo │        │   EUSART RX (LOW prio) │                                        │ HTTP
│  Fan·Buzzer │        │   8-slot EEPROM log    │                                        ▼
└────────────┘        └──────────────────────┘                          ┌─────────────────────┐
                                                                          │  Browser Dashboard    │
                                                                          │  Live tiles · voice    │
                                                                          │  Manual control         │
                                                                          │  Scene macros            │
                                                                          └─────────────────────┘
```

Two interrupt priorities matter here: the SG90's 1–2 ms control pulse can't tolerate any preemption, so Timer0 (servo PWM) runs at HIGH priority while Timer2 (LED PWM) and the UART RX interrupt run LOW. This was stress-tested by streaming continuous voice commands while the servo was mid-motion — no jitter, because the high-priority ISR is never interrupted by UART traffic.

## Hardware

Built on a Betamini PIC18F4550 training board (on-board LM7805, 4 MHz crystal, MCLR pull-up, 16×2 LCD, 4-button keypad) with external HC-SR501 PIR, SG90 servo, DHT11, relay-driven fan, and an ESP32 DevKit connected through a bidirectional MOSFET-based logic-level shifter (5 V ↔ 3.3 V). Three independent power rails — 12 V wall adapter for the kit, a dedicated 5 V supply for the servo (its ~600 mA inrush current would otherwise brown out the regulator), and USB for the ESP32 — share only a common ground, because combining the servo and Wi-Fi current spikes on one rail risks resetting the PIC mid-operation.

| Pin | Port | Connected device |
|---|---|---|
| 3 | RA1/AN1 | LDR voltage divider |
| 6 | RA4 | Smart bulb LED (PWM) |
| 15 | RC0 | Buzzer |
| 16 | RC1 | SG90 servo signal |
| 17 | RC2 | Fan relay |
| 19–22, 27, 28 | RD0–RD5 | 16×2 LCD (4-bit) |
| 25, 26 | RC6/RC7 | UART to/from ESP32 |
| 33 | RB0 | DHT11 data |
| 34 | RB1 | HC-SR501 PIR |
| 37–40 | RB4–RB7 | Keypad |

Full pin table, power distribution, and the Proteus schematic are in the [report](Smart_Home_Automation_Report.pdf).

## UART protocol

Status frames stream from the PIC every second; commands flow from the ESP32 on user actions; out-of-band alerts interrupt the stream on emergencies.

| Direction | Message | Meaning |
|---|---|---|
| PIC → ESP | `$S,L,T,H,M,D,F,P,LD,C,A` | 1 Hz status frame (10 fields) |
| PIC → ESP | `!INTRUDE` / `!HOTALERT` / `!HUMALERT` | Emergency alerts |
| PIC → ESP | `$POK` / `$PNO` | PIN accepted / rejected |
| ESP → PIC | `$D1`/`$D0`, `$F1`/`$F0`, `$AA`/`$AD` | Door, fan, alarm control |
| ESP → PIC | `$MH`/`$MA`, `$YA`/`$YM`/`$YV` | Mode + control-source switching |
| ESP → PIC | `$Lnnn`, `$Pnnnn` | LED brightness, PIN entry |
| ESP → PIC | `$X`, `$R` | Clear overrides, dump EEPROM log |

Full protocol table and the end-to-end voice-command trace (mic → Web Speech API → HTTP → UART → servo → status frame → TTS confirmation, ~1.0–1.3 s round trip) are in the report.

## Repository structure

```
firmware/
  pic/main.asm                          # PIC18F4550 firmware (MPASM), 2500+ lines
  esp32/smart_home_esp32.ino            # ESP32 gateway + embedded dashboard (HTML/CSS/JS)
  esp32/config.example.h                # Wi-Fi/Gmail credential template (copy to config.h)
build/
  smart_home_1.X.production.hex         # Compiled PIC hex (MPLAB X)
  SMART_HOME_PROTEUS.X.production.hex   # Proteus simulation build
simulation/
  MicroProcesser_Final.pdsprj           # Proteus 8 circuit simulation project
Smart_Home_Automation_Report.pdf        # Full project report: schematics, protocol, test results
```

## Setup

### PIC firmware

Requires MPLAB X IDE with the MPASM/XC8 assembler and a PIC18F4550 target (Betamini board or equivalent). Open `firmware/pic/main.asm`, build, and program via the compiled hex in `build/`, or flash `build/smart_home_1.X.production.hex` directly with a PICkit.

### ESP32 firmware

```bash
# In Arduino IDE:
# 1. Install "ESP Mail Client" by Mobizt via Library Manager
# 2. Copy the config template and fill in your own credentials
cp firmware/esp32/config.example.h firmware/esp32/config.h
# Edit config.h: WIFI_SSID, WIFI_PASSWORD, EMAIL_SENDER, EMAIL_PASSWORD (Gmail App Password), EMAIL_RECIPIENT
# 3. Open smart_home_esp32.ino, select your ESP32 board, upload
```

`config.h` is gitignored — never commit real credentials. Use a [Gmail App Password](https://myaccount.google.com/apppasswords), not your account password.

### Wiring

ESP32 GPIO16/17 (RX/TX) connect to PIC RC6/RC7 through a bidirectional 5V↔3.3V logic-level shifter — see the pin table above and the full schematic in the report.

## Testing

All 17 functional test cases pass on the assembled hardware — boot sequence, LDR/DHT11 sensor reads, keypad and voice PIN entry, 3-strikes intrusion lockout with email delivery, PIR-armed alarm, fan hysteresis (including humidity-aware triggering), servo control via UART and dashboard, EEPROM log persistence across resets, and all six scene macros. Measured performance: LDR noise reduced from ±5% to ±1% by the moving-average filter, zero false PIR alarms in a 30-minute soak test, UART running at 9615 baud (0.16% error, well inside 1% tolerance for 8N1), and end-to-end voice-command latency of ~1.0–1.3 seconds. Full test matrix and engineering observations are in the [report](Smart_Home_Automation_Report.pdf).

## License

MIT — see [LICENSE](LICENSE).
