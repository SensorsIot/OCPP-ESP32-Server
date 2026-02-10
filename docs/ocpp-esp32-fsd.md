# ESP32 OCPP Server - Functional Specification Document

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.5 |
| Status | Draft |
| Created | 2026-01-25 |
| Updated | 2026-02-09 |

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
7. Support single wallbox connection

## 2. Hardware Requirements

### 2.1 Target Platform

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-WROOM-32 or ESP32-S3 |
| Ethernet | W5500 SPI Ethernet module (for wallbox) |
| WiFi | Built-in ESP32 WiFi (for MQTT) |
| Flash | 4MB |
| PSRAM | none |
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

#### WT32-ETH01 Module incl W5500
- ESP32-WROOM-32
- W5500 Ethernet module 

### 2.4 Pin Assignments (WT32-ETH01)

#### Ethernet RMII (LAN8720 — hardwired, NOT available for GPIO)

| GPIO | RMII Function |
|------|--------------|
| 0 | REF_CLK (50 MHz clock input; also strapping pin) |
| 16 | OSC_EN (PHY oscillator enable, active high) |
| 18 | MDIO (management data) |
| 19 | TXD0 |
| 21 | TX_EN |
| 22 | TXD1 |
| 23 | MDC (management clock) |
| 25 | RXD0 |
| 26 | RXD1 |
| 27 | CRS_DV (carrier sense) |

#### Application Pin Assignments

| Function | GPIO | Notes |
|----------|------|-------|
| BTN_CONFIG | 14 | Config button (hold 5s for captive portal; not a strapping pin) |
| RELAY_PHASE23 | 4 | L2+L3 enable relay (NO; L1 always connected) |

#### All GPIOs on WT32-ETH01 Header

| GPIO | Available | Notes |
|------|-----------|-------|
| 0 | No | Ethernet RMII CLK + strapping (boot mode) |
| 1 | No | UART0 TX (serial console) |
| 2 | Strapping | Must float/LOW at boot |
| 3 | No | UART0 RX (serial console) |
| **4** | **Relay** | Phase switching relay output |
| 5 | Free | UART2 RX (repurposable) |
| 12 | Strapping | Flash voltage select (LOW=3.3V, HIGH=1.8V) |
| **14** | **Config btn** | Captive portal trigger |
| 15 | Strapping | Debug log silenced if LOW at boot |
| 17 | Free | UART2 TX (repurposable) |
| 32 | Free | Labeled "CFG" on some board variants |
| 33 | Free | Labeled "485_EN" on some board variants |
| 35 | Free | Input-only, no internal pull |
| 36 | Free | Input-only, no internal pull |
| 39 | Free | Input-only, no internal pull |

#### Test Wiring (Pi Serial Portal ↔ DUT)

| Pi BCM | DUT Pin | Direction | Function |
|--------|---------|-----------|----------|
| 17 | EN | Pi → DUT | Reset (active LOW) |
| 18 | GPIO 0 | Pi → DUT | Boot mode select (LOW = download) |
| 27 | GPIO 14 | Pi → DUT | Config button (LOW = pressed) |
| 22 | GPIO 4 | DUT → Pi | Relay state readback (LOW = 1-phase, HIGH = 3-phase) |

### 2.5 Flash Partition Layout (OTA Support)

