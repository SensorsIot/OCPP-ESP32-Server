# ESP32 OCPP Server - Functional Specification Document

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.1 |
| Status | Draft |
| Created | 2026-01-25 |
| Updated | 2026-01-25 |

## 1. Overview

### 1.1 Purpose

This document specifies the functional requirements for an ESP32-based OCPP 1.6J Central System (server) that bridges OCPP-compliant EV charging stations (wallboxes) to an MQTT-based energy management system.

### 1.2 System Context

```
                                                          ┌────────────────┐
                                                          │  WiFi Router   │
                                                          │   (Internet)   │
                                                          └───────┬────────┘
                                                                  │ WiFi
                                                                  ▼
┌─────────────┐     Ethernet      ┌─────────────────┐      ┌─────────────┐     ┌────────────────┐
│   Wallbox   │◄──────────────────│  ESP32 OCPP     │◄─────│    WiFi     │────►│  MQTT Broker   │
│  (Charger)  │   WebSocket/OCPP  │     Server      │      │  (STA Mode) │     │                │
└─────────────┘                   └────────┬────────┘      └─────────────┘     └────────────────┘
                                           │                                           │
                                           │                                           ▼
                                  ┌────────┴────────┐                          ┌────────────────┐
                                  │  Captive Portal │                          │ Energy Manager │
                                  │   (AP Mode)     │                          │                │
                                  └─────────────────┘                          └────────────────┘

Network Separation:
- Ethernet: Dedicated link to wallbox (isolated network, OCPP WebSocket server)
- WiFi STA: Connection to home/site network for MQTT communication
- WiFi AP: Captive portal for initial configuration (when unconfigured or on demand)
```

### 1.3 Goals

1. Provide standalone OCPP 1.6J Central System functionality on ESP32
2. Use wired Ethernet for reliable, isolated wallbox communication
3. Use WiFi for MQTT communication with energy management system
4. Provide captive portal for easy credential configuration
5. Support OTA firmware updates for field maintenance
6. Minimize resource usage for stable long-term operation
7. Support single wallbox connection (expandable to multiple)

## 2. Hardware Requirements

### 2.1 Target Platform

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-WROOM-32 or ESP32-S3 |
| Ethernet | W5500 SPI Ethernet module (for wallbox) |
| WiFi | Built-in ESP32 WiFi (for MQTT) |
| Flash | Minimum 4MB (8MB recommended for OTA) |
| PSRAM | Optional but recommended (4-8MB) |
| Power | 5V DC via USB or barrel jack |

### 2.2 Network Architecture

```
┌──────────────────────────────────────────────────────────┐
│                        ESP32                              │
│  ┌─────────────────┐          ┌─────────────────────┐    │
│  │   W5500 SPI     │          │    Built-in WiFi    │    │
│  │   Ethernet      │          │                     │    │
│  │                 │          │  ┌───────────────┐  │    │
│  │  - OCPP Server  │          │  │  STA Mode     │  │    │
│  │  - WebSocket    │          │  │  - MQTT Client│  │    │
│  │  - Static IP    │          │  │  - NTP Sync   │  │    │
│  │    192.168.4.1  │          │  │  - OTA Updates│  │    │
│  │                 │          │  └───────────────┘  │    │
│  │                 │          │  ┌───────────────┐  │    │
│  │                 │          │  │  AP Mode      │  │    │
│  │                 │          │  │  - Captive    │  │    │
│  │                 │          │  │    Portal     │  │    │
│  │                 │          │  │  - Config UI  │  │    │
│  └────────┬────────┘          │  └───────────────┘  │    │
│           │                   └──────────┬──────────┘    │
└───────────┼──────────────────────────────┼───────────────┘
            │                              │
            ▼                              ▼
      ┌──────────┐                  ┌──────────────┐
      │ Wallbox  │                  │ WiFi Router  │
      │ (OCPP)   │                  │ (MQTT/Cloud) │
      └──────────┘                  └──────────────┘
```

### 2.3 Recommended Hardware Configurations

#### Option A: ESP32 DevKit + W5500 Module (Recommended)
- ESP32-WROOM-32 or ESP32-S3 DevKit
- W5500 Ethernet module (SPI interface)
- More flexible pin assignment
- Widely available, low cost

#### Option B: Custom PCB
- ESP32-WROOM module
- Integrated W5500
- RJ45 connector for wallbox
- External antenna for WiFi (optional)

### 2.4 Pin Assignments

| Function | GPIO | Notes |
|----------|------|-------|
| **SPI Ethernet (W5500)** |||
| ETH_MISO | 19 | SPI MISO |
| ETH_MOSI | 23 | SPI MOSI |
| ETH_SCK | 18 | SPI Clock |
| ETH_CS | 5 | Chip Select |
| ETH_INT | 4 | Interrupt (optional) |
| ETH_RST | 21 | Reset (optional) |
| **Status LEDs** |||
| LED_STATUS | 2 | Onboard LED (system status) |
| LED_OCPP | 15 | OCPP/Ethernet connection |
| LED_WIFI | 16 | WiFi connection status |
| LED_MQTT | 17 | MQTT connection status |
| **User Input** |||
| BTN_CONFIG | 0 | Boot button (hold for config mode) |
| **Phase Switching** |||
| RELAY_PHASE_1 | 25 | Phase 1 enable (always on when charging) |
| RELAY_PHASE_2 | 26 | Phase 2 enable relay |
| RELAY_PHASE_3 | 27 | Phase 3 enable relay |
| PHASE_SENSE | 34 | Phase configuration feedback (input) |

### 2.5 Flash Partition Layout (OTA Support)

```
┌─────────────────────────────────────────────────────────┐
│  Partition Table (8MB Flash)                            │
├─────────────────────────────────────────────────────────┤
│  nvs        │  0x9000  │  20KB   │ Configuration       │
│  otadata    │  0xE000  │   8KB   │ OTA state           │
│  app0       │ 0x10000  │ 1.5MB   │ Application (slot 1)│
│  app1       │ 0x190000 │ 1.5MB   │ Application (slot 2)│
│  spiffs     │ 0x310000 │ 1.5MB   │ Web UI files        │
│  coredump   │ 0x490000 │  64KB   │ Crash dumps         │
└─────────────────────────────────────────────────────────┘
```

## 3. Software Architecture

### 3.1 Framework and Libraries

| Component | Library | Version | Purpose |
|-----------|---------|---------|---------|
| Framework | ESP-IDF | 5.4 | Native C framework |
| Build System | CMake + Ninja | Built-in | ESP-IDF build toolchain |
| Ethernet | esp_eth (W5500 SPI) | Built-in | Wallbox connection |
| WiFi | esp_wifi | Built-in | MQTT/Internet connection |
| WebSocket Server | esp_http_server | Built-in | OCPP over Ethernet |
| HTTP Server | esp_http_server | Built-in | Captive portal UI |
| DNS Server | lwip | Built-in | Captive portal redirect |
| MQTT Client | mqtt (esp-mqtt) | Built-in | Energy manager comm |
| JSON | cJSON | Built-in | Message parsing |
| OCPP | **Custom Implementation** | - | OCPP 1.6J subset |
| OTA | esp_https_ota | Built-in | Firmware updates |
| NTP | esp_sntp | Built-in | Time sync |
| Config Storage | NVS Flash | Built-in | Persistent config |
| File System | SPIFFS | Built-in | Web UI storage |
| Console | esp_console | Built-in | Serial CLI REPL |

