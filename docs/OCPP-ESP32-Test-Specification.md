# ESP32 OCPP Server Test Specification

| Field | Value |
|-------|-------|
| **Version** | 1.0 |
| **Date** | 2026-02-09 |
| **Author** | Claude |
| **Status** | Draft |
| **SUT** | ESP32 OCPP Server (WT32-ETH01) |
| **FSD Reference** | [docs/ocpp-esp32-fsd.md](ocpp-esp32-fsd.md) |

---

## 1. Overview

### 1.1 System Context

The ESP32 OCPP Server bridges EV charging stations (wallboxes) to an MQTT-based energy management system using a dual-network architecture.

```
┌─────────────┐    Ethernet     ┌──────────────┐      WiFi       ┌──────────────┐
│   Wallbox   │◄───────────────│  ESP32 OCPP  │◄───────────────►│ MQTT Broker  │
│  (Charger)  │   OCPP/WS       │    Server    │                 │              │
└─────────────┘   192.168.4.x   └──────┬───────┘                 └──────────────┘
                                       │                                 │
                                  WiFi AP                                ▼
                               (config mode)                     ┌──────────────┐
                               ┌──────────────┐                  │   Energy     │
                               │Captive Portal│                  │   Manager    │
                               └──────────────┘                  └──────────────┘
```

### 1.2 Key Features Under Test

1. **OCPP 1.6J Central System** — WebSocket server handling wallbox communication
2. **Smart Charging** — Dynamic power limits via charging profiles
3. **Phase Switching** — 1-phase ↔ 3-phase via GPIO relay with safety interlocks
4. **MQTT Bridge** — Status publishing and command subscription
5. **Captive Portal** — WiFi/MQTT credential provisioning
6. **OTA Updates** — Firmware upload with rollback support
7. **Watchdog** — Hardware and software watchdog recovery

### 1.3 Test Suite at a Glance

| Category | ID Range | Count | Automated | Manual |
|----------|----------|------:|----------:|-------:|
| Setup | TC-000 – TC-001 | 2 | 2 | 0 |
| Connection | TC-100 – TC-104 | 5 | 5 | 0 |
| Charging | TC-110 – TC-113 | 4 | 4 | 0 |
| Remote Commands | TC-120 – TC-123 | 4 | 4 | 0 |
| Phase Switching | TC-130 – TC-134 | 5 | 3 | 2 |
| Captive Portal | CP-100 – CP-103 | 4 | 2 | 2 |
| OTA Update | OTA-100 – OTA-103 | 4 | 3 | 1 |
| Edge Cases | EC-100 – EC-115 | 16 | 12 | 4 |
| Long Duration | LD-001 – LD-004 | 4 | 4 | 0 |
| **Total** | | **48** | **39** | **9** |

### 1.4 How to Run

**Quick smoke test (5 min):**
```bash
cd ocpp-test-wallbox
pytest tests/ -m "not slow and not integration" -x -v
```

**Full automated suite:**
```bash
cd ocpp-test-wallbox
pytest tests/ -v --tb=short
```

**Manual tests:**
Follow test cases marked "Manual" in the classification table (Section 10).

---

## 2. Test Environment

### 2.1 Infrastructure

| Component | Address / Location | Role |
|-----------|--------------------|------|
| ESP32 DUT | 192.168.4.1 (Ethernet) | System under test |
| MQTT Broker | 192.168.1.50:1883 | Message broker for commands/status |
| Serial Portal | 192.168.0.87:8080 | RFC2217 serial access to DUT |
| WiFi Tester | 192.168.0.87 | WiFi AP control for testing |
| Test Host | Any | Runs pytest, mosquitto_pub/sub |

### 2.2 Infrastructure Rules

- **MQTT Broker**: always running. Only TC-000 and EC-106 may restart it.
- **Serial Portal**: always running. No test may restart it.
- **WiFi Tester AP**: always running. Only WIFI-* tests may modify AP settings.
- **DUT**: may be reset by any test. Must be restored to clean state after flash/erase operations.

### 2.3 Hardware Setup

| Component | Description | Connection |
|-----------|-------------|------------|
| DUT | WT32-ETH01 (ESP32 + LAN8720) | RFC2217 via Serial Portal SLOT1 |
| Phase Relay | Single relay for L2+L3 | GPIO 25 (output) |
| Config Button | Portal trigger | GPIO 14 (not a strapping pin) |

### 2.4 Partition Layout

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| nvs | 0x9000 | 20KB | Configuration (WiFi, MQTT credentials) |
| otadata | 0xE000 | 8KB | OTA state |
| app0 | 0x10000 | 1.75MB | Application (slot 1) |
| app1 | 0x1D0000 | 1.75MB | Application (slot 2) |
| spiffs | 0x390000 | 384KB | Web UI files |

### 2.5 DUT Initial State

Before running any test, the DUT must be in this state:

1. Flash firmware: `idf.py -p 'rfc2217://192.168.0.87:4001' flash`
2. Erase NVS: `esptool.py --port 'rfc2217://192.168.0.87:4001' erase_region 0x9000 0x5000`
3. Verify boot: serial output contains `boot:0x` and no crash backtrace
4. Verify clean config: no WiFi credentials configured (AP mode starts)

**What "clean" means:**
- Factory default configuration
- No persisted WiFi/MQTT credentials
- Boot count at initial value
- No active OCPP sessions

### 2.6 Test Tools

| Tool | Version | Purpose | Install |
|------|---------|---------|---------|
| pytest | 8.x | Test framework | `pip install pytest pytest-asyncio` |
| ocpp-test-wallbox | local | Wallbox emulator + MQTT client | `cd ocpp-test-wallbox && pip install -e .` |
| mosquitto-clients | 2.x | MQTT pub/sub | `apt install mosquitto-clients` |
| esptool | 4.x | Flash/erase ESP32 | `pip install esptool` |
| idf.py | v5.4 | Build/flash ESP-IDF | `source /opt/esp-idf/export.sh` |

### 2.7 MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `ocpp/{id}/status` | Publish | Wallbox connection + connector status |
| `ocpp/{id}/session` | Publish | Active transaction with meter values |
| `ocpp/{id}/phase` | Publish | Current phase mode |
| `ocpp/{id}/command/start` | Subscribe | Start charging transaction |
| `ocpp/{id}/command/stop` | Subscribe | Stop charging transaction |
| `ocpp/{id}/command/limit` | Subscribe | Set power limit (W) |