```
┌─────────────────────────────────────────────────────────┐
│  Partition Table (4MB Flash)                            │
├─────────────────────────────────────────────────────────┤
│  nvs        │  0x9000  │  20KB   │ Configuration       │
│  otadata    │  0xE000  │   8KB   │ OTA state           │
│  app0       │ 0x10000  │ 1.75MB  │ Application (slot 1)│
│  app1       │ 0x1D0000 │ 1.75MB  │ Application (slot 2)│
│  spiffs     │ 0x390000 │ 384KB   │ Web UI files        │
│  coredump   │ 0x3F0000 │  64KB   │ Crash dumps         │
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
│  ┌────────────┐ ┌────────────┐ ┌────────────┐                             │
│  │  Config    │ │   NTP      │ │   Logger   │                             │
│  │  Manager   │ │   Sync     │ │            │                             │
│  └────────────┘ └────────────┘ └────────────┘                             │
├───────────────────────────────────────────────────────────────────────────┤
│                        Hardware Abstraction                                │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐     │
│  │  W5500 SPI  │  │  WiFi PHY   │  │ Phase Relay   │  │  NVS/LittleFS│    │
│  └─────────────┘  └─────────────┘  └──────────────┘  └─────────────┘     │
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
           ┌───────│ MQTT Host Configured?│───────┐
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

| Mode | WiFi | Ethernet | OCPP | MQTT via | Portal | OTA |
|------|------|----------|------|----------|--------|-----|
| Config | AP | Inactive | No | N/A | Yes | Yes |
| Normal | Off | Active | ETH only | Ethernet | No | Yes |
| Test | STA | Active | ETH + WiFi | WiFi | No | Yes |
| Fallback | AP+STA | Active | ETH only | Ethernet | Yes | Yes |

## 4. Functional Requirements

### 4.1 Network Management

#### 4.1.1 Ethernet Connection (Wallbox Network)
- **ETH-001**: System SHALL initialize W5500 Ethernet on boot
- **ETH-002**: System SHALL use static IP for Ethernet (default: 192.168.4.1)
- **ETH-003**: System SHALL monitor link status and log changes
- **ETH-004**: System SHALL support configurable static IP settings
- **ETH-005**: Ethernet network SHALL be isolated from WiFi network in Normal mode
- **ETH-006**: OCPP WebSocket server SHALL bind to Ethernet interface in Normal mode
- **ETH-007**: In Test mode, OCPP WebSocket server SHALL also bind to WiFi interface

#### 4.1.2 WiFi Station Mode (MQTT/Internet Network)
- **WIFI-001**: System SHALL connect to configured WiFi network in STA mode
- **WIFI-002**: WiFi credentials SHALL be stored encrypted in NVS
- **WIFI-003**: System SHALL automatically reconnect on WiFi disconnect
- **WIFI-004**: System SHALL log WiFi connection status changes
- **WIFI-005**: System SHALL support WPA2/WPA3 authentication
- **WIFI-006**: In test mode, MQTT client SHALL use WiFi interface; in production, MQTT SHALL use Ethernet
- **WIFI-007**: System SHALL sync time via NTP over WiFi

#### 4.1.3 WiFi Access Point Mode (Configuration)
- **AP-001**: System SHALL start AP mode when no valid WiFi config exists
- **AP-002**: System SHALL start AP mode when BTN_CONFIG held for 5 seconds
- **AP-003**: AP SHALL use SSID format: `OCPP-ESP32-{MAC_LAST_4}`
- **AP-004**: AP SHALL use open authentication (no password) for easy initial setup
- **AP-005**: AP SHALL assign IP 192.168.1.1 to clients
- **AP-006**: System MAY run AP and STA concurrently (fallback mode)

#### 4.1.4 Test Mode
- **TEST-001**: System SHALL support a "Test mode" configurable via captive portal or NVS
- **TEST-002**: In Test mode, WebSocket server SHALL listen on ALL interfaces (ETH + WiFi)
- **TEST-003**: In Test mode, wallbox emulator MAY connect via WiFi instead of Ethernet
- **TEST-004**: Test mode SHALL be indicated via serial log and MQTT status
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
- **MQTT-006**: System SHALL log MQTT connection status changes

#### 4.6.2 Topic Structure (Simplified)

Base topic: `ocpp/{chargepoint_id}/` (configurable prefix)

| Topic | Direction | QoS | Description |
|-------|-----------|-----|-------------|
| `status` | Publish | 1 | Wallbox connection + connector status |
| `session` | Publish | 1 | Active transaction with meter values (consolidated) |
| `phase` | Publish | 1 | Current phase mode (for monitoring) |
| `command/start` | Subscribe | 1 | Start charging transaction |
| `command/stop` | Subscribe | 1 | Stop charging transaction |
| `command/limit` | Subscribe | 1 | Set power limit (triggers automatic phase switching) |

**Removed topics (simplified):**
- ~~`meter`~~ → Merged into `session`
- ~~`availability`~~ → Use `command/stop` + don't send `command/start`
- ~~`command/availability`~~ → Use start/stop instead
- ~~`command/phase`~~ → Automatic based on power limit threshold
- ~~`command/config/*`~~ → Use serial console or captive portal
- ~~`command/reset`~~ → Use serial console or captive portal

#### 4.6.3 Message Formats

**Status Message (Published)**
Published on wallbox connect/disconnect and connector status changes.
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "connected": true,
  "status": "Charging",
  "error_code": "NoError"
}
```

**Session Message (Published)**
Published periodically during active transaction (includes meter values).
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "transaction_id": 12345,
  "id_tag": "RFID123456",
  "energy_wh": 1500,
  "power_w": 7400,
  "current_a": 32.0,
  "duration_s": 1800,
  "phase_mode": "3-phase"
}
```

**Phase Message (Published)**
Published on phase mode changes.
```json
{
  "timestamp": "2026-01-25T10:30:00Z",
  "phase_mode": "3-phase",
  "power_correction_factor": 1.0
}
```

**Start Command (Subscribed)**
```json
{
  "id_tag": "ENERGY_MANAGER"
}
```

**Stop Command (Subscribed)**
```json
{}
```

**Limit Command (Subscribed)**
Sets power limit. Phase switching is automatic based on threshold (< 4.1 kW = 1-phase, >= 4.1 kW = 3-phase).
```json
{
  "power_w": 7400
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
| `ap_pass` | string | "" | AP password (empty = open network) |
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
- **OTA-014**: System SHALL indicate update progress via web UI and serial log
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
│   L1 ●─────────────────────────────────────────────● L1     │
│                (always connected, no relay)                  │
│                                                              │
│   L2 ●──────┐                                               │
│              ├──[RELAY_PHASE23]─────────────────────● L2     │
│   L3 ●──────┘       (NO)          ┌───────────────● L3     │
│                                    │                         │
│                    (single relay   │                         │
│                     controls both  │                         │
│                     L2 and L3)  ───┘                         │
│                                                              │
│   N  ●─────────────────────────────────────────────● N      │
│                                                              │
│   PE ●─────────────────────────────────────────────● PE     │
│                                                              │
│   NO = Normally Open (L2+L3 connected when energized)       │
│   L1 = Always connected (not controlled by OCPP server)     │
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

- **PHASE-001**: System SHALL control phase switching via single GPIO output
- **PHASE-002**: Phase 1 (L1) SHALL be always connected (no relay, not tracked by OCPP server)
- **PHASE-003**: Phases 2 and 3 (L2+L3) SHALL be controlled by a single normally-open relay
- **PHASE-004**: System SHALL NEVER switch relays while charging is active
- **PHASE-005**: System SHALL stop transaction before phase switching
- **PHASE-006**: System SHALL verify wallbox status is "Available" before switching
- **PHASE-007**: System SHALL wait for configurable delay after stop before switching
- **PHASE-008**: System SHALL start new transaction after successful switch
- **PHASE-009**: System SHALL verify phase state via wallbox MeterValues (L2/L3 voltage = 0 V confirms 1-phase; non-zero confirms 3-phase)
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
│  5. SWITCH RELAY                                                        │
│     └─► Set GPIO: according to target phase mode                        │
│     └─► 1-phase: RELAY_PHASE23=OFF (L2+L3 disconnected)                │
│     └─► 3-phase: RELAY_PHASE23=ON  (L2+L3 connected)                   │
│                                                                          │
│  6. VERIFY SWITCH                                                       │
│     └─► Wait: for wallbox StatusNotification after relay change         │
│     └─► Read: MeterValues from wallbox — check L2/L3 voltage           │
│     └─► 1-phase: L2 and L3 voltage must be 0 V                         │
│     └─► 3-phase: L2 and L3 voltage must be non-zero                    │
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
│    ┌──────────────┐       Voltage Mismatch                    │       │
│    │  SWITCHING   │─────────────────────────────────►  ERROR ──┤       │
│    │  (GPIO+WB)   │                                            │       │
│    └──────┬───────┘                                            │       │
│           │                                                    │       │
│     WB Voltage OK                                              │       │
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
  "power_correction_factor": 1.0
}
```

**Note:** Phase switching is automatic based on `command/limit` power threshold (see 4.10.8).
No manual `command/phase` topic - phase mode is determined by requested power level.

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
- **SAFETY-005**: System SHALL verify phase switch via wallbox MeterValues (L2/L3 voltage must match expected state)
- **SAFETY-006**: On ANY error, system SHALL remain in current phase configuration
- **SAFETY-007**: System SHALL report all switching errors to MQTT
- **SAFETY-008**: If switch fails, system SHALL NOT restart transaction automatically

### 4.11 Status Indication

No physical LEDs are used. Status is communicated via:
- **Serial console**: ESP_LOG messages for all state changes
- **MQTT**: Status topics for remote monitoring
- **Captive portal**: `/api/system/status` endpoint with heap, uptime, connection state

### 4.12 Logging and Diagnostics

#### 4.12.1 Logging
- **LOG-001**: System SHALL log to serial console
- **LOG-002**: System SHALL support log levels (error, warn, info, debug)
- **LOG-003**: System SHALL include timestamps in logs
- **LOG-004**: System MAY publish logs to MQTT (debug topic)

#### 4.12.2 OCPP Message Logging (Critical)
- **LOG-010**: System SHALL log all OCPP messages received from wallbox
- **LOG-011**: System SHALL log all OCPP messages sent to wallbox
- **LOG-012**: Log format SHALL include direction (RX/TX), message type, action, and payload
- **LOG-013**: At INFO level, log action name and key fields only
- **LOG-014**: At DEBUG level, log full JSON payload

**Log Format Example:**
```
I (12345) OCPP: RX [2,"abc123","StatusNotification",{"connectorId":1,"status":"Charging"}]
I (12346) OCPP: TX [3,"abc123",{"status":"Accepted"}]
```

**Abbreviated Format (INFO level):**
```
I (12345) OCPP: RX StatusNotification: connector=1 status=Charging
I (12346) OCPP: TX StatusNotification.conf: Accepted
```

#### 4.12.3 Diagnostics
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
- Wallbox voltage verification (L2/L3 via MeterValues)

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
A Python-based test simulator provides automated testing. The Pi (Serial Portal +
WiFi Tester) provides GPIO control, serial access, and WiFi AP for the DUT.

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Test Host (Python)                                │
│                                                                      │
│  ┌─────────────────────┐              ┌─────────────────────┐       │
│  │  Wallbox Emulator   │              │    MQTT Client      │       │
│  │                     │              │                     │       │
│  │  - OCPP 1.6J Client │              │  - Publish commands │       │
│  │  - WebSocket conn   │              │  - Subscribe status │       │
│  │  - Phase-aware      │              │  - Phase control    │       │
│  │    MeterValues      │              │  - Power limits     │       │
│  │  - POST /api/phase  │              │                     │       │
│  │    to sync mode     │              │                     │       │
│  └──────────┬──────────┘              └──────────┬──────────┘       │
│             │                                    │                   │
│  ┌──────────┴────────────────────────────────────┴──────────┐       │
│  │                    Test Scenarios                         │       │
│  │  - Automated test sequences                              │       │
│  │  - Pass/fail assertions                                  │       │
│  │  - WiFiTesterDriver for GPIO + serial                    │       │
│  │  - Relay readback (BCM 22 ← GPIO 4)                     │       │
│  └──────────────────────────────────────────────────────────┘       │
└──────────┬──────────────────┬────────────────────┬──────────────────┘
           │ WebSocket        │ MQTT               │ WiFiTesterDriver
           │ (Ethernet)       │ (WiFi)             │ (HTTP API)
           ▼                  ▼                    ▼
┌──────────────────────────────────┐    ┌─────────────────────────────┐
│      ESP32 OCPP Server (DUT)     │    │   Pi (Serial Portal +       │
│                                  │    │        WiFi Tester)          │
│  GPIO 4 (relay) ──────wire──────────► │   BCM 22 (relay readback)   │
│  GPIO 14 (config btn) ◄──wire──────── │   BCM 27 (config trigger)   │
│  EN (reset) ◄──────────wire──────────│   BCM 17 (reset)            │
│  GPIO 0 (boot mode) ◄──wire─────────│   BCM 18 (boot mode)        │
│  Serial (UART) ◄────RFC2217─────────│   /dev/ttyUSB0              │
│                                  │    │   MQTT broker (1883)        │
│                                  │    │   WiFi AP (192.168.4.1)     │
└──────────────────────────────────┘    └─────────────────────────────┘
```

**Phase test coordination:** During phase switching tests, the test script polls
Pi BCM 22 to detect DUT relay state changes, then calls the wallbox emulator's
`POST /api/phase` endpoint to switch its MeterValues generation (L2/L3 current=0
and voltage=0 in 1-phase mode). For TC-EC-103 (voltage mismatch), the test
intentionally does NOT sync the emulator, simulating a stuck relay.

#### 7.1.2 Test Tools

| Tool | Purpose |
|------|---------|
| ocpp-test-wallbox | Wallbox emulator (OCPP client) + MQTT client + Web UI |
| WiFiTesterDriver | Python driver for Pi Serial Portal / WiFi Tester (GPIO, serial, WiFi AP) |
| pytest | Test framework (pytest-asyncio for async OCPP/WebSocket tests) |
| esptool | Flash/erase ESP32 via RFC2217 serial |
| mosquitto_pub/sub | Manual MQTT testing |
| Wireshark | Network packet analysis |
| Serial Monitor | Debug log analysis (via WiFiTesterDriver or pyserial RFC2217) |

### 7.2 Unit Test Areas

| Component | What to Test |
|-----------|-------------|
| OCPP Parser | Valid messages parsed correctly, malformed JSON rejected, oversized messages (>4KB) rejected |
| Config Manager | Store/retrieve WiFi and MQTT credentials across reboot, factory reset restores defaults |
| Phase Logic | 1-phase power = input/3, 3-phase power = input unchanged |
| State Machine | Valid connector transitions accepted, invalid transitions rejected |
| JSON Serializer | MeterValues serialize to valid OCPP JSON, special characters escaped |

### 7.3 Test Categories

Detailed test procedures, step tables, and automation commands are in the
[Test Specification](OCPP-ESP32-Test-Specification.md).
This section lists what must be tested and the high-level pass criteria.

#### Standard Tests

| Category | Key Pass Criteria |
|----------|-------------------|
| Setup | DUT flashed, NVS erased, boots to correct mode |
| Captive Portal | Portal entry (GPIO 14 or NVS-empty), WiFi/MQTT provisioning, DNS redirect |
| MQTT Transport | Production mode (Ethernet only), test mode (WiFi), mode switching, fallback |
| Connection | WebSocket connect, BootNotification, Heartbeat, timeout detection, reconnect |
| Charging | Full charge cycle, authorization, MeterValues forwarding, transaction IDs |
| Remote Commands | MQTT start/stop/limit commands translate to correct OCPP actions |
| Phase Switching | 3→1 and 1→3 switch < 30s, no switch under load, voltage verification, power correction |
| OTA Update | Upload succeeds, corrupt firmware rejected, rollback on boot failure |

#### Edge Case Tests

| Category | Key Pass Criteria |
|----------|-------------------|
| Disconnect/Reconnect | Automatic recovery after WebSocket, WiFi, or MQTT broker disconnect |
| Phase Switch Errors | Safe abort on timeout, voltage mismatch detection and reporting |
| Input Validation | Malformed OCPP/MQTT messages handled gracefully, no crash |
| Concurrent Load | Both OCPP and MQTT interfaces functional under sustained parallel traffic |
| Power Loss | Safe recovery to known state after power cycle during phase switch |
| WiFi Resilience | Graceful degradation, automatic reconnection, DHCP renewal |
| Watchdog | Software and hardware WDT trigger recovery, no false triggers during WiFi disconnect |

#### Long-Duration Tests

| Duration | Description | Pass Criteria |
|----------|-------------|---------------|
| 24 hours | Continuous charging | No memory leaks, stable heap |
| 72 hours | Idle with heartbeats | No watchdog resets |
| 7 days | Normal usage pattern | < 1 unexpected reset |
| 3 hours | 100 repeated phase switches | 100% success rate |

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
- Flash: 4MB (OTA dual partition)
- Optimization: Size (`-Os`)
- Watchdog: 5s timeout with panic
- WiFi: SoftAP support enabled
- Ethernet: W5500 SPI enabled

### 9.2 Custom Partition Table (partitions.csv)

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xE000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1C0000,
app1,     app,  ota_1,   0x1D0000, 0x1C0000,
spiffs,   data, spiffs,  0x390000, 0x60000,
coredump, data, coredump,0x3F0000, 0x10000,
```

### 9.3 ESP-IDF Component Dependencies

| Component | ESP-IDF Module | Purpose |
|-----------|---------------|---------|
| board_pins | soc | GPIO number definitions |
| led_status | esp_common | Status stub (no physical LEDs) |
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
| 1.3 | 2026-01-30 | - | Removed all LED hardware (no physical LEDs); status via log/MQTT only |
| | | | Simplified phase switching: single relay for L2+L3, L1 always connected |
| | | | Reduced pin count: removed 4 LED GPIOs and 2 relay GPIOs |
| 1.4 | 2026-02-09 | - | Removed PHASE_SENSE (GPIO 34) — relay has no feedback contact |
| | | | Phase verification now uses wallbox MeterValues (L2/L3 voltage) |
| | | | Updated PHASE-009, SAFETY-005, switching sequence, state machine, EC-103 |
| | | | Config button moved from GPIO 0 (strapping pin) to GPIO 14 |
| | | | Boot decision changed from wifi_ssid to mqtt_host |
| | | | MQTT transport: Ethernet (production) or WiFi (test mode) |
| | | | WIFI-006 updated for dual transport modes |
| 1.5 | 2026-02-09 | - | Relay moved from GPIO 25 (Ethernet RXD0 on WT32-ETH01) to GPIO 4 |
| | | | Replaced incorrect W5500 SPI pin table with WT32-ETH01 LAN8720 RMII pins |
| | | | Added complete GPIO availability table for WT32-ETH01 header |
| | | | Added Pi ↔ DUT test wiring table (BCM 22 reads relay state) |
| 1.6 | 2026-02-09 | - | Slimmed section 7: removed detailed test step tables, reference test spec |
| | | | Updated test architecture diagram (Pi GPIO, relay readback, emulator sync) |
| | | | Added WiFiTesterDriver and pytest to test tools |