### 3.2 Module Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             Main Application                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────────────────────────┐  ┌──────────────────────────────────┐ │
│  │       Ethernet Domain            │  │         WiFi Domain              │ │
│  │  ┌────────────┐ ┌────────────┐   │  │  ┌────────────┐ ┌────────────┐  │ │
│  │  │ W5500      │ │ WebSocket  │   │  │  │ WiFi       │ │ MQTT       │  │ │
│  │  │ Manager    │ │ Server     │   │  │  │ Manager    │ │ Client     │  │ │
│  │  └─────┬──────┘ └─────┬──────┘   │  │  └─────┬──────┘ └─────┬──────┘  │ │
│  │        │              │          │  │        │              │         │ │
│  │  ┌─────┴──────────────┴──────┐   │  │  ┌─────┴──────────────┴──────┐  │ │
│  │  │      OCPP Handler         │   │  │  │    Captive Portal        │  │ │
│  │  │ ┌────────┐ ┌────────────┐ │   │  │  │ ┌─────────┐ ┌──────────┐ │  │ │
│  │  │ │Message │ │Transaction │ │   │  │  │ │DNS      │ │Web UI    │ │  │ │
│  │  │ │Router  │ │Manager     │ │   │  │  │ │Server   │ │Server    │ │  │ │
│  │  │ └────────┘ └────────────┘ │   │  │  │ └─────────┘ └──────────┘ │  │ │
│  │  │ ┌────────┐ ┌────────────┐ │   │  │  └──────────────────────────┘  │ │
│  │  │ │Charging│ │Smart       │ │   │  │  ┌──────────────────────────┐  │ │
│  │  │ │Profile │ │Charging    │ │   │  │  │      OTA Manager         │  │ │
│  │  │ └────────┘ └────────────┘ │   │  │  │ ┌─────────┐ ┌──────────┐ │  │ │
│  │  └───────────────────────────┘   │  │  │ │HTTP OTA │ │Partition │ │  │ │
│  └──────────────────────────────────┘  │  │ │Handler  │ │Manager   │ │  │ │
│                                        │  │ └─────────┘ └──────────┘ │  │ │
│                                        │  └──────────────────────────┘  │ │
│                                        └────────────────────────────────┘ │
├───────────────────────────────────────────────────────────────────────────┤
│                           Shared Services                                  │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐             │
│  │  Config    │ │   LED      │ │   NTP      │ │   Logger   │             │
│  │  Manager   │ │  Status    │ │   Sync     │ │            │             │
│  └────────────┘ └────────────┘ └────────────┘ └────────────┘             │
├───────────────────────────────────────────────────────────────────────────┤
│                        Hardware Abstraction                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │
│  │  W5500 SPI  │  │  WiFi PHY   │  │    LEDs     │  │  NVS/LittleFS│     │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘      │
└───────────────────────────────────────────────────────────────────────────┘
```

### 3.3 Task Structure (FreeRTOS)

| Task | Priority | Stack | Core | Description |
|------|----------|-------|------|-------------|
| Main Loop | 1 | 8KB | 0 | app_main(), watchdog feed |
| Ethernet | 2 | 4KB | 0 | W5500 Ethernet management |
| WiFi | 2 | 4KB | 0 | WiFi STA/AP management |
| WebSocket | 3 | 8KB | 1 | OCPP WebSocket server |
| MQTT | 2 | 4KB | 1 | MQTT client operations |
| OCPP Handler | 3 | 8KB | 1 | Message processing |
| Captive Portal | 1 | 4KB | 0 | Web UI and DNS server |
| OTA | 1 | 8KB | 0 | Firmware update handler |

### 3.4 Operating Modes

```
                    ┌─────────────────────┐
                    │    Power On/Reset   │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
           ┌───────│  Check Config Valid  │───────┐
           │       └─────────────────────┘       │
           │ No                                   │ Yes
           ▼                                      ▼
┌─────────────────────┐                ┌─────────────────────┐
│   CONFIG MODE       │                │   NORMAL MODE       │
│  - WiFi AP active   │                │  - WiFi STA active  │
│  - Captive portal   │◄──BTN_HOLD────│  - Ethernet active  │
│  - No OCPP/MQTT     │                │  - OCPP running     │
│  - OTA available    │                │  - MQTT connected   │
└──────────┬──────────┘                │  - OTA available    │
           │                           └─────────────────────┘
           │ Config Saved
           └──────────────────────────────────────┘
```

| Mode | WiFi | Ethernet | OCPP | MQTT | Portal | OTA |
|------|------|----------|------|------|--------|-----|
| Config | AP | Inactive | No | No | Yes | Yes |
| Normal | STA | Active | ETH only | Yes | No | Yes |
| Test | STA | Optional | ETH + WiFi | Yes | No | Yes |
| Fallback | AP+STA | Active | ETH only | No | Yes | Yes |

## 4. Functional Requirements

### 4.1 Network Management

#### 4.1.1 Ethernet Connection (Wallbox Network)
- **ETH-001**: System SHALL initialize W5500 Ethernet on boot
- **ETH-002**: System SHALL use static IP for Ethernet (default: 192.168.4.1)
- **ETH-003**: System SHALL monitor link status and indicate via LED
- **ETH-004**: System SHALL support configurable static IP settings
- **ETH-005**: Ethernet network SHALL be isolated from WiFi network in Normal mode
- **ETH-006**: OCPP WebSocket server SHALL bind to Ethernet interface in Normal mode
- **ETH-007**: In Test mode, OCPP WebSocket server SHALL also bind to WiFi interface

#### 4.1.2 WiFi Station Mode (MQTT/Internet Network)
- **WIFI-001**: System SHALL connect to configured WiFi network in STA mode
- **WIFI-002**: WiFi credentials SHALL be stored encrypted in NVS
- **WIFI-003**: System SHALL automatically reconnect on WiFi disconnect
- **WIFI-004**: System SHALL indicate WiFi connection status via LED
- **WIFI-005**: System SHALL support WPA2/WPA3 authentication
- **WIFI-006**: MQTT client SHALL only use WiFi interface
- **WIFI-007**: System SHALL sync time via NTP over WiFi

#### 4.1.3 WiFi Access Point Mode (Configuration)
- **AP-001**: System SHALL start AP mode when no valid WiFi config exists
- **AP-002**: System SHALL start AP mode when BTN_CONFIG held for 5 seconds
- **AP-003**: AP SHALL use SSID format: `OCPP-ESP32-{MAC_LAST_4}`
- **AP-004**: AP SHALL use configurable password (default: `ocpp12345`)
- **AP-005**: AP SHALL assign IP 192.168.1.1 to clients
- **AP-006**: System MAY run AP and STA concurrently (fallback mode)

#### 4.1.4 Test Mode
- **TEST-001**: System SHALL support a "Test mode" configurable via captive portal or NVS
- **TEST-002**: In Test mode, WebSocket server SHALL listen on ALL interfaces (ETH + WiFi)
- **TEST-003**: In Test mode, wallbox emulator MAY connect via WiFi instead of Ethernet
- **TEST-004**: Test mode SHALL be indicated by rapid alternating LED blink pattern
- **TEST-005**: Test mode allows full operation without Ethernet hardware connected

```
  Test Mode Network Topology:
  ┌─────────────────────────────────────────────────────────────┐
  │                     WiFi Network                            │
  │                                                             │
  │  ┌───────────────────┐         ┌───────────────────────┐   │
  │  │ Python Simulator  │         │    ESP32 OCPP Server  │   │
  │  │                   │         │                       │   │
  │  │ Wallbox Emulator ─┼─ WS ──►│ WebSocket (WiFi:9000) │   │
  │  │                   │         │                       │   │
  │  │ MQTT Client ──────┼─ MQTT ►│ MQTT Client ──────────┼───┤
  │  └───────────────────┘         └───────────────────────┘   │
  │                                                      │     │
  │                                              ┌───────┴───┐ │
  │                                              │MQTT Broker│ │
  │                                              └───────────┘ │
  └─────────────────────────────────────────────────────────────┘