---

## 3. Setup Test Cases

These tests establish a clean, known starting state for all subsequent tests.

#### TC-000: Flash and Provision DUT

**Precondition:**
- Firmware built: `ls ocpp-esp32/build/ocpp-esp32.bin` exits 0
- Serial Portal accessible: `curl -s http://192.168.0.87:8080/api/devices` returns JSON

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Flash firmware via RFC2217 | `idf.py flash` exits 0 |
| 2 | Erase NVS partition | `esptool.py erase_region 0x9000 0x5000` exits 0 |
| 3 | Reset DUT | Serial output shows boot sequence |
| 4 | Verify version in serial log | Version matches build |
| 5 | Verify AP mode started | Serial shows `WiFi AP started: OCPP-ESP32-XXXX` |

**Pass Criteria:** DUT is flashed, NVS erased, boots into AP mode with correct version.

**Automation:** `pytest tests/test_setup.py::test_flash_provision -v`

---

#### TC-001: Verify Clean State

**Precondition:**
- TC-000 passed
- DUT running: serial output shows normal boot

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Check serial console `config` command | All values at defaults |
| 2 | Check heap memory | Free heap > 100KB |
| 3 | Verify no WiFi STA connection | WiFi status shows "disconnected" |
| 4 | Verify no MQTT connection | MQTT status shows "disconnected" |
| 5 | Verify AP mode active | AP SSID visible in WiFi scan |

**Pass Criteria:** DUT is in a known clean state with all defaults applied and no residual configuration.

**Automation:** `pytest tests/test_setup.py::test_verify_clean -v`

---

## 4. Standard Test Cases

Core functionality tests validating the primary features of the SUT.

### 4.1 Connection Tests

Validates OCPP WebSocket connection handling between wallbox and server.

#### TC-100: Wallbox WebSocket Connection

**Precondition:**
- DUT running in normal mode: Ethernet interface up
- DUT IP reachable: `ping 192.168.4.1` succeeds
- No existing WebSocket connections

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Connect wallbox emulator via WebSocket to `ws://192.168.4.1:9000/ocpp/TEST001` | Connection accepted |
| 2 | Send BootNotification | Response received within 5s |
| 3 | Check response status | `status: Accepted` |
| 4 | Verify heartbeat interval in response | `interval` is 60 (default) |
| 5 | Verify MQTT status published | Topic `ocpp/TEST001/status` shows `connected: true` |

**Pass Criteria:** Wallbox connects, registers via BootNotification, and connection status is published to MQTT.

**Automation:** `pytest tests/test_connection.py::test_websocket_connect -v`

---

#### TC-101: Heartbeat Exchange

**Precondition:**
- TC-100 passed: wallbox connected
- Baseline heartbeat count: record from event log as `H_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send Heartbeat request | Response received |
| 2 | Check response | Contains `currentTime` field |
| 3 | Wait 60 seconds | Heartbeat sent automatically |
| 4 | Verify heartbeat count | Count is `H_before + 1` |

**Pass Criteria:** Heartbeat exchange works, automatic heartbeats sent at configured interval.

**Automation:** `pytest tests/test_connection.py::test_heartbeat -v`

---

#### TC-102: StatusNotification Handling

**Precondition:**
- Wallbox connected: TC-100 passed
- Baseline MQTT messages: record count as `M_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send StatusNotification (Available) | Response received |
| 2 | Check response | Empty object `{}` (acknowledged) |
| 3 | Verify MQTT status published | `status: Available` in `ocpp/TEST001/status` |
| 4 | Send StatusNotification (Preparing) | Response received |
| 5 | Verify MQTT status updated | `status: Preparing` in MQTT |

**Pass Criteria:** StatusNotification messages are acknowledged and forwarded to MQTT.

**Automation:** `pytest tests/test_connection.py::test_status_notification -v`

---

#### TC-103: Connection Timeout Detection

**Precondition:**
- Wallbox connected: TC-100 passed
- DUT heartbeat timeout configured: 30 seconds

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Stop sending heartbeats from wallbox | Wallbox goes silent |
| 2 | Wait 35 seconds | DUT detects timeout |
| 3 | Verify disconnection logged | Serial shows "WebSocket timeout" |
| 4 | Verify MQTT status published | `connected: false` in status topic |

**Pass Criteria:** DUT detects wallbox timeout and publishes disconnection status.

**Automation:** `pytest tests/test_connection.py::test_connection_timeout -v`

---

#### TC-104: Reconnection After Disconnect

**Precondition:**
- Wallbox was connected then disconnected: TC-103 scenario
- MQTT still connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Reconnect wallbox emulator | Connection accepted |
| 2 | Send BootNotification | Response received |
| 3 | Verify status in response | `status: Accepted` |
| 4 | Verify MQTT status published | `connected: true` |
| 5 | Send StatusNotification (Available) | Normal operation resumed |

**Pass Criteria:** Wallbox can reconnect after disconnection, full functionality restored.

**Automation:** `pytest tests/test_connection.py::test_reconnection -v`

---

### 4.2 Charging Tests

Validates charging transaction lifecycle.

#### TC-110: Basic Charging Cycle

**Precondition:**
- Wallbox connected, status Available
- No active transaction
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send StatusNotification (Preparing) | EV plugged in |
| 2 | Send Authorize request (idTag: "TEST123") | `status: Accepted` |
| 3 | Send StartTransaction | Response with `transactionId` |
| 4 | Send StatusNotification (Charging) | Charging started |
| 5 | Verify MQTT session published | Transaction details in `session` topic |
| 6 | Send MeterValues (1000 Wh, 3500 W) | Values acknowledged |
| 7 | Verify MQTT meter values | Power and energy in `session` topic |
| 8 | Send StopTransaction | Transaction stopped |
| 9 | Send StatusNotification (Finishing) | Session ending |
| 10 | Send StatusNotification (Available) | Ready for next session |

**Pass Criteria:** Complete charging cycle from plug-in to completion with meter values published.

**Automation:** `pytest tests/test_charging.py::test_basic_cycle -v`

---

#### TC-111: Authorization Accept All Mode

**Precondition:**
- DUT configured with `auth_mode: accept_all`
- Wallbox connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send Authorize (idTag: "UNKNOWN_TAG") | `status: Accepted` |
| 2 | Send Authorize (idTag: "") | `status: Accepted` |
| 3 | Send StartTransaction with any idTag | Transaction starts |

