# ⚡ OCPP ESP32 Server

[![License](https://img.shields.io/badge/license-TBD-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-red.svg)](https://docs.espressif.com/projects/esp-idf/)
[![OCPP](https://img.shields.io/badge/OCPP-1.6J-green.svg)](https://www.openchargealliance.org/)
[![Python](https://img.shields.io/badge/Python-3.11+-yellow.svg)](https://python.org)
[![Status](https://img.shields.io/badge/status-in%20development-orange.svg)]()

ESP32-based OCPP 1.6J Central System that bridges EV charging stations (wallboxes) to an MQTT-based energy management system.

---

## 🏗️ Architecture

```
┌─────────────┐    Ethernet     ┌──────────────┐     WiFi      ┌──────────────┐
│   Wallbox   │◄───────────────│  ESP32 OCPP  │◄────────────►│ MQTT Broker  │
│  (Charger)  │  WebSocket/OCPP │    Server    │    MQTT       │              │
└─────────────┘                 └──────────────┘               └──────┬───────┘
                                       │                              │
                                       │                              ▼
                                ┌──────────────┐              ┌──────────────┐
                                │Captive Portal│              │   Energy     │
                                │  (Config UI) │              │   Manager    │
                                └──────────────┘              └──────────────┘
```

**Dual-network design:**
| Interface | Purpose | Protocol |
|-----------|---------|----------|
| 🔌 Ethernet (W5500) | Isolated wallbox connection | OCPP 1.6J over WebSocket |
| 📶 WiFi STA | Home/site network | MQTT to energy manager |
| 📡 WiFi AP | Configuration portal | HTTP captive portal |

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| ⚡ OCPP 1.6J | Custom Central System implementation |
| 🔋 Smart Charging | Dynamic power limits via charging profiles |
| 🔀 Phase Switching | 1-phase ↔ 3-phase via GPIO relays |
| 📊 Power Correction | Automatic meter value adjustment per phase mode |
| 🔄 OTA Updates | Web-based firmware upload with rollback |
| 🌐 Captive Portal | Zero-config WiFi/MQTT credential setup |
| 📡 MQTT Bridge | Full energy management integration |
| 🛡️ Safety Interlocks | Never switch relays under load |

---

## 🗂️ Project Structure

```
├── 📄 docs/
│   └── ocpp-esp32-fsd.md          # Functional Specification Document
├── 🧪 ocpp-test-simulator/        # Python test harness
│   ├── src/
│   │   ├── wallbox_emulator/      # OCPP charge point simulator
│   │   ├── mqtt_client/           # MQTT command/status client
│   │   └── scenarios/             # Automated test sequences
│   └── config/                    # Test configuration
└── 🔧 ocpp-esp32/                 # ESP32 firmware (planned)
```

---

## 🔧 Hardware

| Component | Specification |
|-----------|---------------|
| 🧠 MCU | ESP32-WROOM-32 or ESP32-S3 |
| 🔌 Ethernet | W5500 SPI module |
| 💾 Flash | 8MB recommended (OTA dual partition) |
| ⚡ Phase Relays | GPIO 25 (L1/NC), 26 (L2/NO), 27 (L3/NO) |
| 📍 Feedback | GPIO 34 (phase sense input) |

---

## 📖 Documentation

| Document | Description |
|----------|-------------|
| [Functional Spec](docs/ocpp-esp32-fsd.md) | Full FSD with requirements, architecture, test cases |
| [Test Simulator](ocpp-test-simulator/README.md) | Python test harness documentation |

---

## 📋 Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| 📄 FSD | Functional Specification | ✅ Complete |
| 🧪 Test Scaffold | Simulator directory structure | ✅ Complete |
| 🔧 Phase 1 | Core infrastructure (ETH, WiFi, NVS, LEDs) | ⬜ Planned |
| 🌐 Phase 2 | Captive portal & configuration | ⬜ Planned |
| 🔄 Phase 3 | OTA updates | ⬜ Planned |
| ⚡ Phase 4 | OCPP core (WebSocket, messages) | ⬜ Planned |
| 🔋 Phase 5 | Transactions & metering | ⬜ Planned |
| 📡 Phase 6 | MQTT bridge | ⬜ Planned |
| 🔀 Phase 7 | Phase switching | ⬜ Planned |
| 📊 Phase 8 | Smart charging | ⬜ Planned |
| 🛡️ Phase 9 | Security & advanced features | ⬜ Planned |

---

## 🚀 Getting Started

### Prerequisites

- [ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/get-started/)
- Python 3.11+ (for test simulator)

### Test Simulator Setup

```bash
cd ocpp-test-simulator
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

---

## 📜 License

TBD