```

### 4.2 Captive Portal

#### 4.2.1 DNS Redirect
- **CP-001**: System SHALL run DNS server in AP mode
- **CP-002**: DNS server SHALL redirect all queries to portal IP (192.168.1.1)
- **CP-003**: System SHALL respond to captive portal detection requests

#### 4.2.2 Web Interface
- **CP-010**: Portal SHALL serve responsive HTML/CSS/JS interface
- **CP-011**: Portal SHALL be stored in LittleFS partition
- **CP-012**: Portal SHALL work without external dependencies (offline)
- **CP-013**: Portal SHALL support modern browsers (Chrome, Firefox, Safari)

#### 4.2.3 Configuration Pages

| Page | URL | Purpose |
|------|-----|---------|
| Home | `/` | Status overview, navigation |
| WiFi Setup | `/wifi` | Scan networks, enter credentials |
| MQTT Setup | `/mqtt` | Broker address, credentials |
| Ethernet | `/ethernet` | Static IP configuration |
| OCPP | `/ocpp` | WebSocket port, settings |
| System | `/system` | Device name, reboot, factory reset |
| Firmware | `/update` | OTA firmware upload |
| Status | `/api/status` | JSON status endpoint |

#### 4.2.4 Configuration Form Fields

**WiFi Configuration:**
```
┌─────────────────────────────────────────┐
│  WiFi Configuration                     │
├─────────────────────────────────────────┤
│  Available Networks: [Scan]             │
│  ┌─────────────────────────────────┐   │
│  │ ● HomeNetwork       (-45 dBm)   │   │
│  │ ○ OfficeWiFi        (-62 dBm)   │   │
│  │ ○ GuestNetwork      (-78 dBm)   │   │
│  └─────────────────────────────────┘   │
│                                         │
│  SSID:     [____________________]       │
│  Password: [____________________] 👁    │
│                                         │
│  [ ] Use static IP                      │
│  IP:      [___.___.___.___ ]           │
│  Gateway: [___.___.___.___ ]           │
│  Subnet:  [___.___.___.___ ]           │
│  DNS:     [___.___.___.___ ]           │
│                                         │
│           [Save & Connect]              │
└─────────────────────────────────────────┘
```

**MQTT Configuration:**
```
┌─────────────────────────────────────────┐
│  MQTT Configuration                     │
├─────────────────────────────────────────┤
│  Broker:   [____________________]:1883  │
│  Username: [____________________]       │
│  Password: [____________________] 👁    │
│                                         │
│  [ ] Use TLS (port 8883)               │
│                                         │
│  Topic Prefix: [ocpp___________]        │
│  Client ID:    [ocpp-esp32-____]        │
│                                         │
│           [Test Connection]             │
│           [Save]                        │
└─────────────────────────────────────────┘
```

### 4.3 WebSocket Server (OCPP)

#### 4.3.1 Server Configuration
- **WS-001**: Server SHALL listen on configurable port (default: 9000)
- **WS-002**: Server SHALL accept WebSocket connections at path `/ocpp/{chargePointId}`
- **WS-003**: Server SHALL support OCPP 1.6 subprotocol (`ocpp1.6`)
- **WS-004**: Server SHALL handle WebSocket ping/pong for keepalive
- **WS-005**: Server SHALL support maximum 2 concurrent connections

#### 4.3.2 Connection Management
- **WS-010**: Server SHALL track connected charge points by ID
- **WS-011**: Server SHALL detect disconnection within 30 seconds
- **WS-012**: Server SHALL publish connection status to MQTT
- **WS-013**: Server SHALL log connection events

### 4.4 OCPP 1.6J Implementation

#### 4.4.1 Supported Messages (Charger → Server)

| Message | Priority | Description |
|---------|----------|-------------|
| BootNotification | Required | Charger registration |
| Heartbeat | Required | Connection keepalive |
| StatusNotification | Required | Connector status changes |
| Authorize | Required | RFID/authorization requests |
| StartTransaction | Required | Charging session start |
| StopTransaction | Required | Charging session end |
| MeterValues | Required | Energy consumption data |
| DataTransfer | Optional | Vendor-specific data |
| DiagnosticsStatusNotification | Optional | Diagnostics upload status |
| FirmwareStatusNotification | Optional | Firmware update status |

#### 4.4.2 Supported Messages (Server → Charger)

| Message | Priority | Description |
|---------|----------|-------------|
| RemoteStartTransaction | Required | Remote charge start |
| RemoteStopTransaction | Required | Remote charge stop |
| ChangeAvailability | Required | Enable/disable connector |
| SetChargingProfile | Required | Smart charging control |
| ClearChargingProfile | Required | Remove charging limits |
| GetConfiguration | Required | Read charger config |
| ChangeConfiguration | Required | Modify charger config |
| Reset | Required | Soft/hard reset |
| UnlockConnector | Optional | Remote cable unlock |
| TriggerMessage | Optional | Request specific message |
| UpdateFirmware | Optional | Firmware update |
| GetDiagnostics | Optional | Request diagnostics |

#### 4.4.3 Message Handling Requirements

- **OCPP-001**: System SHALL parse OCPP-J messages (JSON over WebSocket)
- **OCPP-002**: System SHALL validate message format and required fields
- **OCPP-003**: System SHALL respond within 30 seconds
- **OCPP-004**: System SHALL maintain message ID uniqueness
- **OCPP-005**: System SHALL handle Call, CallResult, and CallError
- **OCPP-006**: System SHALL queue outgoing messages if connection busy
- **OCPP-007**: System SHALL retry failed requests (configurable count)

### 4.5 Smart Charging (Charging Profiles)

#### 4.5.1 Profile Support
- **SC-001**: System SHALL support TxDefaultProfile (default limits)
- **SC-002**: System SHALL support TxProfile (transaction-specific)
- **SC-003**: System SHALL support ChargePointMaxProfile (absolute max)
- **SC-004**: System SHALL support Relative and Absolute schedule types
- **SC-005**: System SHALL support charging schedule periods

#### 4.5.2 Charging Profile Structure

```json
{
  "chargingProfileId": 1,
  "stackLevel": 0,
  "chargingProfilePurpose": "TxDefaultProfile",
  "chargingProfileKind": "Absolute",
  "chargingSchedule": {
    "chargingRateUnit": "A",
    "chargingSchedulePeriod": [
      {"startPeriod": 0, "limit": 16.0},
      {"startPeriod": 3600, "limit": 8.0}
    ]
  }
}
```

### 4.6 MQTT Integration

#### 4.6.1 Connection
- **MQTT-001**: System SHALL connect to configurable MQTT broker
- **MQTT-002**: System SHALL support MQTT over TCP (port 1883) and TLS (8883)
- **MQTT-003**: System SHALL authenticate with username/password
- **MQTT-004**: System SHALL use configurable client ID
- **MQTT-005**: System SHALL reconnect automatically on disconnect
- **MQTT-006**: System SHALL indicate MQTT status via LED

#### 4.6.2 Topic Structure

Base topic: `ocpp/{chargepoint_id}/` (configurable prefix)

| Topic | Direction | QoS | Description |
|-------|-----------|-----|-------------|
| `status` | Publish | 1 | Connector status |
| `session` | Publish | 1 | Active session info |
| `meter` | Publish | 0 | Meter values |
| `availability` | Publish | 1 | Availability state |
| `error` | Publish | 1 | Error codes |
| `command/start` | Subscribe | 1 | Start charging |
| `command/stop` | Subscribe | 1 | Stop charging |
| `command/limit` | Subscribe | 1 | Set current limit |
| `command/availability` | Subscribe | 1 | Set availability |
| `command/reset` | Subscribe | 1 | Reset charger |
| `command/config/get` | Subscribe | 1 | Get configuration |
| `command/config/set` | Subscribe | 1 | Set configuration |

#### 4.6.3 Message Formats

**Status Message (Published)**
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "connector_id": 1,
  "status": "Charging",
  "error_code": "NoError",
  "vendor_error": ""
}
```

**Session Message (Published)**
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "transaction_id": 12345,
  "connector_id": 1,
  "id_tag": "RFID123456",
  "meter_start": 1000,
  "meter_current": 1500,
  "energy_kwh": 0.5,
  "duration_seconds": 1800,
  "current_power_kw": 7.4
}
```

**Meter Values Message (Published)**
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "connector_id": 1,
  "transaction_id": 12345,
  "values": {
    "energy_wh": 1500,
    "power_w": 7400,
    "current_a": 32.0,
    "voltage_v": 230,
    "soc_percent": 45
  }
}
```

**Start Command (Subscribed)**
```json
{
  "connector_id": 1,
  "id_tag": "ENERGY_MANAGER"
}
```

**Limit Command (Subscribed)**
```json
{
  "connector_id": 1,
  "current_limit_a": 16.0,
  "duration_seconds": 3600
}
```

### 4.7 Authorization