**Pass Criteria:** All authorization requests accepted in accept_all mode.

**Automation:** `pytest tests/test_charging.py::test_auth_accept_all -v`

---

#### TC-112: MeterValues Publishing

**Precondition:**
- Active charging transaction
- MQTT connected
- Baseline meter reading: record as `E_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send MeterValues (energy: 500 Wh, power: 7400 W) | Acknowledged |
| 2 | Wait 1 second | MQTT publishes |
| 3 | Verify MQTT session topic | `energy_wh: 500`, `power_w: 7400` |
| 4 | Send MeterValues (energy: 1000 Wh, power: 7400 W) | Acknowledged |
| 5 | Verify MQTT updated | `energy_wh: 1000` |

**Pass Criteria:** MeterValues are forwarded to MQTT session topic with correct values.

**Automation:** `pytest tests/test_metering.py::test_meter_publishing -v`

---

#### TC-113: Transaction ID Management

**Precondition:**
- No active transaction
- Wallbox connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start transaction | Get `transactionId: N` |
| 2 | Stop transaction | Transaction N stopped |
| 3 | Start new transaction | Get `transactionId: N+1` |
| 4 | Verify IDs are unique | No ID reuse |

**Pass Criteria:** Transaction IDs are assigned sequentially and uniquely.

**Automation:** `pytest tests/test_charging.py::test_transaction_ids -v`

---

### 4.3 Remote Command Tests

Validates MQTT-initiated commands to the wallbox.

#### TC-120: Remote Start via MQTT

**Precondition:**
- Wallbox connected, status Available
- MQTT connected
- No active transaction

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"id_tag": "ENERGY_MANAGER"}` to `ocpp/TEST001/command/start` | Command received |
| 2 | Wait for RemoteStartTransaction on WebSocket | Wallbox receives request |
| 3 | Wallbox responds Accepted | Response sent |
| 4 | Wallbox sends StartTransaction | Transaction begins |
| 5 | Verify MQTT session published | Session active |

**Pass Criteria:** MQTT start command triggers RemoteStartTransaction to wallbox.

**Automation:** `pytest tests/test_remote.py::test_remote_start -v`

---

#### TC-121: Remote Stop via MQTT

**Precondition:**
- Active charging transaction
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{}` to `ocpp/TEST001/command/stop` | Command received |
| 2 | Wait for RemoteStopTransaction on WebSocket | Wallbox receives request |
| 3 | Wallbox responds Accepted | Response sent |
| 4 | Wallbox sends StopTransaction | Transaction ends |
| 5 | Verify MQTT session updated | Session stopped |

**Pass Criteria:** MQTT stop command triggers RemoteStopTransaction to wallbox.

**Automation:** `pytest tests/test_remote.py::test_remote_stop -v`

---

#### TC-122: Power Limit via MQTT

**Precondition:**
- Active charging transaction
- Charging at 11 kW (3-phase)
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"power_w": 5500}` to `ocpp/TEST001/command/limit` | Command received |
| 2 | Wait for SetChargingProfile on WebSocket | Profile sent to wallbox |
| 3 | Verify profile limit | Limit set to ~24A (5500W / 230V) |
| 4 | Wallbox applies profile | MeterValues show reduced power |
| 5 | Verify within 10% tolerance | Power between 4950W and 6050W |

**Pass Criteria:** MQTT power limit command translates to SetChargingProfile.

**Automation:** `pytest tests/test_power_profiles.py::test_mqtt_power_limit -v`

---

#### TC-123: Power Limit Zero (Stop Charging)

**Precondition:**
- Active charging transaction
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"power_w": 0}` to `ocpp/TEST001/command/limit` | Command received |
| 2 | Wait for SetChargingProfile | Profile with 0A limit |
| 3 | Wallbox stops drawing power | MeterValues show 0W |
| 4 | Transaction remains active | Status still "Charging" or "SuspendedEVSE" |

**Pass Criteria:** Zero power limit suspends charging without stopping transaction.

**Automation:** `pytest tests/test_power_profiles.py::test_zero_power_limit -v`

---

### 4.4 Phase Switching Tests

Validates 1-phase ↔ 3-phase switching with safety interlocks.

#### TC-130: Phase Switch 3→1 (Automatic)

**Precondition:**
- Charging at 7 kW (3-phase mode)
- Phase status: `phase_mode: 3-phase`
- MQTT connected
- Relay GPIO 25 is HIGH

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"power_w": 3500}` to `command/limit` | Below 4.1 kW threshold |
| 2 | DUT initiates phase switch | State: STOPPING |
| 3 | RemoteStopTransaction sent | Wallbox stops |
| 4 | Wait for status Available | Safe to switch |
| 5 | Wait 5 second safety delay | No relay activity yet |
| 6 | Relay switches | GPIO 25 → LOW (L2+L3 disconnected) |
| 7 | RemoteStartTransaction sent | Charging resumes |
| 8 | Verify MeterValues: L2/L3 voltage = 0 V | Wallbox confirms phases disconnected |
| 9 | Verify MeterValues corrected | Reported power divided by 3 |
| 10 | Verify MQTT phase topic | `phase_mode: 1-phase` |

**Pass Criteria:** Complete 3→1 phase switch under 30s, relay only switches when not charging, wallbox confirms L2/L3 voltage = 0 V.

**Automation:** `pytest tests/test_phase.py::test_switch_3_to_1 -v`

---

#### TC-131: Phase Switch 1→3 (Automatic)

