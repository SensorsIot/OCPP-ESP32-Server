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
                                                    ┌──────────────┐
                                    (Future)        │ MQTT Broker  │
                               ┌ ─ ─WiFi STA─ ─ ─ ─►│              │
                               │                    └──────┬───────┘
┌─────────────┐    Ethernet    │ ┌──────────────┐         │
│   Wallbox   │◄──────────────┼──│ WT32-ETH01   │         ▼
│  (Charger)  │  WebSocket/OCPP  │ OCPP Server  │  ┌──────────────┐
└─────────────┘   (LAN8720)   │  └──────────────┘  │   Energy     │
                              │         │          │   Manager    │
                              │    WiFi AP         └──────────────┘
                              │  (config mode)
                              │  ┌──────────────┐
                              └─►│Captive Portal│
                                 │  (Config UI) │
                                 └──────────────┘
```

**Network modes:**
| Mode | Interfaces | When |
|------|------------|------|
| **Normal** | 🔌 Ethernet only | `wifi_ssid` configured |
| **Config** | 📡 WiFi AP + Captive Portal | `wifi_ssid` empty or config button held |

**Planned (future):**
| Interface | Purpose | Protocol |
|-----------|---------|----------|
| 📶 WiFi STA | Home/site network | MQTT to energy manager |

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
├── 🧪 ocpp-test-wallbox/          # Python test harness
│   ├── src/
│   │   ├── wallbox_emulator/      # OCPP charge point simulator
│   │   ├── mqtt_client/           # MQTT command/status client
│   │   └── scenarios/             # Automated test sequences
│   └── config/                    # Test configuration
└── 🔧 ocpp-esp32/                 # ESP32 firmware (ESP-IDF v5.4)
    ├── main/main.c                # Boot logic, watchdog
    └── components/                # ESP-IDF components
        ├── board_pins/            # GPIO pin definitions
        ├── config_manager/        # NVS config persistence + JSON API
        ├── gpio_control/          # Phase relay + config button
        ├── ethernet_manager/      # W5500 SPI Ethernet
        ├── wifi_manager/          # WiFi STA/AP modes
        ├── captive_portal/        # Web UI + REST API
        ├── dns_server/            # Captive portal DNS redirect
        ├── ocpp_server/           # OCPP 1.6J WebSocket server
        ├── mqtt_manager/          # MQTT client bridge
        ├── phase_control/         # Phase switching state machine
        ├── ota_manager/           # OTA firmware updates
        └── console_cmd/           # Serial CLI REPL
```

---

## 🔧 Hardware

### Supported Boards

| Board | Ethernet | Notes |
|-------|----------|-------|
| **WT32-ETH01** | LAN8720 RMII | Recommended - integrated Ethernet |
| ESP32 + W5500 | W5500 SPI | External module required |

### Pin Configuration (WT32-ETH01)

| Function | GPIO | Description |
|----------|------|-------------|
| 🔌 ETH MDC | 23 | Ethernet clock |
| 🔌 ETH MDIO | 18 | Ethernet data |
| 🔌 ETH Power | 16 | PHY power enable |
| ⚡ Phase Relay | 25 | Single relay for L2+L3 |
| 🔘 Config Button | 14 | Enter config mode (hold 5s) |

---

## 📖 Documentation

| Document | Description |
|----------|-------------|
| [Functional Spec](docs/ocpp-esp32-fsd.md) | Full FSD with requirements, architecture, test cases |
| [Test Simulator](ocpp-test-wallbox/README.md) | Python test harness documentation |

---

## 📋 Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| 📄 FSD | Functional Specification | ✅ Complete |
| 🧪 Test Scaffold | Simulator directory structure | ✅ Complete |
| 🔧 Phase 1 | Core infrastructure (ETH, WiFi, NVS, GPIO, Console) | ✅ Complete |
| 🌐 Phase 2 | Captive portal & configuration | ✅ Complete |
| ⚡ Phase 4 | OCPP core (WebSocket server, BootNotification, Heartbeat) | ✅ Complete |
| 🔄 Phase 3 | OTA updates | ⬜ Planned |
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

### Build ESP32 Firmware

```bash
cd ocpp-esp32
source /path/to/esp-idf/export.sh
idf.py set-target esp32
idf.py build
```

### Flash via Local Serial

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Flash via RFC2217 (Remote Serial)

For remote flashing via Raspberry Pi with RFC2217 portal:

```bash
# Check available devices
curl http://PI_IP:8080/api/discover

# Start RFC2217 servers
curl -X POST http://PI_IP:8080/api/start-all

# Flash (use 921600 baud for faster uploads)
idf.py -p 'rfc2217://PI_IP:4001?ign_set_control' -b 921600 flash

# Monitor
idf.py -p 'rfc2217://PI_IP:4001?ign_set_control' monitor
```

### Serial Console

After flashing, connect at 115200 baud. Available commands:

| Command | Description |
|---------|-------------|
| `status` | System status (network, phase mode, heap) |
| `heap` | Free/minimum heap memory |
| `config` | Show all configuration values |
| `config_set <key> <value>` | Set a config value (persisted to NVS) |
| `wifi_scan` | Scan for nearby WiFi networks |
| `factory_reset` | Restore factory defaults |
| `reboot` | Reboot the device |

### Test Simulator Setup

```bash
cd ocpp-test-wallbox
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

---

## 📜 License

TBD