#### 4.7.1 Authorization Modes
- **AUTH-001**: System SHALL support "accept all" mode (no authorization)
- **AUTH-002**: System SHALL support local whitelist (stored in NVS)
- **AUTH-003**: System SHALL support MQTT-based authorization
- **AUTH-004**: System SHALL cache authorized tags locally

#### 4.7.2 Authorization Flow

```
Wallbox                    ESP32                     MQTT/Manager
   │                         │                            │
   │──Authorize(idTag)──────►│                            │
   │                         │──auth/request/{id}────────►│
   │                         │                            │
   │                         │◄─auth/response/{id}────────│
   │◄─AuthorizeConf(status)──│                            │
```

### 4.8 Configuration Management

#### 4.8.1 Storage
- **CFG-001**: Configuration SHALL be stored in ESP32 NVS
- **CFG-002**: Configuration SHALL persist across reboots
- **CFG-003**: Configuration SHALL be modifiable via serial console
- **CFG-004**: Configuration SHALL be modifiable via MQTT
- **CFG-005**: Factory reset SHALL restore defaults

#### 4.8.2 Configuration Parameters

**Device Settings:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `device_name` | string | "ocpp-esp32" | Device identifier |
| `log_level` | enum | "info" | Logging verbosity |
| `test_mode` | bool | false | Enable test mode (WS on WiFi) |

**Ethernet Settings (Wallbox Network):**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `eth_ip` | string | "192.168.4.1" | Static IP address |
| `eth_subnet` | string | "255.255.255.0" | Subnet mask |
| `eth_gateway` | string | "192.168.4.1" | Gateway (self) |

**WiFi Settings (MQTT Network):**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `wifi_ssid` | string | "" | WiFi network name |
| `wifi_pass` | string | "" | WiFi password (encrypted) |
| `wifi_dhcp` | bool | true | Use DHCP for WiFi |
| `wifi_ip` | string | "" | Static IP (if DHCP off) |
| `wifi_gateway` | string | "" | Gateway IP |
| `wifi_subnet` | string | "" | Subnet mask |
| `wifi_dns` | string | "" | DNS server |

**Access Point Settings:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ap_ssid` | string | "OCPP-ESP32-XXXX" | AP network name |
| `ap_pass` | string | "ocpp12345" | AP password |
| `ap_timeout` | uint16 | 300 | AP auto-disable (seconds, 0=never) |

**OCPP Settings:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ws_port` | uint16 | 9000 | WebSocket server port |
| `heartbeat_interval` | uint16 | 60 | Heartbeat interval (s) |
| `meter_interval` | uint16 | 30 | Meter sample interval (s) |
| `auth_mode` | enum | "accept_all" | Authorization mode |

**MQTT Settings:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mqtt_host` | string | "" | MQTT broker address |
| `mqtt_port` | uint16 | 1883 | MQTT broker port |
| `mqtt_user` | string | "" | MQTT username |
| `mqtt_pass` | string | "" | MQTT password (encrypted) |
| `mqtt_prefix` | string | "ocpp" | MQTT topic prefix |
| `mqtt_tls` | bool | false | Use TLS for MQTT |
| `mqtt_client_id` | string | "ocpp-esp32-XXXX" | MQTT client ID |

**Phase Switching Settings:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `phase_mode` | enum | "auto" | Phase mode (1, 3, auto) |
| `phase_switch_delay` | uint16 | 5000 | Delay before switching (ms) |
| `phase_1_max_current` | float | 16.0 | Max current single phase (A) |
| `phase_3_max_current` | float | 16.0 | Max current per phase 3-ph (A) |
| `phase_switch_threshold` | float | 4.2 | Power threshold for switch (kW) |

**OTA Settings:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ota_enabled` | bool | true | Enable OTA updates |
| `ota_url` | string | "" | URL for auto-update check |
| `ota_check_interval` | uint32 | 86400 | Auto-check interval (s) |

### 4.9 OTA (Over-The-Air) Updates

#### 4.9.1 Update Methods
- **OTA-001**: System SHALL support firmware upload via captive portal web UI
- **OTA-002**: System SHALL support firmware upload via HTTP POST to `/update`
- **OTA-003**: System MAY support automatic update check from configured URL
- **OTA-004**: System SHALL support ArduinoOTA for development

#### 4.9.2 Update Process

```
┌─────────────────────────────────────────────────────────────┐
│                    OTA Update Flow                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐              │
│  │ Receive  │───►│ Validate │───►│  Write   │              │
│  │ Firmware │    │  Header  │    │ Partition│              │
│  └──────────┘    └──────────┘    └────┬─────┘              │
│                                       │                     │
│       ┌───────────────────────────────┘                    │
│       │                                                     │
│       ▼                                                     │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐              │
│  │  Verify  │───►│  Update  │───►│  Reboot  │              │
│  │ Checksum │    │ Boot Ptr │    │  System  │              │
│  └──────────┘    └──────────┘    └──────────┘              │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 4.9.3 Update Requirements
- **OTA-010**: System SHALL verify firmware signature/checksum before applying
- **OTA-011**: System SHALL support rollback to previous firmware on boot failure
- **OTA-012**: System SHALL preserve configuration across updates
- **OTA-013**: System SHALL reject firmware larger than OTA partition
- **OTA-014**: System SHALL indicate update progress via LED and web UI
- **OTA-015**: System SHALL disable OCPP operations during update
- **OTA-016**: System SHALL complete pending transactions before update

#### 4.9.4 Web UI Update Page

```
┌─────────────────────────────────────────┐
│  Firmware Update                        │
├─────────────────────────────────────────┤
│                                         │
│  Current Version: 1.2.3                 │
│  Build Date: 2026-01-25                 │
│  Free Space: 1.5 MB                     │
│                                         │
│  ┌─────────────────────────────────┐   │
│  │  Select firmware file (.bin)    │   │
│  │  [Browse...]                    │   │
│  └─────────────────────────────────┘   │
│                                         │
│  ████████████░░░░░░░░░░░░ 45%          │
│  Uploading firmware...                  │
│                                         │
│  [Upload Firmware]                      │
│                                         │
│  ⚠ Do not power off during update      │
└─────────────────────────────────────────┘
```

### 4.10 Phase Switching Control

#### 4.10.1 Overview

The system controls 1-phase to 3-phase switching via GPIO-controlled relays. This enables:
- Maximum single-phase charging when grid capacity is limited
- Full 3-phase charging when available power allows
- Dynamic switching based on energy management commands

**CRITICAL SAFETY REQUIREMENT**: Relays MUST NEVER be switched while power is flowing to the vehicle. The switching sequence requires stopping the transaction first.

```
┌─────────────────────────────────────────────────────────────┐
│                   Phase Switching Circuit                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   Grid Input                         To Wallbox              │
│   ─────────                          ─────────               │
│                                                              │
│   L1 ●────────────[RELAY_1]────────────────────────● L1     │
│                      (NC)                                    │
│                                                              │
│   L2 ●────────────[RELAY_2]────────────────────────● L2     │
│                      (NO)                                    │
│                                                              │
│   L3 ●────────────[RELAY_3]────────────────────────● L3     │
│                      (NO)                                    │
│                                                              │
│   N  ●─────────────────────────────────────────────● N      │
│                                                              │
│   PE ●─────────────────────────────────────────────● PE     │
│                                                              │
│   NC = Normally Closed (always connected)                   │
│   NO = Normally Open (connected when energized)             │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 4.10.2 Power Reporting Correction

The wallbox always reports power as if operating in 3-phase mode. The OCPP server MUST correct the reported values based on actual phase configuration:

| Phase Mode | Wallbox Reports | OCPP Server Reports | Correction |
|------------|-----------------|---------------------|------------|
| 3-phase | 11.0 kW | 11.0 kW | None (1:1) |
| 1-phase | 11.0 kW | 3.67 kW | Divide by 3 |

**Power Correction Requirements:**
- **PWR-001**: When in 1-phase mode, reported power SHALL be divided by 3
- **PWR-002**: When in 3-phase mode, reported power SHALL be passed through unchanged
- **PWR-003**: Energy values (Wh) SHALL be corrected the same way
- **PWR-004**: Current values SHALL be reported for active phase only in 1-phase mode
- **PWR-005**: Corrected values SHALL be published to MQTT
- **PWR-006**: Corrected values SHALL be used in MeterValues to energy manager