**Precondition:**
- Charging at 3 kW (1-phase mode)
- Phase status: `phase_mode: 1-phase`
- Relay GPIO 25 is LOW

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"power_w": 7500}` to `command/limit` | Above 4.1 kW threshold |
| 2 | DUT initiates phase switch | State: STOPPING |
| 3 | RemoteStopTransaction sent | Wallbox stops |
| 4 | Wait for status Available | Safe to switch |
| 5 | Wait 5 second safety delay | No relay activity yet |
| 6 | Relay switches | GPIO 25 → HIGH (L2+L3 connected) |
| 7 | RemoteStartTransaction sent | Charging resumes |
| 8 | Verify MeterValues: L2/L3 voltage > 0 V | Wallbox confirms phases connected |
| 9 | Verify MeterValues | Reported power 1:1 (no correction) |
| 10 | Verify MQTT phase topic | `phase_mode: 3-phase` |

**Pass Criteria:** Complete 1→3 phase switch under 30s, relay only switches when not charging.

**Automation:** `pytest tests/test_phase.py::test_switch_1_to_3 -v`

---

#### TC-132: Phase Switch Safety - No Switch Under Load

**Precondition:**
- Charging actively (status: Charging)
- MeterValues showing > 0 W

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Record relay GPIO state | Initial state |
| 2 | Publish phase switch command | Switch initiated |
| 3 | Verify RemoteStopTransaction sent first | Stop before switch |
| 4 | Verify relay unchanged while power > 0 | Safety interlock |
| 5 | Only after Available status | Relay changes |

**Pass Criteria:** Relay NEVER switches while power is flowing.

**Automation:** `pytest tests/test_phase.py::test_no_switch_under_load -v`

---

#### TC-133: Phase Switch Voltage Verification Failure

**Precondition:**
- Wallbox connected, reports per-phase voltage in MeterValues
- Charging stopped (Available status)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Initiate 3→1 phase switch | Switch sequence starts |
| 2 | Relay commands sent | GPIO 25 → LOW |
| 3 | Transaction restarted | Wallbox resumes charging |
| 4 | Wallbox MeterValues arrive | L2/L3 voltage still > 50 V (relay stuck) |
| 5 | Voltage mismatch detected | Error logged |
| 6 | MQTT error published | `error: voltage_mismatch` |

**Pass Criteria:** Wallbox voltage mismatch is detected and reported via MQTT.

**Automation:** `pytest tests/test_phase.py::test_voltage_mismatch -v` (requires wallbox emulator to report fake per-phase voltage)

---

#### TC-134: Power Correction in 1-Phase Mode

**Precondition:**
- Operating in 1-phase mode
- Wallbox reports 11 kW (as if 3-phase)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Wallbox sends MeterValues: 11000 W | Raw value |
| 2 | DUT applies correction | 11000 / 3 = 3667 W |
| 3 | Check MQTT session topic | `power_w: 3667` (approx) |
| 4 | Check MeterValues to wallbox | Corrected value in profile |

**Pass Criteria:** In 1-phase mode, all power values divided by 3 before publishing.

**Automation:** `pytest tests/test_phase.py::test_power_correction -v`

---

## 5. Captive Portal Tests

Validates WiFi provisioning and configuration via captive portal.

#### CP-100: Enter Captive Portal Mode

**Precondition:**
- DUT running in normal mode with WiFi configured
- GPIO 14 (config button) accessible

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Hold CONFIG button (GPIO 14) for 5 seconds | Button held |
| 2 | Verify serial log | `Entering config mode` logged |
| 3 | Verify AP starts | SSID `OCPP-ESP32-XXXX` visible |
| 4 | Connect phone/laptop to AP | DHCP assigns 192.168.1.x |
| 5 | Open browser to any URL | Redirected to portal |
| 6 | Verify portal page loads | Configuration UI displayed |

**Pass Criteria:** Long button press triggers captive portal mode.

**Automation:** Manual (requires physical button press) or GPIO automation via Serial Portal

---

#### CP-101: WiFi Credential Provisioning

**Precondition:**
- DUT in captive portal mode (AP active)
- Client connected to DUT AP
- Test WiFi network available: `TestNetwork` / `testpass123`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to `/wifi` in portal | WiFi page loads |
| 2 | Click "Scan" | Network list populates |
| 3 | Verify test network visible | `TestNetwork` in list |
| 4 | Select network, enter password | Form accepts input |
| 5 | Click "Save & Connect" | Success message |
| 6 | Wait for DUT to reboot | Automatic restart |
| 7 | Verify WiFi connection | DUT connected to TestNetwork |
| 8 | Verify IP assigned | DUT has IP on test network |

**Pass Criteria:** WiFi credentials saved, DUT connects to configured network after reboot.

**Automation:** `pytest tests/test_captive_portal.py::test_wifi_provision -v` (partial)

---

#### CP-102: MQTT Configuration

**Precondition:**
- DUT in captive portal mode
- Client connected to DUT AP
- MQTT broker available at 192.168.1.50:1883

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to `/mqtt` in portal | MQTT page loads |
| 2 | Enter broker: `192.168.1.50` | Form accepts |
| 3 | Enter port: `1883` | Form accepts |
| 4 | Leave username/password empty | Optional fields |
| 5 | Enter topic prefix: `ocpp` | Form accepts |
| 6 | Click "Save" | Success message |
| 7 | Reboot DUT | Apply settings |
| 8 | Verify MQTT connected | Status topic published |

**Pass Criteria:** MQTT settings saved and applied, connection established.

**Automation:** `pytest tests/test_captive_portal.py::test_mqtt_config -v` (partial)

---

#### CP-103: DNS Redirect (Captive Portal Detection)

**Precondition:**
- DUT in AP mode
- Client connected to DUT AP (192.168.1.x)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Query any domain via DNS | `nslookup google.com` |
| 2 | Verify response | Returns 192.168.1.1 (portal IP) |
| 3 | Open HTTP request to random domain | Browser request |
| 4 | Verify redirect | Redirected to captive portal |
| 5 | Check iOS/Android captive portal detection | Auto-popup on connect |

**Pass Criteria:** All DNS queries redirected to portal, captive portal detection works.

**Automation:** `pytest tests/test_captive_portal.py::test_dns_redirect -v`

---

## 6. OTA Update Tests

Validates over-the-air firmware update functionality.

#### OTA-100: Firmware Upload via Portal

**Precondition:**
- DUT accessible via WiFi or Ethernet
- New firmware built: `ocpp-esp32.bin` exists
- Current version recorded: `V_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to `/update` in browser | Update page loads |
| 2 | Verify current version displayed | Matches `V_before` |
| 3 | Select new firmware.bin file | File accepted |
| 4 | Click "Upload" | Progress bar shows % |
| 5 | Wait for upload complete | Success message |
| 6 | DUT reboots automatically | Reboot within 5s |
| 7 | Verify new version | Version incremented |
| 8 | Verify configuration preserved | WiFi/MQTT still configured |
| 9 | Verify functionality | All features working |

**Pass Criteria:** OTA completes, version updated, config preserved, DUT operational.

**Automation:** `pytest tests/test_ota.py::test_firmware_upload -v`

---

#### OTA-101: OTA with Corrupt Firmware