```
┌─────────────────────────────────────────────────────────────┐
│                   Power Value Flow                           │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Wallbox                 OCPP Server              MQTT       │
│  ────────                ───────────              ────       │
│                                                              │
│  MeterValues ──────────► Check Phase Mode                   │
│  (3-phase values)              │                            │
│                                ▼                            │
│                    ┌─────────────────────┐                  │
│                    │ 3-phase? │ 1-phase? │                  │
│                    └────┬─────┴────┬─────┘                  │
│                         │          │                        │
│                    Pass Through   Divide by 3               │
│                         │          │                        │
│                         └────┬─────┘                        │
│                              │                              │
│                              ▼                              │
│                    Publish Corrected ────────────► meter    │
│                    Values                         topic     │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 4.10.3 Phase Control Requirements

- **PHASE-001**: System SHALL control phases via GPIO outputs
- **PHASE-002**: Phase 1 relay SHALL be normally closed (failsafe)
- **PHASE-003**: Phases 2 and 3 relays SHALL be normally open
- **PHASE-004**: System SHALL NEVER switch relays while charging is active
- **PHASE-005**: System SHALL stop transaction before phase switching
- **PHASE-006**: System SHALL verify wallbox status is "Available" before switching
- **PHASE-007**: System SHALL wait for configurable delay after stop before switching
- **PHASE-008**: System SHALL start new transaction after successful switch
- **PHASE-009**: System SHALL verify phase state via feedback input
- **PHASE-010**: System SHALL report phase configuration to MQTT

#### 4.10.4 Phase Switching Sequence (CRITICAL)

The phase switching process MUST follow this exact sequence:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PHASE SWITCHING SEQUENCE                              │
│                    (Safety-Critical Process)                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  1. RECEIVE SWITCH COMMAND                                              │
│     └─► Validate: different from current phase mode                     │
│     └─► Store: target phase mode, original id_tag                       │
│                                                                          │
│  2. STOP ACTIVE TRANSACTION (if any)                                    │
│     └─► Send: RemoteStopTransaction to wallbox                          │
│     └─► Wait: StopTransaction.conf from wallbox                         │
│     └─► Record: final meter value, transaction data                     │
│                                                                          │
│  3. WAIT FOR SAFE STATE                                                 │
│     └─► Monitor: StatusNotification from wallbox                        │
│     └─► Require: status == "Available" or "Finishing"                   │
│     └─► Timeout: configurable (default 30 seconds)                      │
│     └─► On timeout: abort switch, report error                          │
│                                                                          │
│  4. ADDITIONAL SAFETY DELAY                                             │
│     └─► Wait: phase_switch_delay (default 5 seconds)                    │
│     └─► Purpose: ensure no residual current                             │
│                                                                          │
│  5. SWITCH RELAYS                                                       │
│     └─► Set GPIO: according to target phase mode                        │
│     └─► 1-phase: RELAY_2=OFF, RELAY_3=OFF                              │
│     └─► 3-phase: RELAY_2=ON, RELAY_3=ON                                │
│                                                                          │
│  6. VERIFY SWITCH                                                       │
│     └─► Read: PHASE_SENSE feedback input                                │
│     └─► Compare: expected vs actual state                               │
│     └─► On mismatch: report error, do NOT restart                       │
│                                                                          │
│  7. RESTART TRANSACTION (if was charging)                               │
│     └─► Send: RemoteStartTransaction with original id_tag               │
│     └─► Wait: StartTransaction.conf from wallbox                        │
│     └─► Update: phase mode in session state                             │
│                                                                          │
│  8. REPORT RESULT                                                       │
│     └─► Publish: phase switch result to MQTT                            │
│     └─► Include: old mode, new mode, success/failure, error reason      │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 4.10.5 Phase Switching State Machine

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      Phase Switch State Machine                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│    ┌──────────────┐                                                     │
│    │    IDLE      │◄──────────────────────────────────────────┐        │
│    │  (1ph/3ph)   │                                           │        │
│    └──────┬───────┘                                           │        │
│           │                                                    │        │
│     Switch Command                                             │        │
│           │                                                    │        │
│           ▼                                                    │        │
│    ┌──────────────┐         Timeout                           │        │
│    │   STOPPING   │─────────────────────────────────►  ERROR ─┘        │
│    │ (RemoteStop) │                                                     │
│    └──────┬───────┘                                                     │
│           │                                                             │
│     StopTransaction.conf                                                │
│           │                                                             │
│           ▼                                                             │
│    ┌──────────────┐         Timeout                                    │
│    │   WAITING    │─────────────────────────────────►  ERROR ──┐       │
│    │ (Available)  │                                            │       │
│    └──────┬───────┘                                            │       │
│           │                                                    │       │
│     Status = Available                                         │       │
│           │                                                    │       │
│           ▼                                                    │       │
│    ┌──────────────┐                                            │       │
│    │    DELAY     │                                            │       │
│    │  (5 sec)     │                                            │       │
│    └──────┬───────┘                                            │       │
│           │                                                    │       │
│     Timer Done                                                 │       │
│           │                                                    │       │
│           ▼                                                    │       │
│    ┌──────────────┐       Feedback Mismatch                   │       │
│    │  SWITCHING   │─────────────────────────────────►  ERROR ──┤       │
│    │   (GPIO)     │                                            │       │
│    └──────┬───────┘                                            │       │
│           │                                                    │       │
│     Verify OK                                                  │       │
│           │                                                    │       │
│           ▼                                                    │       │
│    ┌──────────────┐         Timeout                           │       │
│    │  STARTING    │─────────────────────────────────►  ERROR ──┤       │
│    │(RemoteStart) │                                            │       │
│    └──────┬───────┘                                            │       │
│           │                                                    │       │
│     StartTransaction.conf                                      │       │
│           │                                                    ▼       │
│           ▼                                            ┌────────────┐  │
│    ┌──────────────┐                                    │   ERROR    │  │
│    │   SUCCESS    │                                    │  (report)  │  │
│    │  (new mode)  │                                    └──────┬─────┘  │
│    └──────┬───────┘                                           │        │
│           │                                                   │        │
│           └───────────────────────────────────────────────────┴────────┘
│                              Return to IDLE                             │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 4.10.6 MQTT Phase Control

**Phase Status (Published):**
Topic: `ocpp/{charger_id}/phase`
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "phase_mode": "3-phase",
  "phases_active": [true, true, true],
  "can_switch": true,
  "switching_in_progress": false,
  "switch_state": "idle",
  "power_correction_factor": 1.0
}
```

**Phase Switch Result (Published):**
Topic: `ocpp/{charger_id}/phase/result`
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "success": true,
  "old_mode": "3-phase",
  "new_mode": "1-phase",
  "transaction_stopped": 12345,
  "transaction_started": 12346,
  "switch_duration_ms": 8500,
  "error": null
}
```

**Phase Command (Subscribed):**
Topic: `ocpp/{charger_id}/command/phase`
```json
{
  "mode": "1-phase"
}
```
Valid modes: `"1-phase"`, `"3-phase"`, `"auto"`

#### 4.10.8 Automatic Phase Switching Logic

Phase switching is triggered by MQTT power limit commands from the energy manager:

| Requested Power | Phase Mode | Rationale |
|-----------------|------------|-----------|
| < 4.1 kW | 1-phase | Single phase sufficient (max ~3.7 kW @ 16A) |
| >= 4.1 kW | 3-phase | Requires 3-phase for higher power |

**Command Example (Power Limit):**
Topic: `ocpp/{charger_id}/command/limit`
```json
{
  "connector_id": 1,
  "power_limit_kw": 7.4
}
```

**Auto-Switch Logic:**
```
┌─────────────────────────────────────────────────────────────┐
│            Automatic Phase Switch Decision                   │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   Receive power_limit_kw from MQTT                          │
│                    │                                         │
│                    ▼                                         │
│        ┌─────────────────────┐                              │
│        │ power_limit < 4.1kW │                              │
│        └──────────┬──────────┘                              │
│           Yes     │      No                                 │
│            │      │       │                                 │
│            ▼      │       ▼                                 │
│    ┌────────────┐ │  ┌────────────┐                         │
│    │ Target:    │ │  │ Target:    │                         │
│    │ 1-PHASE    │ │  │ 3-PHASE    │                         │
│    └─────┬──────┘ │  └─────┬──────┘                         │
│          │        │        │                                │
│          └────────┼────────┘                                │
│                   ▼                                         │
│        ┌─────────────────────┐                              │
│        │ Current == Target?  │                              │
│        └──────────┬──────────┘                              │
│           Yes     │      No                                 │
│            │      │       │                                 │
│            ▼      │       ▼                                 │
│    ┌────────────┐ │  ┌────────────┐                         │
│    │ No action  │ │  │ Initiate   │                         │
│    │ (set limit)│ │  │ Phase      │                         │
│    └────────────┘ │  │ Switch     │                         │
│                   │  └────────────┘                         │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**After Phase Switch:**
- Calculate appropriate current limit for the new phase mode
- Apply charging profile to wallbox via SetChargingProfile
- 1-phase: current = power_limit_kw / 0.23 (max 16A)
- 3-phase: current = power_limit_kw / (3 × 0.23) per phase

#### 4.10.7 Safety Interlocks

- **SAFETY-001**: Relays SHALL NEVER be switched while current is flowing
- **SAFETY-002**: Transaction MUST be stopped before any phase switching
- **SAFETY-003**: System SHALL wait for "Available" status before switching
- **SAFETY-004**: System SHALL apply safety delay after stop before switching
- **SAFETY-005**: System SHALL verify relay feedback matches commanded state
- **SAFETY-006**: On ANY error, system SHALL remain in current phase configuration
- **SAFETY-007**: System SHALL report all switching errors to MQTT
- **SAFETY-008**: If switch fails, system SHALL NOT restart transaction automatically

### 4.11 Status Indication

#### 4.11.1 LED Patterns

| LED | Pattern | Meaning |
|-----|---------|---------|
| Status | Solid | System running normally |
| Status | Fast blink (5Hz) | Initializing / Booting |
| Status | Slow blink (1Hz) | Error state |
| Status | Double blink | OTA update in progress |
| WiFi | Off | WiFi disconnected |
| WiFi | Slow blink | Connecting to WiFi |
| WiFi | Solid | WiFi connected |
| WiFi | Fast blink | AP mode active (config) |
| OCPP | Off | No charger connected |
| OCPP | Solid | Charger connected |
| OCPP | Blink | OCPP traffic |
| MQTT | Off | MQTT disconnected |
| MQTT | Slow blink | Connecting to broker |
| MQTT | Solid | MQTT connected |
| MQTT | Blink | MQTT traffic |

### 4.12 Logging and Diagnostics

#### 4.12.1 Logging
- **LOG-001**: System SHALL log to serial console
- **LOG-002**: System SHALL support log levels (error, warn, info, debug)
- **LOG-003**: System SHALL include timestamps in logs
- **LOG-004**: System MAY publish logs to MQTT (debug topic)

#### 4.12.2 Diagnostics
- **DIAG-001**: System SHALL expose status via MQTT
- **DIAG-002**: System SHALL report memory usage
- **DIAG-003**: System SHALL report uptime
- **DIAG-004**: System SHALL report network statistics

## 5. Non-Functional Requirements

### 5.1 Performance

| Metric | Requirement |
|--------|-------------|
| Boot time | < 10 seconds to operational |
| OCPP response time | < 500ms for local operations |
| MQTT publish latency | < 100ms |
| Message throughput | 10 messages/second minimum |
| Memory headroom | > 50KB free heap at steady state |

### 5.2 Reliability

- **REL-001**: System SHALL operate continuously without restart for 30+ days
- **REL-002**: System SHALL recover from network outages automatically
- **REL-003**: System SHALL not lose transaction data on power loss
- **REL-004**: System SHALL use watchdog timer (5 second timeout)

### 5.3 Security

- **SEC-001**: System SHALL support TLS 1.2+ for MQTT
- **SEC-002**: System SHALL validate WebSocket subprotocol
- **SEC-003**: System SHALL sanitize all external input
- **SEC-004**: System SHOULD support WebSocket Secure (WSS) - future
- **SEC-005**: System SHALL store passwords encrypted in NVS

### 5.4 Resource Constraints

| Resource | Limit | Notes |
|----------|-------|-------|
| Flash usage | < 1.5MB | Leaves room for OTA |
| RAM usage | < 200KB | With safety margin |
| PSRAM usage | < 2MB | If available |
| Max JSON size | 4KB | Per message |
| Max connections | 2 | WebSocket clients |

## 6. Implementation Phases

### Phase 1: Core Infrastructure
- W5500 Ethernet initialization
- WiFi STA/AP mode management
- NVS configuration storage
- LED status indicators
- Serial console interface
- Basic GPIO setup (phase relays)

### Phase 2: Captive Portal & Configuration
- DNS server for captive portal
- Web server on WiFi AP
- Configuration web UI (HTML/CSS/JS)
- WiFi network scanning
- Configuration persistence
- Factory reset functionality

### Phase 3: OTA Updates
- HTTP firmware upload endpoint
- Partition management
- Update verification
- Rollback support
- Progress indication

### Phase 4: OCPP Core
- WebSocket server on Ethernet
- OCPP message parsing and routing
- BootNotification handling
- Heartbeat handling
- StatusNotification handling
- Basic authorization (accept all)

### Phase 5: Transactions & Metering
- StartTransaction / StopTransaction
- MeterValues handling
- Transaction ID management
- Session state tracking

### Phase 6: MQTT Bridge
- MQTT client over WiFi
- Status publishing
- Command subscription
- Message format conversion
- Reconnection handling

### Phase 7: Phase Switching
- Relay control implementation
- Phase state machine
- MQTT phase commands
- Safety interlocks
- Feedback verification

### Phase 8: Smart Charging
- Charging profile support
- SetChargingProfile handling
- ClearChargingProfile handling
- Schedule management
- Integration with phase switching

### Phase 9: Advanced Features
- Remote commands (start/stop/reset)
- TLS for MQTT
- Auto-update checking
- Diagnostics and monitoring

## 7. Testing Requirements

### 7.1 Test Environment

#### 7.1.1 Test Simulator Architecture
A Python-based test simulator provides automated testing:

```
┌─────────────────────────────────────────────────────────────────────┐
│                     OCPP Test Simulator (Python)                     │
│                                                                      │
│  ┌─────────────────────┐              ┌─────────────────────┐       │
│  │  Wallbox Emulator   │              │    MQTT Client      │       │
│  │                     │              │                     │       │
│  │  - OCPP 1.6J Client │              │  - Publish commands │       │
│  │  - WebSocket conn   │              │  - Subscribe status │       │
│  │  - Simulates EV     │              │  - Phase control    │       │
│  │  - Meter values     │              │  - Power limits     │       │
│  └──────────┬──────────┘              └──────────┬──────────┘       │
│             │                                    │                   │
│  ┌──────────┴────────────────────────────────────┴──────────┐       │
│  │                    Test Scenarios                         │       │
│  │  - Automated test sequences                              │       │
│  │  - Pass/fail assertions                                  │       │
│  │  - Report generation                                     │       │
│  └──────────────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────────┘
              │ WebSocket                          │ MQTT
              │ (Ethernet)                         │ (WiFi)
              ▼                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      ESP32 OCPP Server (DUT)                         │
└─────────────────────────────────────────────────────────────────────┘
```

#### 7.1.2 Test Tools

| Tool | Purpose |
|------|---------|
| ocpp-test-simulator | Python test harness (wallbox + MQTT) |
| mosquitto_pub/sub | Manual MQTT testing |
| Wireshark | Network packet analysis |
| Browser DevTools | Captive portal testing |
| Multimeter | Phase relay verification |
| Logic Analyzer | GPIO timing verification |
| Serial Monitor | Debug log analysis |