**Precondition:**
- DUT accessible
- Corrupt firmware file: truncated or random bytes
- Current version: `V_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Upload corrupt firmware file | Upload starts |
| 2 | Firmware validation fails | Error message: "Invalid firmware" |
| 3 | DUT remains on current version | Version unchanged |
| 4 | DUT continues normal operation | No crash or brick |

**Pass Criteria:** DUT rejects corrupt firmware and continues operating.

**Automation:** `pytest tests/test_ota.py::test_corrupt_firmware -v`

---

#### OTA-102: OTA Rollback on Boot Failure

**Precondition:**
- DUT with OTA partition layout
- "Bad" firmware that crashes on boot (test firmware)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Upload bad firmware | Upload succeeds |
| 2 | DUT reboots to new partition | Boot attempt |
| 3 | New firmware crashes | Watchdog triggers |
| 4 | Bootloader rolls back | Previous partition selected |
| 5 | Verify old firmware running | Version is `V_before` |
| 6 | Verify DUT operational | Normal operation |

**Pass Criteria:** Bootloader rollback protects against bad firmware.

**Automation:** Manual (requires special test firmware that intentionally crashes)

---

#### OTA-103: OTA During Active Transaction

**Precondition:**
- Active charging transaction
- OTA firmware ready

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start OTA upload | Upload begins |
| 2 | Verify warning displayed | "Active transaction will be stopped" |
| 3 | Confirm update | Transaction stopped first |
| 4 | Wait for Available status | Charging stopped gracefully |
| 5 | OTA proceeds | Upload and apply |
| 6 | After reboot | DUT operational, no orphan transaction |

**Pass Criteria:** Active transaction stopped cleanly before OTA.

**Automation:** `pytest tests/test_ota.py::test_ota_during_transaction -v`

---

## 7. Edge Case Tests

Tests for error handling, boundary conditions, and recovery from unexpected inputs.

#### EC-100: WebSocket Disconnect During Charging

**Precondition:**
- Active charging transaction
- Wallbox connected via WebSocket
- MQTT publishing meter values

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disconnect wallbox (close WebSocket) | Connection lost |
| 2 | Verify MQTT status | `connected: false` published |
| 3 | Wait 10 seconds | DUT in disconnected state |
| 4 | Reconnect wallbox | WebSocket re-established |
| 5 | Wallbox sends BootNotification | Re-registration |
| 6 | Session state inquiry | Transaction can be resumed or properly closed |

**Pass Criteria:** Automatic recovery, MQTT reflects connection state.

**Automation:** `pytest tests/test_errors.py::test_websocket_disconnect_charging -v`

---

#### EC-101: WiFi Disconnect During Charging

**Precondition:**
- Active charging transaction
- MQTT connected and publishing
- OCPP via Ethernet operating

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disable WiFi AP | WiFi connection lost |
| 2 | OCPP continues operating | Wallbox communication OK |
| 3 | Verify serial log | "WiFi disconnected, MQTT queuing" |
| 4 | Wait 60 seconds | Messages accumulate |
| 5 | Re-enable WiFi AP | WiFi reconnects |
| 6 | MQTT reconnects | Queued messages sent |

**Pass Criteria:** OCPP unaffected by WiFi loss, MQTT recovers automatically.

**Automation:** `pytest tests/test_errors.py::test_wifi_disconnect_charging -v`

---

#### EC-102: Phase Switch Timeout

**Precondition:**
- Active charging transaction
- Phase switch initiated
- Wallbox configured to not respond to RemoteStop

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Initiate phase switch via MQTT | STOPPING state entered |
| 2 | RemoteStopTransaction sent | Wallbox doesn't stop |
| 3 | Wait 30 seconds | Timeout timer expires |
| 4 | Switch aborted | State returns to IDLE |
| 5 | MQTT error published | `error: timeout` |
| 6 | Relays unchanged | Original phase mode maintained |
| 7 | Transaction continues | Charging not interrupted |

**Pass Criteria:** Safe abort on timeout, no relay activity, error reported.

**Automation:** `pytest tests/test_phase.py::test_switch_timeout -v`

---

#### EC-103: Rapid Power Limit Changes

**Precondition:**
- Active charging transaction
- Charging at 11 kW
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send 10 power limit commands in 10 seconds | Rapid commands |
| 2 | Monitor DUT heap | No memory leaks |
| 3 | Monitor watchdog | No resets |
| 4 | Verify final limit applied | Last command value |
| 5 | System remains stable | Normal operation |

**Pass Criteria:** DUT handles rapid commands without crash or memory leak.

**Automation:** `pytest tests/test_dynamic_power.py::test_rapid_limit_changes -v`

---

#### EC-104: Malformed OCPP Message

**Precondition:**
- Wallbox connected
- Baseline error count: record as `E_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send invalid JSON | `CallError: FormationViolation` |
| 2 | Send valid JSON, missing required fields | `CallError: PropertyConstraintViolation` |
| 3 | Send unknown action | `CallError: NotImplemented` |
| 4 | Verify DUT still operational | Heartbeat works |
| 5 | Check error count | `E_before + 3` |

**Pass Criteria:** Graceful error handling, no crash, proper error responses.

**Automation:** `pytest tests/test_errors.py::test_malformed_ocpp -v`

---

#### EC-105: Malformed MQTT Command

**Precondition:**
- DUT subscribed to command topics
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish invalid JSON to `command/start` | Message ignored |
| 2 | Publish `{"power_w": "invalid"}` to `command/limit` | Error logged |
| 3 | Publish empty message to `command/stop` | Treated as valid (empty = stop all) |
| 4 | Verify DUT operational | No crash |

**Pass Criteria:** Invalid MQTT commands don't crash DUT.

**Automation:** `pytest tests/test_errors.py::test_malformed_mqtt -v`

---

#### EC-106: MQTT Broker Restart