### 7.2 Unit Tests

| Test ID | Component | Test Case | Expected Result |
|---------|-----------|-----------|-----------------|
| UT-001 | OCPP Parser | Parse valid BootNotification | Correct fields extracted |
| UT-002 | OCPP Parser | Parse malformed JSON | Error returned, no crash |
| UT-003 | OCPP Parser | Parse oversized message (>4KB) | Rejected with error |
| UT-004 | Config | Store WiFi credentials | Persisted in NVS |
| UT-005 | Config | Retrieve after reboot | Values match stored |
| UT-006 | Config | Factory reset | Defaults restored |
| UT-007 | Phase Logic | Calculate 1-phase power | Input/3 returned |
| UT-008 | Phase Logic | Calculate 3-phase power | Input unchanged |
| UT-009 | State Machine | Connector Available→Preparing | Valid transition |
| UT-010 | State Machine | Invalid transition | Rejected |
| UT-011 | JSON | Serialize MeterValues | Valid OCPP JSON |
| UT-012 | JSON | Handle special characters | Properly escaped |

### 7.3 Standard Test Cases

#### 7.3.1 TC-100: Basic Charging Cycle

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Power on ESP32 | Boot complete < 10s, LEDs indicate ready |
| 2 | Wallbox connects via WebSocket | Connection accepted, BootNotification exchanged |
| 3 | Wallbox sends StatusNotification (Available) | Status published to MQTT |
| 4 | EV plugs in (StatusNotification: Preparing) | Status change published |
| 5 | Wallbox sends Authorize request | AuthorizeConf with Accepted |
| 6 | Wallbox sends StartTransaction | StartTransactionConf with transactionId |
| 7 | Session info published to MQTT | Correct transaction details |
| 8 | Wallbox sends MeterValues periodically | Values published to MQTT |
| 9 | Wallbox sends StopTransaction | StopTransactionConf received |
| 10 | Session end published to MQTT | Final meter values included |

**Pass Criteria**: All steps complete without error, MQTT messages match expected format.

#### 7.3.2 TC-101: Remote Start via MQTT

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Wallbox connected, status Available | Status confirmed via MQTT |
| 2 | Publish start command to MQTT | Command received by ESP32 |
| 3 | ESP32 sends RemoteStartTransaction | Wallbox receives request |
| 4 | Wallbox responds with Accepted | StartTransaction follows |
| 5 | Charging begins | Status changes to Charging |

**Pass Criteria**: Charging starts within 10 seconds of MQTT command.

#### 7.3.3 TC-102: Remote Stop via MQTT

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging session active | Status is Charging |
| 2 | Publish stop command to MQTT | Command received by ESP32 |
| 3 | ESP32 sends RemoteStopTransaction | Wallbox receives request |
| 4 | Wallbox responds with Accepted | StopTransaction follows |
| 5 | Charging stops | Status changes to Finishing/Available |

**Pass Criteria**: Charging stops within 10 seconds of MQTT command.

#### 7.3.4 TC-103: Power Limit Command

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging at 11 kW | MeterValues show ~11 kW |
| 2 | Publish limit command: 5.5 kW | Command received |
| 3 | ESP32 sends SetChargingProfile | Profile with 5.5 kW limit |
| 4 | Wallbox applies limit | MeterValues show ~5.5 kW |
| 5 | Verify MQTT meter topic | Correct power reported |

**Pass Criteria**: Power reduced within 30 seconds, within 10% tolerance.

#### 7.3.5 TC-200: Phase Switch 3→1

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging at 7 kW (3-phase) | Phase status shows 3-phase |
| 2 | Publish limit command: 3.5 kW | Command received |
| 3 | ESP32 initiates phase switch | State: STOPPING |
| 4 | RemoteStopTransaction sent | Wallbox stops charging |
| 5 | Wait for Available status | Status confirmed |
| 6 | Safety delay (5s) | No relay activity |
| 7 | Relays switched (L2, L3 OFF) | GPIO 26, 27 = LOW |
| 8 | RemoteStartTransaction sent | Wallbox resumes |
| 9 | Charging resumes in 1-phase | MeterValues / 3 reported |
| 10 | Phase result published | Success, new mode: 1-phase |

**Pass Criteria**: Complete switch < 30s, no relay switching under load.

#### 7.3.6 TC-201: Phase Switch 1→3

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging at 3 kW (1-phase) | Phase status shows 1-phase |
| 2 | Publish limit command: 7.5 kW | Command received |
| 3 | ESP32 initiates phase switch | State: STOPPING |
| 4 | RemoteStopTransaction sent | Wallbox stops charging |
| 5 | Wait for Available status | Status confirmed |
| 6 | Safety delay (5s) | No relay activity |
| 7 | Relays switched (L2, L3 ON) | GPIO 26, 27 = HIGH |
| 8 | RemoteStartTransaction sent | Wallbox resumes |
| 9 | Charging resumes in 3-phase | MeterValues 1:1 reported |
| 10 | Phase result published | Success, new mode: 3-phase |

**Pass Criteria**: Complete switch < 30s, no relay switching under load.

#### 7.3.7 TC-300: Captive Portal Configuration

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Hold CONFIG button 5 seconds | AP mode activates, LED fast blink |
| 2 | Connect to AP (OCPP-ESP32-XXXX) | DHCP assigns IP |
| 3 | Open browser | Redirected to portal |
| 4 | Navigate to WiFi page | Network list displayed |
| 5 | Enter WiFi credentials | Form accepts input |
| 6 | Save configuration | Success message |
| 7 | Navigate to MQTT page | Form displayed |
| 8 | Enter MQTT settings | Form accepts input |
| 9 | Save and reboot | System restarts |
| 10 | Verify connections | WiFi + MQTT connected |

**Pass Criteria**: Configuration persists across reboot, connections established.

#### 7.3.8 TC-301: OTA Firmware Update

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Access portal /update page | Current version displayed |
| 2 | Select valid firmware.bin | File accepted |
| 3 | Click Upload | Progress bar shows % |
| 4 | Upload completes | Success message |
| 5 | System reboots | Automatic reboot |
| 6 | Verify new version | Version number updated |
| 7 | Verify functionality | All features working |

**Pass Criteria**: Update completes, system operational, version updated.

### 7.4 Edge Case Tests

#### 7.4.1 EC-100: WebSocket Disconnect During Charging

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging session active | Normal operation |
| 2 | Disconnect Ethernet cable | Connection lost |
| 3 | Wait 30 seconds | ESP32 detects disconnect |
| 4 | MQTT status published | Status: disconnected |
| 5 | Reconnect Ethernet | Link restored |
| 6 | Wallbox reconnects | BootNotification exchanged |
| 7 | Session state restored | Charging continues |

**Pass Criteria**: Automatic recovery, no data loss.

#### 7.4.2 EC-101: WiFi Disconnect During Charging

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging session active | MQTT publishing |
| 2 | Disable WiFi AP | WiFi connection lost |
| 3 | OCPP continues | Wallbox communication OK |
| 4 | Messages queued | Buffer fills |
| 5 | Re-enable WiFi AP | WiFi reconnects |
| 6 | Queued messages sent | MQTT catches up |

**Pass Criteria**: OCPP unaffected, MQTT recovers automatically.

#### 7.4.3 EC-102: Phase Switch Timeout

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Initiate phase switch | STOPPING state entered |
| 2 | Wallbox doesn't stop (simulate) | Timeout timer starts |
| 3 | Wait 30 seconds | Timeout expires |
| 4 | Switch aborted | State returns to IDLE |
| 5 | Error published to MQTT | Reason: timeout |
| 6 | Relays unchanged | Original phase mode |

**Pass Criteria**: Safe abort, no relay activity, error reported.

#### 7.4.4 EC-103: Phase Switch Relay Feedback Mismatch

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Initiate phase switch | Normal sequence to SWITCHING |
| 2 | Relays commanded | GPIO set |
| 3 | Feedback doesn't match | PHASE_SENSE incorrect |
| 4 | Mismatch detected | Error state entered |
| 5 | Transaction NOT restarted | Safety interlock |
| 6 | Error published to MQTT | Reason: feedback_mismatch |

**Pass Criteria**: No restart on failure, error clearly reported.

#### 7.4.5 EC-104: Rapid Power Limit Changes

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging at 11 kW | Stable operation |
| 2 | Send 10 limit commands in 10s | Commands received |
| 3 | Monitor system | No crash, no watchdog |
| 4 | Final limit applied | Last command wins |
| 5 | Check memory | No leaks |

**Pass Criteria**: System stable, correct final state.

#### 7.4.6 EC-105: Malformed MQTT Commands

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send invalid JSON | Message ignored |
| 2 | Send missing fields | Error logged |
| 3 | Send wrong types | Error logged |
| 4 | Send oversized payload | Rejected |
| 5 | System continues | No crash |

**Pass Criteria**: Graceful handling, no crashes.

#### 7.4.7 EC-106: Power Cycle During Phase Switch

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Initiate phase switch | Process started |
| 2 | Power cycle ESP32 | Immediate restart |
| 3 | System boots | Normal boot sequence |
| 4 | Check phase state | Relays in default state |
| 5 | Check transaction | No orphaned transaction |

**Pass Criteria**: Safe recovery, deterministic state.

#### 7.4.8 EC-107: OTA Update During Charging

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging session active | Transaction running |
| 2 | Start OTA update | Warning displayed |
| 3 | Confirm update | Transaction stopped first |
| 4 | Wait for Available | Status confirmed |
| 5 | Update proceeds | Normal OTA flow |

**Pass Criteria**: Transaction cleanly stopped before update.

#### 7.4.9 EC-108: Maximum Message Size

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send MeterValues with all fields | Large but valid |
| 2 | Parse and process | Successful |
| 3 | Send 4KB+ message | Exceeds limit |
| 4 | Message rejected | Error response |
| 5 | System stable | No memory issues |

**Pass Criteria**: Size limits enforced, no buffer overflow.

#### 7.4.10 EC-109: Concurrent MQTT and OCPP Activity

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Wallbox sending MeterValues | High OCPP traffic |
| 2 | Send multiple MQTT commands | Concurrent processing |
| 3 | Monitor both interfaces | All messages handled |
| 4 | Check timing | No excessive delays |
| 5 | Verify data integrity | No corruption |

**Pass Criteria**: Both interfaces functional under load.

### 7.5 Long-Duration Tests

| Test ID | Duration | Description | Pass Criteria |
|---------|----------|-------------|---------------|
| LD-001 | 24 hours | Continuous charging | No memory leaks, stable |
| LD-002 | 72 hours | Idle with heartbeats | No watchdog resets |
| LD-003 | 7 days | Normal usage pattern | < 1 unexpected reset |
| LD-004 | 24 hours | Repeated phase switches | All switches successful |

### 7.6 Test Report Template

```
═══════════════════════════════════════════════════════════════
                    OCPP ESP32 TEST REPORT
═══════════════════════════════════════════════════════════════
Date:           2026-01-25
Firmware:       v1.2.3
Tester:         ___________
Test Suite:     Standard / Edge Cases / Long Duration

───────────────────────────────────────────────────────────────
SUMMARY
───────────────────────────────────────────────────────────────
Total Tests:    ___
Passed:         ___
Failed:         ___
Skipped:        ___
Pass Rate:      ___%

───────────────────────────────────────────────────────────────
FAILED TESTS
───────────────────────────────────────────────────────────────
Test ID    | Description              | Failure Reason
-----------|--------------------------|---------------------------
           |                          |

───────────────────────────────────────────────────────────────
NOTES
───────────────────────────────────────────────────────────────


═══════════════════════════════════════════════════════════════
```

## 8. Project Structure

```
ocpp-esp32/
├── CMakeLists.txt              # Top-level ESP-IDF project file
├── partitions.csv              # Custom partition table (OTA)
├── sdkconfig.defaults          # Default Kconfig settings
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # app_main(), boot mode logic, watchdog
└── components/
    ├── board_pins/              # GPIO pin definitions (header-only)
    │   ├── CMakeLists.txt
    │   ├── include/board_pins.h
    │   └── board_pins.c
    ├── led_status/              # LED pattern manager (timer-driven)
    │   ├── CMakeLists.txt
    │   ├── include/led_status.h
    │   └── led_status.c
    ├── config_manager/          # NVS read/write, config struct
    │   ├── CMakeLists.txt
    │   ├── include/config_manager.h
    │   └── config_manager.c
    ├── gpio_control/            # Phase relays + config button
    │   ├── CMakeLists.txt
    │   ├── include/gpio_control.h
    │   └── gpio_control.c
    ├── ethernet_manager/        # W5500 SPI Ethernet init
    │   ├── CMakeLists.txt
    │   ├── include/ethernet_manager.h
    │   └── ethernet_manager.c
    ├── wifi_manager/            # WiFi STA/AP mode management
    │   ├── CMakeLists.txt
    │   ├── include/wifi_manager.h
    │   └── wifi_manager.c
    └── console_cmd/             # Serial CLI commands
        ├── CMakeLists.txt
        ├── include/console_cmd.h
        └── console_cmd.c
```

## 9. Dependencies

### 9.1 ESP-IDF Build Configuration

The project uses ESP-IDF v5.4 native CMake build system. All dependencies are
built-in ESP-IDF components — no external libraries required.

```cmake
# Top-level CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ocpp-esp32)
```

Key sdkconfig settings:
- Target: ESP32
- Flash: 8MB (OTA dual partition)
- Optimization: Size (`-Os`)
- Watchdog: 5s timeout with panic
- WiFi: SoftAP support enabled
- Ethernet: W5500 SPI enabled

### 9.2 Custom Partition Table (partitions.csv)

```csv
# Name,   Type, SubType, Offset,   Size,    Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xE000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x180000,
app1,     app,  ota_1,   0x190000, 0x180000,
spiffs,   data, spiffs,  0x310000, 0x180000,
coredump, data, coredump,0x490000, 0x10000,
```

### 9.3 ESP-IDF Component Dependencies

| Component | ESP-IDF Module | Purpose |
|-----------|---------------|---------|
| board_pins | soc | GPIO number definitions |
| led_status | esp_driver_gpio, esp_timer | LED output + 50ms tick timer |
| config_manager | nvs_flash, esp_wifi | NVS persistence, MAC address |
| gpio_control | esp_driver_gpio, esp_timer | Relay output + button polling |
| ethernet_manager | esp_eth, esp_driver_spi, esp_netif | W5500 SPI Ethernet |
| wifi_manager | esp_wifi, esp_netif, lwip | WiFi STA/AP modes |
| console_cmd | console, esp_system | Serial REPL commands |

## 10. Appendices

### Appendix A: OCPP 1.6 Message Format

```
[MessageTypeId, UniqueId, Action, Payload]

Call:       [2, "19223201", "BootNotification", {...}]
CallResult: [3, "19223201", {...}]
CallError:  [4, "19223201", "GenericError", "Error description", {...}]
```

### Appendix B: Connector States

```
Available → Preparing → Charging → SuspendedEV/SuspendedEVSE → Finishing → Available
                ↓                              ↓
             Faulted ←─────────────────────────┘
```

### Appendix C: Reference Documents

- OCPP 1.6 JSON Specification
- OCPP 1.6 Implementation Guide
- ESP32 Technical Reference Manual
- W5500 Datasheet

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-25 | - | Initial specification |
| 1.1 | 2026-01-25 | - | Added dual-network architecture (ETH for wallbox, WiFi for MQTT) |
| | | | Added captive portal for configuration |
| | | | Added OTA firmware update support |
| | | | Added phase switching control (1/3 phase) |
| | | | Added power value correction for phase modes |
| | | | Added safety-critical phase switching sequence |
| 1.2 | 2026-01-30 | - | Changed framework from Arduino/PlatformIO to ESP-IDF v5.4 native C |
| | | | Updated project structure to ESP-IDF component architecture |
| | | | Updated dependency table to ESP-IDF built-in components |
| | | | Added serial console CLI (esp_console) to framework table |
| | | | Specified custom OCPP implementation (not MicroOcpp) |