**Precondition:**
- DUT connected to MQTT
- Active message publishing
- Baseline reconnect count: `R_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Stop MQTT broker | Connection lost |
| 2 | Verify DUT detects disconnect | Serial log: "MQTT disconnected" |
| 3 | Wait 10 seconds | DUT retrying |
| 4 | Restart MQTT broker | Broker available |
| 5 | Verify DUT reconnects | Within 30 seconds |
| 6 | Verify publishing resumes | Status messages flowing |
| 7 | Check reconnect count | `R_before + 1` |

**Pass Criteria:** Automatic MQTT reconnection after broker restart.

**Automation:** `pytest tests/test_errors.py::test_mqtt_broker_restart -v`

---

#### EC-107: Maximum Message Size

**Precondition:**
- Wallbox connected
- Message size limit: 4KB

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Send MeterValues with all fields (large but valid) | Accepted |
| 2 | Send 4KB message | At limit, processed |
| 3 | Send 5KB message | Rejected, error response |
| 4 | Verify DUT stable | No memory issues |

**Pass Criteria:** Size limits enforced, no buffer overflow.

**Automation:** `pytest tests/test_errors.py::test_max_message_size -v`

---

#### EC-108: Concurrent MQTT and OCPP Traffic

**Precondition:**
- Wallbox connected, sending MeterValues every 1s
- MQTT commands arriving every 1s

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Generate high OCPP traffic | 10 msg/sec |
| 2 | Generate high MQTT traffic | 10 cmd/sec |
| 3 | Run for 60 seconds | Sustained load |
| 4 | Verify no dropped messages | All processed |
| 5 | Verify timing | No >1s delays |

**Pass Criteria:** Both interfaces functional under concurrent load.

**Automation:** `pytest tests/test_errors.py::test_concurrent_traffic -v`

---

#### EC-109: Power Cycle During Phase Switch

**Precondition:**
- Phase switch in progress (SWITCHING state)
- Power supply controllable

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Initiate phase switch | Process started |
| 2 | Power cycle DUT mid-switch | Immediate restart |
| 3 | DUT boots | Normal boot sequence |
| 4 | Check relay state | Default state (safe) |
| 5 | Check transaction state | No orphaned transaction |
| 6 | Verify operation | Normal functionality |

**Pass Criteria:** Safe recovery to known state after power loss.

**Automation:** Manual (requires power control)

---

#### EC-110: WiFi Signal Degradation

**Precondition:**
- DUT connected to WiFi
- MQTT publishing
- RSSI measurable

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Normal operation | RSSI > -60 dBm |
| 2 | Increase distance/attenuation | RSSI decreases |
| 3 | At -70 dBm | Connection maintained |
| 4 | At -80 dBm | Possible packet loss, retries |
| 5 | At -85 dBm | Disconnection likely |
| 6 | Return to normal range | Automatic reconnection |

**Pass Criteria:** Graceful degradation, automatic recovery.

**Automation:** Manual (requires RF environment control)

---

#### EC-111: DHCP Lease Expiry

**Precondition:**
- DUT connected to WiFi with short DHCP lease (60s)
- MQTT connected
- IP address recorded: `IP_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Wait 60+ seconds | Lease expires |
| 2 | DHCP renewal attempt | Automatic |
| 3 | Verify IP | Same or new IP assigned |
| 4 | Verify MQTT | Reconnects if IP changed |
| 5 | Normal operation continues | No user intervention |

**Pass Criteria:** Automatic DHCP renewal without service disruption.

**Automation:** `pytest tests/test_errors.py::test_dhcp_renewal -v`

---

#### EC-112: Software Watchdog Recovery

**Precondition:**
- DUT running normally
- Watchdog timeout: 60 seconds
- Test firmware with task hang capability

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Monitor serial: health checks every 5s | Normal operation |
| 2 | Trigger task hang (test firmware) | Main task stops responding |
| 3 | Wait 65 seconds | Watchdog timeout |
| 4 | Verify reboot | Serial shows boot sequence |
| 5 | Verify recovery | WiFi + MQTT reconnect |

**Pass Criteria:** Watchdog detects hang and forces recovery reboot.

**Automation:** Manual (requires special test firmware)

---

#### EC-113: Hardware Watchdog Fallback

**Precondition:**
- DUT with hardware watchdog enabled
- Watchdog timeout: 5 seconds

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Verify serial: "Hardware WDT initialized" | WDT active |
| 2 | Normal operation | WDT fed regularly |
| 3 | If software watchdog fails | Hardware WDT triggers |
| 4 | System panic and reboot | Failsafe recovery |

**Pass Criteria:** Hardware watchdog provides last-resort recovery.

**Automation:** Manual (code review verification)

---

#### EC-114: Watchdog During WiFi Disconnect

**Precondition:**
- DUT running with active watchdog
- WiFi connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disable WiFi AP | Connection lost |
| 2 | Wait 5 minutes | Extended disconnect |
| 3 | Monitor serial | Health checks continue |
| 4 | No watchdog resets | System stable |
| 5 | Re-enable WiFi | Connection restored |

**Pass Criteria:** Watchdog doesn't false-trigger during WiFi reconnection attempts.

**Automation:** `pytest tests/test_errors.py::test_watchdog_wifi_disconnect -v`

---

#### EC-115: Memory Pressure Watchdog

**Precondition:**
- DUT running normally
- Baseline heap: `H_before` > 100KB
- Critical threshold: 20KB

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Monitor heap via serial | Stable at `H_before` |
| 2 | Simulate memory pressure | Heap decreases |
| 3 | Warning threshold crossed | "Low heap" logged |
| 4 | Critical threshold crossed | "Critical heap - reboot" |
| 5 | DUT reboots | Preventive recovery |
| 6 | After reboot | Heap recovered, WiFi reconnects |

**Pass Criteria:** Memory exhaustion triggers preventive reboot before crash.

**Automation:** Manual (requires special test firmware)

---

### Edge Case Checklist

| Category | Covered By | Description |
|----------|-----------|-------------|
| Malformed input | EC-104, EC-105 | Invalid JSON, wrong types |
| Oversized input | EC-107 | Exceeds 4KB message limit |
| Empty input | EC-105 | Empty MQTT commands |
| Concurrent operations | EC-108 | Parallel OCPP + MQTT |
| Disconnect/reconnect | EC-100, EC-101, EC-106 | Network drops |
| Rapid-fire | EC-103 | Burst of commands |
| Timeout | EC-102 | Phase switch timeout |
| Resource exhaustion | EC-115 | Memory pressure |
| Power loss | EC-109 | Power cycle mid-operation |

---

## 8. Long Duration / Stress Tests

Stability and endurance tests run over extended periods.

#### LD-001: 24-Hour Continuous Charging

**Precondition:**
- DUT running in normal mode
- Wallbox emulator connected
- MQTT connected
- Baseline metrics: record heap, uptime

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start charging transaction | Session begins |
| 2 | Run for 24 hours | Continuous operation |
| 3 | MeterValues sent every 30s | 2880 messages |
| 4 | Check heap at end | Within 10% of baseline |
| 5 | Check for error logs | No error-level entries |
| 6 | Verify MQTT publishing | All messages delivered |
| 7 | Stop transaction | Clean stop |

**Pass Criteria:** 24 hours of continuous charging with no memory leak or errors.

**Duration:** 24h

**Automation:** `pytest tests/test_long_duration.py::test_24h_charging -v --timeout=90000`

---

#### LD-002: 72-Hour Idle with Heartbeats

**Precondition:**
- DUT running, no active transaction
- Wallbox connected (heartbeats only)
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start monitoring | Baseline state |
| 2 | Run for 72 hours | Idle operation |
| 3 | Heartbeats every 60s | 4320 heartbeats |
| 4 | Check watchdog resets | Zero resets |
| 5 | Check uptime | 72+ hours |
| 6 | Verify heap stable | No degradation |

**Pass Criteria:** 72 hours idle with no watchdog resets.

**Duration:** 72h

**Automation:** `pytest tests/test_long_duration.py::test_72h_idle -v --timeout=270000`

---

#### LD-003: 7-Day Normal Usage Pattern

**Precondition:**
- DUT in production-like environment
- Wallbox emulator with realistic patterns

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Simulate daily charging pattern | 2-3 sessions/day |
| 2 | Run for 7 days | 14-21 transactions |
| 3 | Include WiFi disconnects | 1-2 per day |
| 4 | Check unexpected resets | < 1 reset |
| 5 | Check all transactions complete | No orphans |

**Pass Criteria:** < 1 unexpected reset over 7 days of normal usage.

**Duration:** 7d

**Automation:** `pytest tests/test_long_duration.py::test_7day_usage -v --timeout=700000`

---

#### LD-004: Repeated Phase Switches

**Precondition:**
- DUT in normal mode with phase switching enabled
- Wallbox connected
- 100 switch cycles planned

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start charging in 3-phase | Initial state |
| 2 | Switch 3→1→3 (one cycle) | Complete switch |
| 3 | Repeat 100 times | 200 individual switches |
| 4 | Verify all switches successful | 100% success rate |
| 5 | Verify relay wear | No mechanical issues |
| 6 | Check timing consistency | Each switch < 30s |

**Pass Criteria:** 100 phase switch cycles with 100% success rate.

**Duration:** 3h

**Automation:** `pytest tests/test_long_duration.py::test_repeated_phase_switch -v --timeout=15000`

---

## 9. Test Commands Reference

### Setup Commands

```bash
# Build firmware
cd ocpp-esp32
source /opt/esp-idf/export.sh
idf.py build

# Flash via RFC2217
idf.py -p 'rfc2217://192.168.0.87:4001' flash

# Erase NVS (factory reset)
esptool.py --port 'rfc2217://192.168.0.87:4001' erase_region 0x9000 0x5000

# Monitor serial output
idf.py -p 'rfc2217://192.168.0.87:4001' monitor
```

### Test Execution

```bash
# Run all automated tests
cd ocpp-test-wallbox
pytest tests/ -v

# Run specific category
pytest tests/ -m connection -v
pytest tests/ -m charging -v
pytest tests/ -m phase -v

# Run single test
pytest tests/test_charging.py::test_basic_cycle -v

# Run with coverage
pytest tests/ --cov=src --cov-report=html
```

### MQTT Testing

```bash
# Subscribe to all OCPP topics
mosquitto_sub -h 192.168.1.50 -t "ocpp/#" -v

# Publish start command
mosquitto_pub -h 192.168.1.50 -t "ocpp/TEST001/command/start" -m '{"id_tag":"TEST"}'

# Publish stop command
mosquitto_pub -h 192.168.1.50 -t "ocpp/TEST001/command/stop" -m '{}'

# Publish power limit
mosquitto_pub -h 192.168.1.50 -t "ocpp/TEST001/command/limit" -m '{"power_w":5500}'
```

### Monitoring

```bash
# Check DUT heap via serial console
# In idf.py monitor, type: heap

# Check DUT config
# In idf.py monitor, type: config

# Check DUT status
# In idf.py monitor, type: status
```

---

## 10. Test Classification & Execution Sequence

### Execution Phases

| Phase | Category | Tests | Requires Human | Requires DUT | Duration |
|-------|----------|------:|:--------------:|:------------:|----------|
| 1 | Setup | 2 | No | Yes | 5 min |
| 2 | Connection | 5 | No | Yes | 10 min |
| 3 | Charging | 4 | No | Yes | 15 min |
| 4 | Remote Commands | 4 | No | Yes | 10 min |
| 5 | Phase Switching | 5 | 2 tests | Yes | 20 min |
| 6 | Captive Portal | 4 | 2 tests | Yes | 15 min |
| 7 | OTA Update | 4 | 1 test | Yes | 15 min |
| 8 | Edge Cases | 16 | 4 tests | Yes | 45 min |
| 9 | Long Duration | 4 | No | Yes | 7+ days |

### Manual-Only Tests

These tests require human interaction and cannot be fully automated:

| Test ID | Reason |
|---------|--------|
| TC-133 | Requires physical wire disconnection |
| CP-100 | Requires physical button press (unless GPIO wired) |
| EC-109 | Requires power cycle |
| EC-110 | Requires RF environment control |
| EC-112 | Requires special test firmware |
| EC-113 | Code review verification |
| EC-115 | Requires special test firmware |
| OTA-102 | Requires intentionally crashing firmware |

### GPIO Wiring for Automation

Wire Serial Portal Pi GPIOs to DUT pins:

| Pi GPIO (BCM) | DUT Pin | Function | Active Level | DUT Pull | Notes |
|---------------|---------|----------|-------------|----------|-------|
| 17 | EN/RESET | Reset chip | LOW | External pullup | Boot sequencing |
| 18 | GPIO 0 | Boot mode select | LOW | PULLUP | Download mode (strapping pin) |
| 27 | GPIO 14 | Config button | LOW | PULLUP | Captive portal trigger |

**Flash firmware (no DTR/CTS):**
```python
wt.gpio_set(18, 0)      # Hold GPIO 0 LOW (download mode)
wt.gpio_set(17, 0)      # Reset LOW
time.sleep(0.1)
wt.gpio_set(17, "z")    # Release reset
wt.gpio_set(18, "z")    # Release GPIO 0
# Flash via esptool with --before=no_reset
```

**Trigger captive portal (no reset needed):**
```python
wt.gpio_set(27, 0)      # Hold GPIO 14 LOW
time.sleep(5.5)          # Wait > 5 seconds
wt.gpio_set(27, "z")    # Release
# DUT enters AP mode without rebooting
```

---

## 11. Automated Test Coverage

### Test Files

| Test File | Tests | Source Under Test | Framework |
|-----------|------:|-------------------|-----------|
| `tests/test_connection.py` | 5 | OCPP WebSocket handling | pytest-asyncio |
| `tests/test_charging.py` | 4 | Transaction management | pytest-asyncio |
| `tests/test_remote.py` | 4 | MQTT command handling | pytest-asyncio |
| `tests/test_phase.py` | 5 | Phase switching logic | pytest-asyncio |
| `tests/test_power_profiles.py` | 3 | SetChargingProfile | pytest-asyncio |
| `tests/test_metering.py` | 2 | MeterValues processing | pytest-asyncio |
| `tests/test_errors.py` | 12 | Error handling | pytest-asyncio |
| `tests/test_ota.py` | 3 | OTA update | pytest-asyncio |
| `tests/test_captive_portal.py` | 2 | Captive portal | pytest-asyncio |
| **Total** | **40** | | |

### Coverage Gaps

These areas are tested manually only (no automated tests yet):

- **Physical button interactions**: Requires GPIO wiring to automate
- **Power cycle recovery**: Requires controllable power supply
- **RF signal degradation**: Requires RF environment control
- **Watchdog with task hang**: Requires special test firmware

---

## 12. Test Report Template

```
================================================================
         ESP32 OCPP Server — Test Execution Report
================================================================
Date     : ____________
Tester   : ____________
FW / Ver : ____________
Commit   : ____________
Environment: ____________
================================================================

SETUP
  TC-000  Flash and Provision DUT    [ PASS / FAIL / SKIP ]
  TC-001  Verify Clean State         [ PASS / FAIL / SKIP ]

CONNECTION
  TC-100  WebSocket Connection       [ PASS / FAIL / SKIP ]
  TC-101  Heartbeat Exchange         [ PASS / FAIL / SKIP ]
  TC-102  StatusNotification         [ PASS / FAIL / SKIP ]
  TC-103  Connection Timeout         [ PASS / FAIL / SKIP ]
  TC-104  Reconnection               [ PASS / FAIL / SKIP ]

CHARGING
  TC-110  Basic Charging Cycle       [ PASS / FAIL / SKIP ]
  TC-111  Authorization Accept All   [ PASS / FAIL / SKIP ]
  TC-112  MeterValues Publishing     [ PASS / FAIL / SKIP ]
  TC-113  Transaction ID Management  [ PASS / FAIL / SKIP ]

REMOTE COMMANDS
  TC-120  Remote Start via MQTT      [ PASS / FAIL / SKIP ]
  TC-121  Remote Stop via MQTT       [ PASS / FAIL / SKIP ]
  TC-122  Power Limit via MQTT       [ PASS / FAIL / SKIP ]
  TC-123  Power Limit Zero           [ PASS / FAIL / SKIP ]

PHASE SWITCHING
  TC-130  Phase Switch 3→1           [ PASS / FAIL / SKIP ]
  TC-131  Phase Switch 1→3           [ PASS / FAIL / SKIP ]
  TC-132  No Switch Under Load       [ PASS / FAIL / SKIP ]
  TC-133  Voltage Verification Failure [ PASS / FAIL / SKIP ]
  TC-134  Power Correction           [ PASS / FAIL / SKIP ]

CAPTIVE PORTAL
  CP-100  Enter Portal Mode (Manual) [ PASS / FAIL / SKIP ]
  CP-101  WiFi Provisioning          [ PASS / FAIL / SKIP ]
  CP-102  MQTT Configuration         [ PASS / FAIL / SKIP ]
  CP-103  DNS Redirect               [ PASS / FAIL / SKIP ]

OTA UPDATE
  OTA-100 Firmware Upload            [ PASS / FAIL / SKIP ]
  OTA-101 Corrupt Firmware           [ PASS / FAIL / SKIP ]
  OTA-102 Rollback (Manual)          [ PASS / FAIL / SKIP ]
  OTA-103 OTA During Transaction     [ PASS / FAIL / SKIP ]

EDGE CASES
  EC-100  WebSocket Disconnect       [ PASS / FAIL / SKIP ]
  EC-101  WiFi Disconnect            [ PASS / FAIL / SKIP ]
  EC-102  Phase Switch Timeout       [ PASS / FAIL / SKIP ]
  EC-103  Rapid Power Limits         [ PASS / FAIL / SKIP ]
  EC-104  Malformed OCPP             [ PASS / FAIL / SKIP ]
  EC-105  Malformed MQTT             [ PASS / FAIL / SKIP ]
  EC-106  MQTT Broker Restart        [ PASS / FAIL / SKIP ]
  EC-107  Max Message Size           [ PASS / FAIL / SKIP ]
  EC-108  Concurrent Traffic         [ PASS / FAIL / SKIP ]
  EC-109  Power Cycle (Manual)       [ PASS / FAIL / SKIP ]
  EC-110  WiFi Degradation (Manual)  [ PASS / FAIL / SKIP ]
  EC-111  DHCP Lease Expiry          [ PASS / FAIL / SKIP ]
  EC-112  Software Watchdog (Manual) [ PASS / FAIL / SKIP ]
  EC-113  Hardware Watchdog (Manual) [ PASS / FAIL / SKIP ]
  EC-114  Watchdog WiFi Disconnect   [ PASS / FAIL / SKIP ]
  EC-115  Memory Pressure (Manual)   [ PASS / FAIL / SKIP ]

LONG DURATION (separate runs)
  LD-001  24h Continuous Charging    [ PASS / FAIL / SKIP ]
  LD-002  72h Idle Heartbeats        [ PASS / FAIL / SKIP ]
  LD-003  7-Day Usage Pattern        [ PASS / FAIL / SKIP ]
  LD-004  100x Phase Switches        [ PASS / FAIL / SKIP ]

================================================================
Summary: ___/48 passed, ___ failed, ___ skipped
Pass Rate: ___%

FAILED TESTS
-----------
Test ID    | Failure Reason
-----------|--------------------------------------------------



================================================================
Notes:




================================================================
```

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-02-09 | Claude | Initial test specification |
