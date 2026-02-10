# ESP32 OCPP Server Test Specification

| Field | Value |
|-------|-------|
| **Version** | 1.5 |
| **Date** | 2026-02-10 |
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
| Captive Portal | CP-100 – CP-103 | 4 | 4 | 0 |
| MQTT Transport | MT-100 – MT-103 | 4 | 4 | 0 |
| MQTT Resilience | MR-100 – MR-101 | 2 | 2 | 0 |
| Connection | TC-100 – TC-104 | 5 | 5 | 0 |
| Charging | TC-110 – TC-113 | 4 | 4 | 0 |
| Remote Commands | TC-120 – TC-123 | 4 | 4 | 0 |
| Phase Switching | TC-130 – TC-134 | 5 | 5 | 0 |
| Wallbox Emulator | WB-100 – WB-109 | 10 | 10 | 0 |
| OTA Update | OTA-100 – OTA-103 | 4 | 3 | 1 |
| Edge Cases | EC-100 – EC-115 | 14 | 10 | 4 |
| Long Duration | LD-001 – LD-004 | 4 | 4 | 0 |
| **Total** | | **62** | **57** | **5** |

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

There is no physical wallbox. The **Wallbox Emulator** (`ocpp-test-wallbox/`) is a
Python OCPP 1.6J charge point simulator that connects to the DUT via WebSocket
and generates configurable MeterValues, StatusNotifications, and transaction events.

| Component | Address / Location | Role |
|-----------|--------------------|------|
| ESP32 DUT | Ethernet DHCP (e.g. 192.168.0.105) | System under test (OCPP Central System) |
| Wallbox Emulator | `ocpp-test-wallbox/` (runs on test host) | OCPP charge point simulator (WebSocket client) |
| Wallbox Emulator Web UI | localhost:8080 | HTTP API for test automation + real-time dashboard |
| Pi (Serial Portal + WiFi Tester) | 192.168.0.87:8080 (LAN) / 192.168.4.1 (AP) | RFC2217 serial, GPIO, WiFi AP/STA, **MQTT broker** |
| MQTT Broker (test) | 192.168.4.1:1883 (on Pi) | Mosquitto on Pi — DUT reaches it via Pi's AP |
| MQTT Broker (production) | 192.168.0.203:1883 (home LAN) | Only for production-mode tests (MT-100) |
| Test Host | Any (same LAN) | Runs pytest, wallbox emulator, MQTT client |

**MQTT routing:** In test mode the DUT joins the Pi's WiFi AP (192.168.4.x subnet).
The MQTT broker runs on the Pi itself, so the DUT reaches it at **192.168.4.1:1883**.
In production-mode tests the DUT uses Ethernet and connects to the home LAN broker
at 192.168.0.203:1883.

### 2.2 Infrastructure Rules

- **MQTT Broker (Pi)**: Mosquitto runs on the Pi (192.168.4.1). Always running. Only MR-101 may restart it.
- **MQTT Broker (home)**: 192.168.0.203. Only used for production-mode tests (MT-100).
- **Serial Portal**: always running at 192.168.0.87. No test may restart it.
- **WiFi Tester AP**: started/stopped by tests as needed via `wt.ap_start()`/`wt.ap_stop()`.
- **Wallbox Emulator**: started per test (or test session). Each test connects a fresh instance to the DUT's OCPP WebSocket server.
- **DUT**: may be reset by any test. Must be restored to clean state after flash/erase operations.

### 2.3 Hardware Setup

| Component | Description | Connection |
|-----------|-------------|------------|
| DUT | WT32-ETH01 (ESP32 + LAN8720) | RFC2217 via Serial Portal (discover slot at runtime) |
| Phase Relay | Single relay for L2+L3 | GPIO 4 (output) |
| Config Button | Portal trigger | GPIO 14 (not a strapping pin) |
| Wallbox Emulator | Python OCPP 1.6J client | WebSocket to DUT Ethernet IP, port 8887 |

### 2.4 Partition Layout

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| nvs | 0x9000 | 20KB | Configuration (WiFi, MQTT credentials) |
| otadata | 0xE000 | 8KB | OTA state |
| app0 | 0x10000 | 1.75MB | Application (slot 1) |
| app1 | 0x1D0000 | 1.75MB | Application (slot 2) |
| spiffs | 0x390000 | 384KB | Web UI files |

### 2.5 DUT Initial State

The WT32-ETH01 has no DTR/CTS pin breakout; flashing requires GPIO boot sequencing
via the Serial Portal Pi. Always use `WiFiTesterDriver` — never raw curl.

**Discover DUT slot at runtime** (never hardcode):
```python
from wifi_tester_driver import WiFiTesterDriver
wt = WiFiTesterDriver("http://192.168.0.87:8080")
devices = wt.get_devices()
dut = next(s for s in devices if s["present"])
SLOT = dut["label"]   # e.g. "SLOT3"
PORT = dut["url"]     # e.g. "rfc2217://192.168.0.87:4003"
```

**Initialize Pi GPIO pins** (run once at start of every test session):
```python
# DUT → Pi pins: set to input FIRST to avoid driving DUT outputs
wt.gpio_set(22, "z")     # BCM 22 reads DUT GPIO 4 (relay state)

# Pi → DUT pins: release to input (safe default)
wt.gpio_set(17, "z")     # EN/RESET — DUT has external pullup
wt.gpio_set(18, "z")     # GPIO 0 — DUT has internal pullup
wt.gpio_set(27, "z")     # GPIO 14 — DUT has internal pullup
```

Before running any test, the DUT must be in this state:

1. Enter bootloader via GPIO (hold GPIO 0 LOW, pulse EN LOW→release):
   ```python
   import time
   wt.gpio_set(18, 0)       # Hold GPIO 0 LOW (download mode)
   wt.gpio_set(17, 0)       # EN LOW (reset)
   time.sleep(0.2)
   wt.gpio_set(17, "z")     # Release EN — DUT enters bootloader
   ```
2. Flash firmware (with `--before=no_reset` since we entered bootloader via GPIO):
   ```bash
   esptool.py --chip esp32 --port "${PORT}?ign_set_control" \
     --before=no_reset --after=hard_reset \
     write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
     0x1000  build/bootloader/bootloader.bin \
     0x8000  build/partition_table/partition-table.bin \
     0xe000  build/ota_data_initial.bin \
     0x10000 build/ocpp-esp32.bin
   ```
3. Release GPIO 0 and verify boot:
   ```python
   wt.gpio_set(18, "z")     # Release GPIO 0
   ```
4. Erase NVS (repeat GPIO boot sequence from step 1, then):
   ```bash
   esptool.py --chip esp32 --port "${PORT}?ign_set_control" \
     --before=no_reset --after=hard_reset \
     erase_region 0x9000 0x5000
   ```
5. Verify boot: serial output contains `boot:0x` and no crash backtrace
6. Verify clean config: no MQTT host configured → AP mode starts

**What "clean" means:**
- Factory default configuration
- No persisted WiFi/MQTT credentials
- Boot count at initial value
- No active OCPP sessions

### 2.6 Test Tools

| Tool | Version | Purpose | Install |
|------|---------|---------|---------|
| pytest | 8.x | Test framework | `pip install pytest pytest-asyncio` |
| ocpp-test-wallbox | local | **Wallbox emulator** (OCPP client) + MQTT client | `cd ocpp-test-wallbox && pip install -e .` |
| WiFiTesterDriver | local | Serial Portal / WiFi Tester Python driver | `pip install -e /tmp/Universal-ESP32-Tester/pytest` |
| mosquitto-clients | 2.x | MQTT pub/sub (manual testing) | `apt install mosquitto-clients` |
| esptool | 4.x | Flash/erase ESP32 | `pip install esptool` |
| idf.py | v5.4 | Build ESP-IDF firmware | `source /opt/esp-idf/export.sh` |

### 2.7 MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `ocpp/{id}/status` | Publish | Wallbox connection + connector status |
| `ocpp/{id}/session` | Publish | Active transaction with meter values |
| `ocpp/{id}/phase` | Publish | Current phase mode |
| `ocpp/{id}/command/start` | Subscribe | Start charging transaction |
| `ocpp/{id}/command/stop` | Subscribe | Stop charging transaction |
| `ocpp/{id}/command/limit` | Subscribe | Set power limit (W) |

### 2.8 Wallbox Emulator Setup

The wallbox emulator (`ocpp-test-wallbox/`) is a full OCPP 1.6J charge point simulator
with an HTTP API for test automation and a real-time Web UI dashboard.

#### Configuration

Edit `ocpp-test-wallbox/config/default.yaml`:

| Key | Default | Description |
|-----|---------|-------------|
| `wallbox.charge_point_id` | `"TEST001"` | OCPP charge point identity |
| `wallbox.ocpp_server` | `"ws://192.168.0.105:8887/ocpp/TEST001"` | DUT WebSocket URL |
| `wallbox.phase_mode` | `"3-phase"` | Initial phase mode |
| `wallbox.authorize_required` | `false` | Require Authorize before StartTransaction |
| `wallbox.max_current_a` | `32` | Maximum current (A) |
| `wallbox.reconnect_delay_sec` | `5` | Delay before reconnect on disconnect |
| `simulation.meter_interval_sec` | `10` | MeterValues reporting interval |
| `simulation.voltage_v` | `230` | Grid voltage (V) |
| `web_ui.port` | `8080` | HTTP API / dashboard port |

#### Starting the Emulator

```bash
cd ocpp-test-wallbox
python -m ocpp_test_wallbox.main run                          # default config
python -m ocpp_test_wallbox.main run --config config/test.yaml  # custom config
python -m ocpp_test_wallbox.main run --web-port 8081           # alternate port
```

#### HTTP API

All endpoints return JSON. Base URL: `http://localhost:8080`.

| Method | Endpoint | Request Body | Description |
|--------|----------|-------------|-------------|
| GET | `/api/state` | — | Current emulator state (JSON) |
| POST | `/api/plug` | — | Simulate EV plug-in → Preparing |
| POST | `/api/unplug` | — | Simulate EV unplug → Available |
| POST | `/api/start` | `{"id_tag": "evcc"}` (optional) | Start charging transaction |
| POST | `/api/stop` | — | Stop charging transaction |
| POST | `/api/phase` | `{"mode": "1-phase"}` or `{"mode": "3-phase"}` | Set phase mode |
| POST | `/api/authorize` | `{"enabled": true}` or `{"enabled": false}` | Toggle authorization requirement |
| GET | `/ws` | — | WebSocket for real-time state updates (1 Hz) |

#### State JSON Fields

`GET /api/state` returns:

```json
{
  "connected": true,
  "connector_status": "Charging",
  "transaction_id": 1,
  "phase_mode": "3-phase",
  "authorize_required": false,
  "current_limit_a": 16.0,
  "energy_wh": 1500.0,
  "power_w": 11040.0,
  "logs": [{"ts": "...", "dir": "→", "action": "MeterValues", "detail": "..."}]
}
```

#### Python Helper Functions for Test Scripts

```python
import requests, time

EMU_API = "http://localhost:8080"

def emu_state():
    """Get current emulator state."""
    return requests.get(f"{EMU_API}/api/state").json()

def emu_plug():
    """Simulate EV plug-in."""
    requests.post(f"{EMU_API}/api/plug")

def emu_unplug():
    """Simulate EV unplug."""
    requests.post(f"{EMU_API}/api/unplug")

def emu_start(id_tag="evcc"):
    """Start charging transaction."""
    requests.post(f"{EMU_API}/api/start", json={"id_tag": id_tag})

def emu_stop():
    """Stop charging transaction."""
    requests.post(f"{EMU_API}/api/stop")

def emu_set_phase(mode):
    """Set phase mode ('1-phase' or '3-phase')."""
    requests.post(f"{EMU_API}/api/phase", json={"mode": mode})

def emu_set_authorize(enabled):
    """Enable or disable authorization requirement."""
    requests.post(f"{EMU_API}/api/authorize", json={"enabled": enabled})

def wait_emu_status(target, timeout=30):
    """Poll emulator until connector_status matches target."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        state = emu_state()
        if state["connector_status"] == target:
            return state
        time.sleep(0.5)
    raise TimeoutError(f"Emulator did not reach '{target}' within {timeout}s")

def wait_emu_connected(timeout=30):
    """Poll emulator until OCPP connection is established."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        state = emu_state()
        if state["connected"]:
            return state
        time.sleep(0.5)
    raise TimeoutError(f"Emulator did not connect within {timeout}s")
```

### 2.9 DUT State Requirements by Phase

Each execution phase assumes specific DUT configuration and infrastructure state.
Use this matrix to verify prerequisites before running a phase.

#### DUT Configuration Matrix

| Phase | `test_mode` | `mqtt_host` | `wifi_ssid` | `wifi_pass` | Notes |
|-------|-------------|-------------|-------------|-------------|-------|
| 1 (Setup) | — | — | — | — | NVS erased, AP mode |
| 2 (Captive Portal) | — | — | — | — | NVS empty → auto AP mode |
| 3a (MT-100) | `false` | `192.168.0.203` | `<home SSID>` | `<home pass>` | Production mode, MQTT via Ethernet |
| 3b (MT-101) | `true` | `192.168.4.1` | `TestAP` | `password123` | Test mode, MQTT via WiFi |
| 3c (MT-102) | `false` → `true` | `192.168.4.1` | `TestAP` | `password123` | Transition test |
| 3d (MT-103) | `true` | `192.168.4.1` | `""` (empty) | — | WiFi fallback test |
| 4 (MQTT Resilience) | `true` | `192.168.4.1` | `TestAP` | `password123` | Test mode restored |
| 5–12 | `true` | `192.168.4.1` | `TestAP` | `password123` | Test mode for all remaining phases |

#### Infrastructure Matrix

| Component | Phase 1 | Phase 2 | Phase 3a | Phase 3b–3d | Phase 4 | Phase 5–7 | Phase 8 | Phase 9–12 |
|-----------|:-------:|:-------:|:--------:|:-----------:|:-------:|:---------:|:-------:|:----------:|
| Serial Portal | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Pi AP | — | — | — | Yes | Yes | Yes | Yes | Yes |
| Pi MQTT broker | — | — | — | Yes | Yes | Yes | Yes | Yes |
| Home LAN broker | — | — | Yes | — | — | — | — | — |
| Wallbox emulator | — | — | — | — | — | Started at 5 | Yes | Yes |
| Pi BCM 22 (input) | Init | — | — | — | — | — | Yes | As needed |

---

### 2.10 Precondition Check Helpers

Named precondition patterns keep Step 0 entries concise. Each pattern maps to
one or more helper checks. Use these in test setup/fixtures.

#### Named Patterns

| Pattern | Checks Performed | Used By |
|---------|-----------------|---------|
| `BASIC_DUT` | DUT reachable via Ethernet ping | All phases |
| `TEST_MODE_MQTT` | BASIC_DUT + Pi AP running + DUT MQTT connected to Pi broker | Phase 4–12 |
| `WALLBOX_READY` | TEST_MODE_MQTT + wallbox emulator connected to DUT | Phase 5–9, 11–12 |
| `PHASE_READY` | WALLBOX_READY + Pi BCM 22 initialized as input | Phase 8, parts of 9/11 |

#### Helper Functions

```python
import subprocess, time, requests

EMU_API = "http://localhost:8080"

def check_dut_reachable(dut_ip="192.168.0.105"):
    """Ping DUT Ethernet interface."""
    result = subprocess.run(["ping", "-c", "1", "-W", "2", dut_ip],
                            capture_output=True)
    assert result.returncode == 0, f"DUT not reachable at {dut_ip}"

def check_pi_ap_running(wt):
    """Verify Pi WiFi AP is active."""
    status = wt.ap_status()
    assert status.get("active"), "Pi AP not running — call wt.ap_start() first"

def check_mqtt_connected(wt, slot, timeout=10):
    """Monitor serial for MQTT connected message."""
    result = wt.serial_monitor(slot, pattern="MQTT connected", timeout=timeout)
    assert "MQTT connected" in result.get("output", ""), "DUT MQTT not connected"

def check_dut_test_mode(wt, slot, timeout=10):
    """Verify DUT is in test mode via serial."""
    result = wt.serial_monitor(slot, pattern="test_mode=ON", timeout=timeout)
    assert "test_mode=ON" in result.get("output", ""), "DUT not in test mode"

def check_wallbox_connected(timeout=15):
    """Verify wallbox emulator is connected to DUT."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            state = requests.get(f"{EMU_API}/api/state", timeout=2).json()
            if state.get("connected"):
                return state
        except requests.RequestException:
            pass
        time.sleep(1)
    raise AssertionError("Wallbox emulator not connected")

def check_pi_gpio_initialized(wt):
    """Verify Pi BCM 22 is set as input for relay readback."""
    result = wt.gpio_get()
    pin22 = result.get("pins", {}).get("22", {})
    assert pin22.get("mode") in ("input", "z"), \
        "BCM 22 not initialized — call wt.gpio_set(22, 'z') first"

# Composite pattern checks
def check_basic_dut(dut_ip="192.168.0.105"):
    check_dut_reachable(dut_ip)

def check_test_mode_mqtt(wt, slot, dut_ip="192.168.0.105"):
    check_dut_reachable(dut_ip)
    check_pi_ap_running(wt)
    check_mqtt_connected(wt, slot)

def check_wallbox_ready(wt, slot, dut_ip="192.168.0.105"):
    check_test_mode_mqtt(wt, slot, dut_ip)
    check_wallbox_connected()

def check_phase_ready(wt, slot, dut_ip="192.168.0.105"):
    check_wallbox_ready(wt, slot, dut_ip)
    check_pi_gpio_initialized(wt)
```

---

### 2.11 Phase Transition Procedures

DUT configuration changes between phases require the captive portal API.
`WiFiTesterDriver` does not provide `serial_send()` or `ssh_exec()`, so all
automated config changes go through the portal HTTP API.

> **Manual alternative:** `config_set` via serial terminal (e.g. `idf.py monitor`)
> works for manual testing but is not available from automated test scripts.

#### Phase 2 → 3a: Portal → Production Mode

```python
# 1. Enter captive portal (DUT already in AP mode after Phase 2)
#    If DUT left portal: hold GPIO 14 for 5s
wt.gpio_set(27, 0); time.sleep(5.5); wt.gpio_set(27, "z")

# 2. Join DUT AP
wt.sta_join("OCPP-ESP32-XXXX")  # replace XXXX with actual suffix

# 3. Configure for production mode
import requests
requests.post("http://192.168.1.1/api/config", json={
    "test_mode": 0,
    "mqtt_host": "192.168.0.203",
    "mqtt_port": 1883,
    "mqtt_prefix": "ocpp",
    "wifi_ssid": "<home_ssid>",
    "wifi_pass": "<home_pass>"
})

# 4. Reboot
requests.post("http://192.168.1.1/api/reboot")
wt.sta_leave()

# 5. Verify via serial
result = wt.serial_monitor(SLOT, pattern="test_mode=OFF", timeout=15)
assert "test_mode=OFF" in result["output"]
```

#### Phase 3a → 3b: Production → Test Mode

```python
# 1. Enter captive portal (hold GPIO 14)
wt.gpio_set(27, 0); time.sleep(5.5); wt.gpio_set(27, "z")

# 2. Join DUT AP
wt.sta_join("OCPP-ESP32-XXXX")

# 3. Configure for test mode
requests.post("http://192.168.1.1/api/config", json={
    "test_mode": 1,
    "mqtt_host": "192.168.4.1",
    "mqtt_port": 1883,
    "mqtt_prefix": "ocpp",
    "wifi_ssid": "TestAP",
    "wifi_pass": "password123"
})

# 4. Start Pi AP BEFORE rebooting DUT (so DUT can connect on boot)
wt.sta_leave()
wt.ap_start("TestAP", "password123")

# 5. Reboot DUT
requests.post("http://192.168.1.1/api/reboot")

# 6. Verify MQTT connected via WiFi
result = wt.serial_monitor(SLOT, pattern="MQTT connected", timeout=30)
assert "MQTT connected" in result["output"]
```

#### Phase 3b → 3d: Test Mode → Empty SSID

```python
# 1. Enter captive portal
wt.gpio_set(27, 0); time.sleep(5.5); wt.gpio_set(27, "z")

# 2. Join DUT AP, set empty wifi_ssid
wt.sta_join("OCPP-ESP32-XXXX")
requests.post("http://192.168.1.1/api/config", json={
    "test_mode": 1,
    "wifi_ssid": "",
    "wifi_pass": ""
})
requests.post("http://192.168.1.1/api/reboot")
wt.sta_leave()

# 3. Verify: no WiFi STA, MQTT falls back to Ethernet
result = wt.serial_monitor(SLOT, pattern="MQTT connected", timeout=15)
# Expect NO "WiFi STA" messages in output
```

#### Phase 3d → 4: Restore Test Mode

Same as Phase 3a → 3b procedure above. Restores `wifi_ssid=TestAP` and
`test_mode=1` via captive portal.

#### Phase 4 → 5: Start Wallbox Emulator

No DUT reconfiguration needed. Start the wallbox emulator process:

```python
import subprocess
emu_proc = subprocess.Popen(
    ["python", "-m", "ocpp_test_wallbox.main", "run"],
    cwd="ocpp-test-wallbox"
)
# Verify connection
check_wallbox_connected(timeout=15)
```

#### Phase 7 → 8: Initialize GPIO for Relay Readback

```python
# Set BCM 22 as input for relay state readback
wt.gpio_set(22, "z")

# Verify
result = wt.gpio_get()
assert result["pins"]["22"]["mode"] in ("input", "z")
```

---

## 3. Setup Test Cases

These tests establish a clean, known starting state for all subsequent tests.

#### TC-000: Flash and Provision DUT

**Precondition:**
- Firmware built: `ls ocpp-esp32/build/ocpp-esp32.bin` exits 0
- Serial Portal accessible: `curl -s http://192.168.0.87:8080/api/devices` returns JSON

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | Verify Serial Portal reachable: `wt.get_devices()` returns device list | Portal online |
| 0b | Verify firmware file exists: `os.path.isfile("ocpp-esp32/build/ocpp-esp32.bin")` | File present |
| 0c | Initialize Pi GPIOs: `wt.gpio_set(22, "z")`, `wt.gpio_set(17, "z")`, `wt.gpio_set(18, "z")`, `wt.gpio_set(27, "z")` | All pins safe defaults |
| 1 | Enter bootloader: `wt.gpio_set(18, 0)`, `wt.gpio_set(17, 0)`, sleep, `wt.gpio_set(17, "z")` | DUT in download mode |
| 2 | Flash firmware via esptool (`--before=no_reset`) | `esptool.py write_flash` exits 0 |
| 3 | Release boot pin: `wt.gpio_set(18, "z")` | DUT free to boot normally |
| 4 | Re-enter bootloader (repeat step 1), erase NVS | `esptool.py erase_region 0x9000 0x5000` exits 0 |
| 5 | Reset DUT: `wt.gpio_set(17, 0)`, sleep, `wt.gpio_set(17, "z")` | Serial output shows boot sequence |
| 6 | Monitor serial: `wt.serial_monitor(SLOT, pattern="boot:", timeout=10)` | Version matches build |
| 7 | Verify AP mode started (no mqtt_host) | Serial shows `Entering CONFIG mode` |

**Pass Criteria:** DUT is flashed, NVS erased, boots into AP mode with correct version.

**Automation:** `pytest tests/test_setup.py::test_flash_provision -v`

---

#### TC-001: Verify Clean State

**Precondition:**
- TC-000 passed
- DUT running: serial output shows normal boot

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | Verify DUT booted: `wt.serial_monitor(SLOT, pattern="boot:0x", timeout=10)` | Boot pattern found, no crash backtrace |
| 1 | Check serial console `config` command | All values at defaults |
| 2 | Check heap memory | Free heap > 100KB |
| 3 | Verify no WiFi STA connection | WiFi status shows "disconnected" |
| 4 | Verify no MQTT connection | MQTT status shows "disconnected" |
| 5 | Verify AP mode active | AP SSID visible in WiFi scan |

**Pass Criteria:** DUT is in a known clean state with all defaults applied and no residual configuration.

**Automation:** `pytest tests/test_setup.py::test_verify_clean -v`

---

## 4. Captive Portal Tests

Validates WiFi provisioning and configuration via captive portal. These run early
because captive portal is how the DUT gets its initial configuration (MQTT host,
WiFi credentials). All subsequent tests depend on a configured DUT.

#### CP-100: Enter Captive Portal Mode

**Precondition:**
- DUT booted with no mqtt_host configured (NVS erased) → auto-enters AP mode
- OR: DUT running in normal mode, GPIO 14 (config button) accessible

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_basic_dut()` — verify DUT reachable via ping or serial | DUT responsive |
| 1 | **Auto-entry:** Erase NVS, reset DUT | Boots into config mode (no mqtt_host) |
| 2 | **Button entry:** Hold GPIO 14 LOW for 5 seconds via tester GPIO 27 | `Entering config mode` logged |
| 3 | Verify AP starts | SSID `OCPP-ESP32-XXXX` visible in WiFi scan |
| 4 | Tester joins DUT AP: `wt.sta_join("OCPP-ESP32-XXXX")` | Connected, IP 192.168.1.x |
| 5 | HTTP GET `http://192.168.1.1/` via tester relay | Portal page HTML returned |

**Pass Criteria:** DUT enters captive portal mode via NVS-empty boot or GPIO button hold.

**Automation:** `pytest tests/test_captive_portal.py::test_enter_portal -v`

---

#### CP-101: WiFi Credential Provisioning

**Precondition:**
- DUT in captive portal mode (AP active)
- Tester connected to DUT AP as station

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | Verify DUT AP visible: `wt.sta_scan()` contains `OCPP-ESP32-*` | AP SSID found |
| 1 | GET `/wifi` via tester relay | WiFi config page loads |
| 2 | GET `/api/wifi/scan` | Network list JSON returned |
| 3 | POST `/api/config` with `wifi_ssid`, `wifi_pass` | `{"ok": true}` |
| 4 | POST `/api/reboot` | DUT reboots |
| 5 | Tester leaves DUT AP: `wt.sta_leave()` | Disconnected |
| 6 | Monitor serial for WiFi connection | `WiFi connected` or `WiFi STA` in log |

**Pass Criteria:** WiFi credentials saved and applied after reboot.

**Automation:** `pytest tests/test_captive_portal.py::test_wifi_provision -v`

---

#### CP-102: MQTT Configuration

**Precondition:**
- DUT in captive portal mode
- Tester connected to DUT AP

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | Verify tester connected to DUT AP: `wt.sta_status()` shows IP 192.168.1.x | Connected to DUT |
| 1 | POST `/api/config` with `mqtt_host=192.168.4.1`, `mqtt_port=1883`, `mqtt_prefix=ocpp` | `{"ok": true}` |
| 2 | Also set `wifi_ssid` and `wifi_pass` for the tester AP, `test_mode=1` | WiFi + test mode saved |
| 3 | POST `/api/reboot` | DUT reboots |
| 4 | Tester leaves DUT AP, starts tester AP: `wt.sta_leave()`, `wt.ap_start(...)` | AP ready |
| 5 | Monitor serial: `MQTT connected` | MQTT via WiFi to Pi broker (192.168.4.1) |

**Pass Criteria:** MQTT settings saved, DUT exits config mode on reboot (mqtt_host now set), connects to Pi broker via WiFi.

**Automation:** `pytest tests/test_captive_portal.py::test_mqtt_config -v`

---

#### CP-103: DNS Redirect (Captive Portal Detection)

**Precondition:**
- DUT in AP mode
- Tester connected to DUT AP (192.168.1.x)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | Verify portal HTTP reachable: `requests.get("http://192.168.1.1/")` returns 200 | Portal serving pages |
| 1 | Query any domain via DNS | `nslookup google.com` |
| 2 | Verify response | Returns 192.168.1.1 (portal IP) |
| 3 | HTTP GET to random domain via tester relay | Request received |
| 4 | Verify redirect | Redirected to captive portal |

**Pass Criteria:** All DNS queries redirected to portal, HTTP requests redirected.

**Automation:** `pytest tests/test_captive_portal.py::test_dns_redirect -v`

---

## 5. MQTT Transport Mode Tests

Validates switching MQTT between Ethernet (production) and WiFi (test mode).
These tests follow captive portal because they depend on a configured DUT.

#### MT-100: Boot in Production Mode (MQTT via Ethernet)

**Precondition:**
- DUT configured via captive portal: mqtt_host set, wifi_ssid set, test_mode=false
- MQTT broker reachable over Ethernet at 192.168.0.203:1883

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | Verify production config set via captive portal (Section 2.11: Phase 2 → 3a) | `test_mode=false`, `mqtt_host=192.168.0.203` |
| 0b | Verify home LAN broker reachable: `ping -c 1 192.168.0.203` | Broker host responds |
| 1 | Reset DUT | Boot sequence in serial log |
| 2 | Verify serial: `Starting in NORMAL mode (test_mode=OFF)` | Normal mode |
| 3 | Verify serial: NO `WiFi STA` messages | WiFi not started |
| 4 | Verify serial: `Ethernet IP: 192.168.0.x` | Ethernet up |
| 5 | Verify serial: `MQTT connected` | MQTT over Ethernet |
| 6 | Publish to MQTT status topic | Message arrives at broker |

**Pass Criteria:** MQTT connects via Ethernet only, WiFi radio stays off.

**Automation:** `pytest tests/test_mqtt_transport.py::test_production_mode -v`

---

#### MT-101: Boot in Test Mode (MQTT via WiFi)

**Precondition:**
- DUT configured: mqtt_host set, wifi_ssid set, test_mode=true
- WiFi Tester AP running: `wt.ap_start("TestAP", "password123")`
- MQTT broker reachable via WiFi tester network

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | Start Pi AP: `wt.ap_start("TestAP", "password123")` | AP active |
| 0b | Verify Pi MQTT broker: `mosquitto_pub -h 192.168.0.87 -t test -m ping` exits 0 | Broker responsive |
| 1 | Start WiFi tester AP | AP active on 192.168.4.1 |
| 2 | Configure DUT via captive portal (Section 2.11: Phase 3a → 3b): `test_mode=1`, `wifi_ssid=TestAP` | Settings saved |
| 3 | Reset DUT | Boot sequence |
| 4 | Verify serial: `Starting in NORMAL mode (test_mode=ON)` | Test mode |
| 5 | Verify serial: `Test mode: starting WiFi STA for MQTT` | WiFi STA started |
| 6 | Verify serial: `MQTT connected` | MQTT over WiFi |
| 7 | Verify tester sees DUT as connected station | `wt.ap_status()` shows DUT IP |

**Pass Criteria:** MQTT connects via WiFi through tester AP, Ethernet still active for OCPP.

> **Automation note:** Step 2 references `config_set` via serial console. For automation,
> use captive portal API (Section 2.11). `config_set` is available via direct serial
> terminal only.

**Automation:** `pytest tests/test_mqtt_transport.py::test_test_mode -v`

---

#### MT-102: Switch from Production to Test Mode

**Precondition:**
- DUT running in production mode (test_mode=false)
- MQTT connected via Ethernet
- WiFi tester AP running

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | Verify DUT in production mode: serial shows `test_mode=OFF` | Production mode confirmed |
| 0b | Verify Pi AP running: `check_pi_ap_running(wt)` | AP active |
| 1 | Verify MQTT connected via Ethernet | Baseline |
| 2 | Via captive portal (Section 2.11): set `test_mode=1` | Setting persisted |
| 3 | Via captive portal: set `wifi_ssid=TestAP`, `wifi_pass=password123` | WiFi creds set |
| 4 | Reboot DUT (serial: `reboot`) | Restart |
| 5 | Verify WiFi STA connects to tester AP | DUT appears in station list |
| 6 | Verify MQTT reconnects via WiFi | `MQTT connected` in serial |
| 7 | OCPP still works on Ethernet | Wallbox can connect |

**Pass Criteria:** Seamless transition from Ethernet MQTT to WiFi MQTT after config change + reboot.

> **Automation note:** Steps 2–3 reference `config_set` via serial console. For automation,
> use captive portal API (Section 2.11). `config_set` is available via direct serial
> terminal only.

**Automation:** `pytest tests/test_mqtt_transport.py::test_switch_to_test_mode -v`

---

#### MT-103: No WiFi SSID in Test Mode (Fallback)

**Precondition:**
- DUT configured: test_mode=true, wifi_ssid="" (empty)
- MQTT broker reachable via Ethernet

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | Verify empty SSID config set via captive portal (Section 2.11: Phase 3b → 3d) | `wifi_ssid=""` confirmed |
| 1 | Reset DUT | Boot sequence |
| 2 | Verify serial: `Starting in NORMAL mode (test_mode=ON)` | Test mode |
| 3 | Verify NO `WiFi STA` messages | No WiFi SSID → skip WiFi |
| 4 | Verify serial: `MQTT connected` | Falls back to Ethernet |

**Pass Criteria:** Missing WiFi SSID in test mode falls back to Ethernet MQTT gracefully.

**Automation:** `pytest tests/test_mqtt_transport.py::test_test_mode_no_ssid -v`

---

## 5.5 MQTT Resilience Tests

Tests MQTT error handling and recovery **before** the wallbox emulator is connected.
These run on the Pi's WiFi AP (192.168.4.x subnet) — the Pi controls both the AP
and the Mosquitto broker, so broker restarts are isolated from the home network.

#### MR-100: Malformed MQTT Command

**Precondition:**
- DUT subscribed to command topics via Pi broker (192.168.4.1:1883)
- MQTT connected over WiFi AP
- No wallbox connected (DUT handles commands independently)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_test_mode_mqtt(wt, SLOT)` — verify DUT reachable, Pi AP running, MQTT connected | TEST_MODE_MQTT |
| 1 | Publish invalid JSON to `command/start` | Message ignored |
| 2 | Publish `{"power_w": "invalid"}` to `command/limit` | Error logged |
| 3 | Publish empty message to `command/stop` | Treated as valid (empty = stop all) |
| 4 | Verify DUT operational | No crash, heap stable |

**Pass Criteria:** Invalid MQTT commands don't crash DUT. No wallbox connection required.

**Automation:** `pytest tests/test_mqtt_resilience.py::test_malformed_mqtt -v`

---

#### MR-101: MQTT Broker Restart

**Precondition:**
- DUT connected to MQTT via Pi WiFi AP (broker at 192.168.4.1:1883)
- Active message publishing (status topic)
- No wallbox connected
- Baseline reconnect count: `R_before`
- Test host has SSH key access to Pi (`ssh-copy-id pi@192.168.0.87`)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_test_mode_mqtt(wt, SLOT)` — verify DUT reachable, Pi AP running, MQTT connected | TEST_MODE_MQTT |
| 1 | Stop Mosquitto on Pi: `subprocess.run(["ssh", "pi@192.168.0.87", "sudo", "systemctl", "stop", "mosquitto"])` | Connection lost |
| 2 | Verify DUT detects disconnect | Serial log: "MQTT disconnected" |
| 3 | Wait 10 seconds | DUT retrying |
| 4 | Restart Mosquitto: `subprocess.run(["ssh", "pi@192.168.0.87", "sudo", "systemctl", "start", "mosquitto"])` | Broker available |
| 5 | Verify DUT reconnects | Within 30 seconds |
| 6 | Verify publishing resumes | Status messages flowing |
| 7 | Check reconnect count | `R_before + 1` |

**Pass Criteria:** Automatic MQTT reconnection after broker restart on the Pi's isolated network.

> **Infrastructure note:** This test uses `subprocess.run(["ssh", ...])` to control
> Mosquitto on the Pi. The test host needs SSH key access: `ssh-copy-id pi@192.168.0.87`.
> `WiFiTesterDriver` does not provide `ssh_exec()`.

**Automation:** `pytest tests/test_mqtt_resilience.py::test_mqtt_broker_restart -v`

---

## 6. Standard Test Cases

Core functionality tests validating the primary features of the SUT.

### 6.1 Connection Tests

Validates OCPP WebSocket connection handling between wallbox and server.

#### TC-100: Wallbox WebSocket Connection

**Precondition:**
- DUT running in normal mode: Ethernet interface up
- DUT IP reachable: `ping 192.168.4.1` succeeds
- No existing WebSocket connections

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_test_mode_mqtt(wt, SLOT)` | TEST_MODE_MQTT |
| 0b | Verify DUT Ethernet: `check_dut_reachable()` | DUT IP responds |
| 0c | Verify no stale wallbox: `emu_state()` raises or `connected: false` | No prior connection |
| 1 | Connect wallbox emulator via WebSocket to `ws://{DUT_ETH_IP}:8887/ocpp/TEST001` | Connection accepted |
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
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
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
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
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
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
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
| 0 | `check_test_mode_mqtt(wt, SLOT)` — verify MQTT still connected after disconnect | TEST_MODE_MQTT (wallbox disconnected state) |
| 1 | Reconnect wallbox emulator | Connection accepted |
| 2 | Send BootNotification | Response received |
| 3 | Verify status in response | `status: Accepted` |
| 4 | Verify MQTT status published | `connected: true` |
| 5 | Send StatusNotification (Available) | Normal operation resumed |

**Pass Criteria:** Wallbox can reconnect after disconnection, full functionality restored.

**Automation:** `pytest tests/test_connection.py::test_reconnection -v`

---

### 6.2 Charging Tests

Validates charging transaction lifecycle.

#### TC-110: Basic Charging Cycle

**Precondition:**
- Wallbox connected, status Available
- No active transaction
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify `connector_status: Available` | WALLBOX_READY, Available |
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

> **Emulator note:** WB-101 covers the same cycle driven entirely via the emulator HTTP API. This test uses raw WebSocket messages; WB-101 validates the higher-level API-driven flow.

**Automation:** `pytest tests/test_charging.py::test_basic_cycle -v`

---

#### TC-111: Authorization Accept All Mode

**Precondition:**
- DUT configured with `auth_mode: accept_all`
- Wallbox connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
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
| 0 | `check_wallbox_ready(wt, SLOT)` + verify active transaction (`transaction_id` not None) | WALLBOX_READY, transaction active |
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
| 0 | `check_wallbox_ready(wt, SLOT)` + verify no active transaction | WALLBOX_READY, no transaction |
| 1 | Start transaction | Get `transactionId: N` |
| 2 | Stop transaction | Transaction N stopped |
| 3 | Start new transaction | Get `transactionId: N+1` |
| 4 | Verify IDs are unique | No ID reuse |

**Pass Criteria:** Transaction IDs are assigned sequentially and uniquely.

**Automation:** `pytest tests/test_charging.py::test_transaction_ids -v`

---

### 6.3 Remote Command Tests

Validates MQTT-initiated commands to the wallbox.

#### TC-120: Remote Start via MQTT

**Precondition:**
- Wallbox connected, status Available
- MQTT connected
- No active transaction

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify `connector_status: Available` | WALLBOX_READY, Available |
| 1 | Publish `{"id_tag": "ENERGY_MANAGER"}` to `ocpp/TEST001/command/start` | Command received |
| 2 | Wait for RemoteStartTransaction on WebSocket | Wallbox receives request |
| 3 | Wallbox responds Accepted | Response sent |
| 4 | Wallbox sends StartTransaction | Transaction begins |
| 5 | Verify MQTT session published | Session active |

**Pass Criteria:** MQTT start command triggers RemoteStartTransaction to wallbox.

> **Emulator note:** The wallbox emulator handles RemoteStartTransaction automatically (accepts and begins transaction). No manual WebSocket response needed when using the emulator.

**Automation:** `pytest tests/test_remote.py::test_remote_start -v`

---

#### TC-121: Remote Stop via MQTT

**Precondition:**
- Active charging transaction
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify active transaction | WALLBOX_READY, transaction active |
| 1 | Publish `{}` to `ocpp/TEST001/command/stop` | Command received |
| 2 | Wait for RemoteStopTransaction on WebSocket | Wallbox receives request |
| 3 | Wallbox responds Accepted | Response sent |
| 4 | Wallbox sends StopTransaction | Transaction ends |
| 5 | Verify MQTT session updated | Session stopped |

**Pass Criteria:** MQTT stop command triggers RemoteStopTransaction to wallbox.

> **Emulator note:** The wallbox emulator handles RemoteStopTransaction automatically (stops transaction and sends StopTransaction + StatusNotification). No manual WebSocket response needed when using the emulator.

**Automation:** `pytest tests/test_remote.py::test_remote_stop -v`

---

#### TC-122: Power Limit via MQTT

**Precondition:**
- Active charging transaction
- Charging at 11 kW (3-phase)
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify active transaction, Charging status | WALLBOX_READY, Charging |
| 1 | Publish `{"power_w": 5500}` to `ocpp/TEST001/command/limit` | Command received |
| 2 | Wait for SetChargingProfile on WebSocket | Profile sent to wallbox |
| 3 | Verify profile limit | Limit set to ~24A (5500W / 230V) |
| 4 | Wallbox applies profile | MeterValues show reduced power |
| 5 | Verify within 10% tolerance | Power between 4950W and 6050W |

**Pass Criteria:** MQTT power limit command translates to SetChargingProfile.

> **Emulator note:** Verify emulator received the profile: `emu_state()["current_limit_a"]` should reflect the new limit (e.g. ≈24A for 5500W). See WB-105 for dedicated profile verification.

**Automation:** `pytest tests/test_power_profiles.py::test_mqtt_power_limit -v`

---

#### TC-123: Power Limit Zero (Stop Charging)

**Precondition:**
- Active charging transaction
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify active transaction | WALLBOX_READY, transaction active |
| 1 | Publish `{"power_w": 0}` to `ocpp/TEST001/command/limit` | Command received |
| 2 | Wait for SetChargingProfile | Profile with 0A limit |
| 3 | Wallbox stops drawing power | MeterValues show 0W |
| 4 | Transaction remains active | Status still "Charging" or "SuspendedEVSE" |

**Pass Criteria:** Zero power limit suspends charging without stopping transaction.

> **Emulator note:** Verify emulator state: `emu_state()["connector_status"]` should show `SuspendedEVSE` and `current_limit_a` should be 0. See WB-106 for dedicated suspend/resume verification.

**Automation:** `pytest tests/test_power_profiles.py::test_zero_power_limit -v`

---

### 6.4 Phase Switching Tests

Validates 1-phase ↔ 3-phase switching with safety interlocks.

#### Phase Test Architecture

Phase switching tests coordinate three components:

1. **DUT** — controls the relay (GPIO 4) and runs the phase switching state machine
2. **Pi (WiFi Tester)** — reads relay state via BCM 22 (wired to DUT GPIO 4)
3. **Wallbox Emulator** — adjusts MeterValues (L2/L3 current/voltage) based on phase mode

```
  DUT GPIO 4 (relay output) ────wire────► Pi BCM 22 (input, readback)
       │                                        │
       │ relay drives L2+L3                     │ test reads relay state
       │                                        ▼
       │                               Test script detects change
       │                                        │
       │                                        ▼
       │                               POST /api/phase to wallbox emulator
       │                                        │
       ▼                                        ▼
  Wallbox sees L2+L3              Emulator switches to 1-phase or 3-phase
  connected/disconnected          → MeterValues: L2/L3 current=0, voltage=0
```

**Relay readback helper** (used by all phase tests):
```python
def read_relay_state(wt):
    """Read DUT relay state via Pi BCM 22.
    Returns: 0 = 1-phase (relay off), 1 = 3-phase (relay on)"""
    result = wt.gpio_get()
    return result["pins"].get("22", {}).get("value", 0)

def wait_relay_change(wt, target, timeout=30):
    """Poll BCM 22 until relay reaches target state."""
    import time
    deadline = time.time() + timeout
    while time.time() < deadline:
        if read_relay_state(wt) == target:
            return True
        time.sleep(0.5)
    return False
```

**Wallbox emulator phase sync** (called when relay changes):
```python
import requests

WALLBOX_API = "http://localhost:8080"  # wallbox emulator web UI

def sync_emulator_phase(wt):
    """Read relay state and tell wallbox emulator to match."""
    relay = read_relay_state(wt)
    mode = "3-phase" if relay == 1 else "1-phase"
    requests.post(f"{WALLBOX_API}/api/phase", json={"mode": mode})
    return mode
```

---

#### TC-130: Phase Switch 3→1 (Automatic)

**Precondition:**
- Wallbox emulator connected, charging at 7 kW (3-phase mode)
- Phase status: `phase_mode: 3-phase`
- MQTT connected
- Relay readback: `read_relay_state(wt) == 1` (GPIO 4 HIGH)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_phase_ready(wt, SLOT)` | PHASE_READY |
| 0b | Verify relay baseline: `assert read_relay_state(wt) == 1` + emulator `phase_mode: 3-phase` | 3-phase baseline |
| 1 | Verify relay HIGH: `assert read_relay_state(wt) == 1` | 3-phase confirmed |
| 2 | Publish `{"power_w": 3500}` to `command/limit` | Below 4.1 kW threshold |
| 3 | DUT initiates phase switch | Serial: `Phase switch: 3-phase → 1-phase` |
| 4 | RemoteStopTransaction received by emulator | Emulator stops transaction |
| 5 | Emulator sends StatusNotification (Available) | Safe to switch |
| 6 | Wait 5s safety delay | Relay still HIGH |
| 7 | Poll relay: `wait_relay_change(wt, 0, timeout=15)` | BCM 22 goes LOW (relay off) |
| 8 | Sync emulator: `sync_emulator_phase(wt)` → `"1-phase"` | Emulator now generates 1-phase MeterValues |
| 9 | RemoteStartTransaction received by emulator | Emulator resumes charging |
| 10 | Emulator sends MeterValues: L2/L3 voltage=0V, current=0A | DUT sees 1-phase confirmed |
| 11 | Verify MQTT `phase` topic | `phase_mode: 1-phase` |
| 12 | Verify MQTT `session` topic | Power divided by 3 (correction applied) |

**Pass Criteria:** Complete 3→1 phase switch under 30s, relay only switches when not charging, emulator MeterValues confirm L2/L3 disconnected.

**Automation:** `pytest tests/test_phase.py::test_switch_3_to_1 -v`

---

#### TC-131: Phase Switch 1→3 (Automatic)

**Precondition:**
- Wallbox emulator connected, charging at 3 kW (1-phase mode)
- Phase status: `phase_mode: 1-phase`
- Relay readback: `read_relay_state(wt) == 0` (GPIO 4 LOW)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_phase_ready(wt, SLOT)` | PHASE_READY |
| 0b | Verify relay baseline: `assert read_relay_state(wt) == 0` + emulator `phase_mode: 1-phase` | 1-phase baseline |
| 1 | Verify relay LOW: `assert read_relay_state(wt) == 0` | 1-phase confirmed |
| 2 | Publish `{"power_w": 7500}` to `command/limit` | Above 4.1 kW threshold |
| 3 | DUT initiates phase switch | Serial: `Phase switch: 1-phase → 3-phase` |
| 4 | RemoteStopTransaction received by emulator | Emulator stops transaction |
| 5 | Emulator sends StatusNotification (Available) | Safe to switch |
| 6 | Wait 5s safety delay | Relay still LOW |
| 7 | Poll relay: `wait_relay_change(wt, 1, timeout=15)` | BCM 22 goes HIGH (relay on) |
| 8 | Sync emulator: `sync_emulator_phase(wt)` → `"3-phase"` | Emulator now generates 3-phase MeterValues |
| 9 | RemoteStartTransaction received by emulator | Emulator resumes charging |
| 10 | Emulator sends MeterValues: L2/L3 voltage=230V, current>0A | DUT sees 3-phase confirmed |
| 11 | Verify MQTT `phase` topic | `phase_mode: 3-phase` |
| 12 | Verify MQTT `session` topic | Power 1:1 (no correction) |

**Pass Criteria:** Complete 1→3 phase switch under 30s, relay only switches when not charging.

**Automation:** `pytest tests/test_phase.py::test_switch_1_to_3 -v`

---

#### TC-132: Phase Switch Safety — No Switch Under Load

**Precondition:**
- Wallbox emulator charging actively (status: Charging)
- MeterValues showing > 0 W
- Record initial relay state: `relay_before = read_relay_state(wt)`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_phase_ready(wt, SLOT)` | PHASE_READY |
| 0b | Verify emulator Charging + `power_w > 0` | Active load confirmed |
| 1 | Record relay: `relay_before = read_relay_state(wt)` | Initial state captured |
| 2 | Publish phase switch command via MQTT | Switch initiated |
| 3 | Verify DUT sends RemoteStopTransaction **first** | Stop before switch |
| 4 | Poll BCM 22 for 5s while emulator still reports power > 0 W | `read_relay_state(wt) == relay_before` (unchanged) |
| 5 | Emulator responds to RemoteStop, sends Available | Now safe |
| 6 | After safety delay, relay changes | `read_relay_state(wt) != relay_before` |

**Pass Criteria:** Relay NEVER switches while power is flowing. BCM 22 readback proves relay unchanged until Available status.

**Automation:** `pytest tests/test_phase.py::test_no_switch_under_load -v`

---

#### TC-133: Phase Switch Voltage Verification Failure

**Precondition:**
- Wallbox emulator connected, reports per-phase voltage in MeterValues
- Charging stopped (Available status)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_phase_ready(wt, SLOT)` | PHASE_READY |
| 0b | Verify emulator `connector_status: Available` + relay baseline | Available, ready for switch |
| 1 | Initiate 3→1 phase switch | Switch sequence starts |
| 2 | Relay switches: `wait_relay_change(wt, 0)` | BCM 22 goes LOW |
| 3 | **Do NOT sync emulator** — keep it in 3-phase mode | Emulator still reports L2/L3 voltage=230V |
| 4 | RemoteStartTransaction → emulator resumes | Emulator sends 3-phase MeterValues |
| 5 | DUT receives MeterValues with L2/L3 voltage > 50V | Voltage mismatch detected |
| 6 | Verify serial: `Voltage mismatch` error | DUT logs error |
| 7 | Verify MQTT error published | `error: voltage_mismatch` on phase topic |

**Pass Criteria:** DUT detects that L2/L3 voltage doesn't match expected 1-phase state and reports error. This simulates a stuck relay scenario.

**Automation:** `pytest tests/test_phase.py::test_voltage_mismatch -v`

---

#### TC-134: Power Correction in 1-Phase Mode

**Precondition:**
- Operating in 1-phase mode: `read_relay_state(wt) == 0`
- Wallbox emulator in 3-phase mode (intentionally — simulates real wallbox behavior)
- Emulator reports 11 kW as raw power

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_phase_ready(wt, SLOT)` | PHASE_READY |
| 0b | Verify 1-phase mode: `read_relay_state(wt) == 0` | Relay LOW confirmed |
| 1 | Verify 1-phase: `assert read_relay_state(wt) == 0` | Confirmed |
| 2 | Emulator sends MeterValues: 11000 W (3-phase equivalent) | Raw value received by DUT |
| 3 | DUT applies correction: 11000 / 3 = 3667 W | Corrected value |
| 4 | Check MQTT session topic | `power_w: 3667` (±5%) |
| 5 | Switch to 3-phase, sync emulator | `sync_emulator_phase(wt)` |
| 6 | Emulator sends MeterValues: 11000 W | Same raw value |
| 7 | Check MQTT session topic | `power_w: 11000` (no correction) |

**Pass Criteria:** In 1-phase mode all power values divided by 3 before MQTT publishing; in 3-phase mode values pass through unchanged.

**Automation:** `pytest tests/test_phase.py::test_power_correction -v`

---

### 6.5 Wallbox Emulator Tests

Validates the wallbox emulator integration with the DUT, exercising the full
OCPP charge point lifecycle via the emulator's HTTP API and verifying DUT
behavior through MQTT observation. All tests are fully automated — no serial
console or GPIO interaction required.

#### WB-100: Emulator Boot and Connection

**Precondition:**
- DUT running in normal mode, Ethernet up, OCPP WebSocket server listening on port 8887
- MQTT connected (Pi broker)
- Emulator not yet started

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_test_mode_mqtt(wt, SLOT)` | TEST_MODE_MQTT |
| 0b | Verify DUT Ethernet: `check_dut_reachable()` | DUT IP responds |
| 0c | Verify emulator not running: `requests.get(EMU_API)` raises `ConnectionError` | No prior emulator |
| 1 | Start emulator: `python -m ocpp_test_wallbox.main run` | Process starts |
| 2 | Wait for connection: `wait_emu_connected(timeout=15)` | `connected: true` |
| 3 | Verify emulator state: `emu_state()` | `connector_status: Available` |
| 4 | Verify MQTT status topic | `ocpp/TEST001/status` shows `connected: true` |
| 5 | Verify BootNotification in emulator logs | Log entry: `← BootNotification` with `Accepted` |

**Pass Criteria:** Emulator connects to DUT, BootNotification accepted, MQTT status published.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_emulator_boot -v`

---

#### WB-101: Emulator-Driven Charging Cycle

**Precondition:**
- WB-100 passed: emulator connected, status Available
- No active transaction

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify `connector_status: Available` | WALLBOX_READY, Available |
| 1 | Plug in: `emu_plug()` | `connector_status: Preparing` |
| 2 | Wait: `wait_emu_status("Preparing")` | Status confirmed |
| 3 | Start charging: `emu_start()` | Transaction begins |
| 4 | Wait: `wait_emu_status("Charging")` | `connector_status: Charging` |
| 5 | Verify `emu_state()` | `transaction_id` is not None, `power_w > 0` |
| 6 | Verify MQTT session topic | `ocpp/TEST001/session` shows active transaction |
| 7 | Wait 15 seconds | MeterValues accumulate |
| 8 | Verify `emu_state()["energy_wh"] > 0` | Energy counter increasing |
| 9 | Stop charging: `emu_stop()` | Transaction stops |
| 10 | Wait: `wait_emu_status("Finishing")` | Status confirmed |
| 11 | Unplug: `emu_unplug()` | EV disconnected |
| 12 | Wait: `wait_emu_status("Available")` | Cycle complete |
| 13 | Verify MQTT session cleared | Transaction no longer active |

**Pass Criteria:** Full plug→start→meter→stop→unplug cycle completes via HTTP API with correct MQTT updates at each stage.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_charging_cycle -v`

---

#### WB-102: Per-Phase MeterValues (3-Phase)

**Precondition:**
- Emulator connected, charging active
- Phase mode: 3-phase (`emu_set_phase("3-phase")`)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
| 1 | Set phase mode: `emu_set_phase("3-phase")` | Phase mode confirmed |
| 2 | Start charging cycle: `emu_plug()`, `emu_start()` | Charging |
| 3 | Wait for MeterValues (≥1 interval) | Values reported |
| 4 | Verify `emu_state()["phase_mode"]` | `"3-phase"` |
| 5 | Subscribe to MQTT session topic | MeterValues received |
| 6 | Verify L1, L2, L3 currents all > 0 A | Balanced load |
| 7 | Verify L1, L2, L3 voltages ≈ 230 V (±5%) | Grid voltage |
| 8 | Verify MQTT `power_w` ≈ 3 × 230 × current × PF | Power calculation correct |
| 9 | Stop charging: `emu_stop()`, `emu_unplug()` | Clean stop |

**Pass Criteria:** All three phases report non-zero current and ~230V voltage. MQTT power matches 3-phase calculation.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_meter_3phase -v`

---

#### WB-103: Per-Phase MeterValues (1-Phase)

**Precondition:**
- Emulator connected, charging active
- Phase mode: 1-phase (`emu_set_phase("1-phase")`)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
| 1 | Set phase mode: `emu_set_phase("1-phase")` | Phase mode confirmed |
| 2 | Start charging cycle: `emu_plug()`, `emu_start()` | Charging |
| 3 | Wait for MeterValues (≥1 interval) | Values reported |
| 4 | Verify `emu_state()["phase_mode"]` | `"1-phase"` |
| 5 | Subscribe to MQTT session topic | MeterValues received |
| 6 | Verify L1 current > 0 A | Active phase |
| 7 | Verify L2 current = 0 A, L3 current = 0 A | Inactive phases |
| 8 | Verify L2 voltage = 0 V, L3 voltage = 0 V | No voltage on inactive phases |
| 9 | Verify MQTT `power_w` reflects 1-phase power (DUT applies ÷3 correction if needed) | Correct power |
| 10 | Stop charging: `emu_stop()`, `emu_unplug()` | Clean stop |

**Pass Criteria:** Only L1 reports current; L2/L3 report 0 A and 0 V. MQTT power reflects 1-phase operation.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_meter_1phase -v`

---

#### WB-104: Authorization Flow

**Precondition:**
- Emulator connected, status Available
- Two sub-tests: authorize_required=true and authorize_required=false

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify `connector_status: Available` | WALLBOX_READY, Available |
| 1 | Enable authorization: `emu_set_authorize(True)` | `authorize_required: true` |
| 2 | Plug in: `emu_plug()` | Preparing |
| 3 | Start charging: `emu_start(id_tag="AUTH_TEST")` | Transaction begins |
| 4 | Verify emulator logs contain `Authorize` request | Authorize sent before StartTransaction |
| 5 | Verify Authorize response: `Accepted` | Authorization granted |
| 6 | Verify `connector_status: Charging` | Charging started |
| 7 | Stop and unplug: `emu_stop()`, `emu_unplug()` | Clean stop |
| 8 | Disable authorization: `emu_set_authorize(False)` | `authorize_required: false` |
| 9 | Plug in and start: `emu_plug()`, `emu_start()` | Transaction begins |
| 10 | Verify emulator logs do NOT contain `Authorize` request | Authorize skipped |
| 11 | Verify `connector_status: Charging` | Charging started directly |
| 12 | Stop and unplug: `emu_stop()`, `emu_unplug()` | Clean stop |

**Pass Criteria:** With authorize_required=true, Authorize is sent before StartTransaction. With authorize_required=false, Authorize is skipped.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_authorization_flow -v`

---

#### WB-105: SetChargingProfile Applied

**Precondition:**
- Emulator connected, charging active
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
| 1 | Start charging: `emu_plug()`, `emu_start()` | Charging |
| 2 | Record initial limit: `emu_state()["current_limit_a"]` | Baseline |
| 3 | Publish MQTT limit: `{"power_w": 5500}` to `command/limit` | Command sent |
| 4 | Wait 5 seconds | DUT processes and sends SetChargingProfile |
| 5 | Verify `emu_state()["current_limit_a"]` | ≈ 24 A (5500 / 230) |
| 6 | Publish MQTT limit: `{"power_w": 3680}` to `command/limit` | New limit |
| 7 | Wait 5 seconds | Profile updated |
| 8 | Verify `emu_state()["current_limit_a"]` | ≈ 16 A (3680 / 230) |
| 9 | Stop and unplug: `emu_stop()`, `emu_unplug()` | Clean stop |

**Pass Criteria:** MQTT power limit commands translate to SetChargingProfile, and the emulator's `current_limit_a` reflects the new value.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_charging_profile -v`

---

#### WB-106: SuspendedEVSE on 0A Profile

**Precondition:**
- Emulator connected, charging active
- MQTT connected

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
| 1 | Start charging: `emu_plug()`, `emu_start()` | Charging |
| 2 | Verify `connector_status: Charging` | Baseline |
| 3 | Publish MQTT limit: `{"power_w": 0}` to `command/limit` | Zero limit |
| 4 | Wait 5 seconds | DUT sends SetChargingProfile with 0A |
| 5 | Verify `emu_state()["current_limit_a"]` | 0.0 A |
| 6 | Verify `emu_state()["power_w"]` | 0 W |
| 7 | Verify `emu_state()["connector_status"]` | `SuspendedEVSE` |
| 8 | Verify `emu_state()["transaction_id"]` is not None | Transaction still active |
| 9 | Publish MQTT limit: `{"power_w": 7360}` to `command/limit` | Resume |
| 10 | Wait 5 seconds | Profile updated |
| 11 | Verify `emu_state()["connector_status"]` | `Charging` (resumed) |
| 12 | Verify `emu_state()["power_w"] > 0` | Power flowing again |
| 13 | Stop and unplug: `emu_stop()`, `emu_unplug()` | Clean stop |

**Pass Criteria:** Zero power limit suspends EVSE without stopping transaction. Non-zero limit resumes charging.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_suspended_evse -v`

---

#### WB-107: Energy Meter Continuity

**Precondition:**
- Emulator connected
- Phase switching capability

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
| 1 | Set 3-phase: `emu_set_phase("3-phase")` | 3-phase mode |
| 2 | Start charging: `emu_plug()`, `emu_start()` | Charging |
| 3 | Wait 15 seconds | Energy accumulates |
| 4 | Record: `e1 = emu_state()["energy_wh"]` | Checkpoint 1 |
| 5 | Switch to 1-phase: `emu_stop()`, `emu_set_phase("1-phase")`, `emu_start()` | Phase switch |
| 6 | Wait 15 seconds | Energy continues accumulating |
| 7 | Record: `e2 = emu_state()["energy_wh"]` | Checkpoint 2 |
| 8 | Verify `e2 > e1` | Monotonically increasing |
| 9 | Switch back to 3-phase: `emu_stop()`, `emu_set_phase("3-phase")`, `emu_start()` | Phase switch |
| 10 | Wait 15 seconds | Energy continues |
| 11 | Record: `e3 = emu_state()["energy_wh"]` | Checkpoint 3 |
| 12 | Verify `e3 > e2 > e1` | Continuity maintained |
| 13 | Stop and unplug: `emu_stop()`, `emu_unplug()` | Clean stop |

**Pass Criteria:** Energy counter increases monotonically across phase switches. No resets or gaps.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_energy_continuity -v`

---

#### WB-108: Emulator Auto-Reconnection

**Precondition:**
- Emulator connected and running
- DUT accessible for reset

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
| 1 | Verify connected: `assert emu_state()["connected"]` | Baseline |
| 2 | Reset DUT: `wt.gpio_set(17, 0)`, sleep, `wt.gpio_set(17, "z")` | DUT reboots |
| 3 | Wait for emulator to detect disconnect | `connected: false` within 10s |
| 4 | Wait for DUT to boot and OCPP server to start | ~10s |
| 5 | Wait for reconnect: `wait_emu_connected(timeout=30)` | `connected: true` |
| 6 | Verify fresh BootNotification in logs | New `← BootNotification` entry |
| 7 | Verify MQTT status restored | `ocpp/TEST001/status` shows `connected: true` |
| 8 | Start a new charging cycle | Full functionality |

**Pass Criteria:** Emulator automatically reconnects after DUT reset, sends fresh BootNotification, and resumes normal operation.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_auto_reconnect -v`

---

#### WB-109: Multiple Wallbox Connections

**Precondition:**
- Emulator #1 connected as TEST001
- Second emulator instance available

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` — verify emulator #1 connected | WALLBOX_READY |
| 1 | Verify emulator #1 connected | `connected: true` |
| 2 | Start emulator #2 on port 8081: `--web-port 8081 --config config/test002.yaml` | Process starts |
| 3 | Emulator #2 connects to `ws://{DUT}:8887/ocpp/TEST002` | Connection attempt |
| 4 | Verify DUT behavior | DUT accepts only 1 WebSocket client (`s_ws_fd` is single-slot) |
| 5 | Verify emulator #1 status | Either still connected or disconnected (DUT may close old connection) |
| 6 | Verify exactly one emulator is connected | Only one `connected: true` |
| 7 | Stop emulator #2 | Process exits |
| 8 | Verify emulator #1 reconnects if it was disconnected | `connected: true` |

**Pass Criteria:** Documents DUT single-client limitation. Second connection either replaces the first or is rejected. No DUT crash.

**Automation:** `pytest tests/test_wallbox_emulator.py::test_multi_client -v`

---

## 7. OTA Update Tests

Validates over-the-air firmware update functionality.

#### OTA-100: Firmware Upload via Portal

**Precondition:**
- DUT accessible via WiFi or Ethernet
- New firmware built: `ocpp-esp32.bin` exists
- Current version recorded: `V_before`

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0a | `check_test_mode_mqtt(wt, SLOT)` | TEST_MODE_MQTT |
| 0b | Verify firmware file: `os.path.isfile("ocpp-esp32/build/ocpp-esp32.bin")` | File present |
| 0c | Record current version `V_before` from serial or MQTT | Version captured |
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
| 0 | `check_test_mode_mqtt(wt, SLOT)` + record `V_before` | TEST_MODE_MQTT, version captured |
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
| 0 | `check_basic_dut()` + record `V_before` + verify bad firmware file exists | DUT reachable, files ready |
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
| 0 | `check_wallbox_ready(wt, SLOT)` + verify active transaction + firmware file exists | WALLBOX_READY, transaction active, OTA file ready |
| 1 | Start OTA upload | Upload begins |
| 2 | Verify warning displayed | "Active transaction will be stopped" |
| 3 | Confirm update | Transaction stopped first |
| 4 | Wait for Available status | Charging stopped gracefully |
| 5 | OTA proceeds | Upload and apply |
| 6 | After reboot | DUT operational, no orphan transaction |

**Pass Criteria:** Active transaction stopped cleanly before OTA.

**Automation:** `pytest tests/test_ota.py::test_ota_during_transaction -v`

---

## 8. Edge Case Tests

Tests for error handling, boundary conditions, and recovery from unexpected inputs.

#### EC-100: WebSocket Disconnect During Charging

**Precondition:**
- Active charging transaction
- Wallbox connected via WebSocket
- MQTT publishing meter values

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify active transaction | WALLBOX_READY, Charging |
| 1 | Disconnect wallbox (close WebSocket) | Connection lost |
| 2 | Verify MQTT status | `connected: false` published |
| 3 | Wait 10 seconds | DUT in disconnected state |
| 4 | Reconnect wallbox | WebSocket re-established |
| 5 | Wallbox sends BootNotification | Re-registration |
| 6 | Session state inquiry | Transaction can be resumed or properly closed |

**Pass Criteria:** Automatic recovery, MQTT reflects connection state.

> **Emulator note:** The emulator's auto-reconnect behavior (configurable via `reconnect_delay_sec`) makes disconnect/reconnect observable via `emu_state()["connected"]`. See WB-108 for dedicated reconnection testing.

**Automation:** `pytest tests/test_errors.py::test_websocket_disconnect_charging -v`

---

#### EC-101: WiFi Disconnect During Charging

**Precondition:**
- Active charging transaction
- MQTT connected and publishing
- OCPP via Ethernet operating

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + verify MQTT publishing + active transaction | WALLBOX_READY, MQTT active, Charging |
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
| 0 | `check_phase_ready(wt, SLOT)` + verify active transaction | PHASE_READY, Charging |
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
| 0 | `check_wallbox_ready(wt, SLOT)` + verify Charging at ~11 kW | WALLBOX_READY, Charging |
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
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
| 1 | Send invalid JSON | `CallError: FormationViolation` |
| 2 | Send valid JSON, missing required fields | `CallError: PropertyConstraintViolation` |
| 3 | Send unknown action | `CallError: NotImplemented` |
| 4 | Verify DUT still operational | Heartbeat works |
| 5 | Check error count | `E_before + 3` |

**Pass Criteria:** Graceful error handling, no crash, proper error responses.

**Automation:** `pytest tests/test_errors.py::test_malformed_ocpp -v`

---

#### EC-107: Maximum Message Size

**Precondition:**
- Wallbox connected
- Message size limit: 4KB

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
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
| 0 | `check_wallbox_ready(wt, SLOT)` | WALLBOX_READY |
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
| 0 | `check_phase_ready(wt, SLOT)` + verify active transaction | PHASE_READY, Charging |
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
| 0 | `check_test_mode_mqtt(wt, SLOT)` | TEST_MODE_MQTT |
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
| 0 | `check_test_mode_mqtt(wt, SLOT)` + record `IP_before` | TEST_MODE_MQTT, IP captured |
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
| 0 | `check_basic_dut()` + verify serial shows health checks | DUT running, WDT active |
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
| 0 | `check_basic_dut()` + verify serial shows "Hardware WDT initialized" | DUT running, HW WDT active |
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
| 0 | `check_test_mode_mqtt(wt, SLOT)` + verify WDT active via serial | TEST_MODE_MQTT, WDT running |
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
| 0 | `check_basic_dut()` + record `H_before` from serial heap command | DUT running, heap > 100KB |
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
| Malformed input | EC-104, MR-100 | Invalid JSON, wrong types |
| Oversized input | EC-107 | Exceeds 4KB message limit |
| Empty input | MR-100 | Empty MQTT commands |
| Concurrent operations | EC-108 | Parallel OCPP + MQTT |
| Disconnect/reconnect | EC-100, EC-101, MR-101 | Network drops |
| Rapid-fire | EC-103 | Burst of commands |
| Timeout | EC-102 | Phase switch timeout |
| Resource exhaustion | EC-115 | Memory pressure |
| Power loss | EC-109 | Power cycle mid-operation |

---

## 9. Long Duration / Stress Tests

Stability and endurance tests run over extended periods.

#### LD-001: 24-Hour Continuous Charging

**Precondition:**
- DUT running in normal mode
- Wallbox emulator connected
- MQTT connected
- Baseline metrics: record heap, uptime

| Step | Action | Expected Result |
|------|--------|-----------------|
| 0 | `check_wallbox_ready(wt, SLOT)` + record baseline heap and uptime | WALLBOX_READY, metrics captured |
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
| 0 | `check_wallbox_ready(wt, SLOT)` + verify no active transaction | WALLBOX_READY, idle |
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
| 0 | `check_wallbox_ready(wt, SLOT)` + record baseline reset count | WALLBOX_READY, baseline captured |
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
| 0 | `check_phase_ready(wt, SLOT)` + verify 3-phase baseline | PHASE_READY, 3-phase |
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

## 10. Test Commands Reference

### Setup Commands

```bash
# Build firmware
cd ocpp-esp32
source /opt/esp-idf/export.sh
idf.py build
```

### Flashing (WT32-ETH01 — no DTR/CTS, GPIO boot sequencing required)

The WT32-ETH01 has no DTR/CTS pin breakout. Use `WiFiTesterDriver` for all
GPIO operations — never raw curl.

```python
from wifi_tester_driver import WiFiTesterDriver
import time

wt = WiFiTesterDriver("http://192.168.0.87:8080")
devices = wt.get_devices()
dut = next(s for s in devices if s["present"])
PORT = dut["url"]  # e.g. "rfc2217://192.168.0.87:4003"

# 1. Enter bootloader mode
wt.gpio_set(18, 0)       # Hold GPIO 0 LOW (download mode)
wt.gpio_set(17, 0)       # EN LOW (reset)
time.sleep(0.2)
wt.gpio_set(17, "z")     # Release EN — DUT enters bootloader
```

```bash
# 2. Flash (--before=no_reset because we entered bootloader via GPIO)
esptool.py --chip esp32 \
  --port "${PORT}?ign_set_control" \
  --before=no_reset --after=hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0xe000  build/ota_data_initial.bin \
  0x10000 build/ocpp-esp32.bin
```

```python
# 3. Release GPIO 0 (boot pin)
wt.gpio_set(18, "z")

# Erase NVS (repeat step 1 to enter bootloader first, then):
#   esptool.py ... erase_region 0x9000 0x5000

# Reset DUT (pulse EN LOW→release)
wt.gpio_set(17, 0)
time.sleep(0.2)
wt.gpio_set(17, "z")
```

### Serial Monitoring

```python
# Via WiFiTesterDriver (preferred)
result = wt.serial_monitor(SLOT, pattern="OCPP ESP32 Server ready", timeout=15)
print(result["output"])
```

```python
# Direct pyserial via RFC2217 (fallback only)
import serial, time
ser = serial.serial_for_url(f'{PORT}?ign_set_control', do_not_open=True)
ser.baudrate = 115200; ser.timeout = 1; ser.dtr = False; ser.rts = False
ser.open()
deadline = time.time() + 15
while time.time() < deadline:
    line = ser.readline()
    if line: print(line.decode('utf-8', errors='replace').rstrip())
ser.close()
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
## Pi broker (most tests — DUT reaches via WiFi AP at 192.168.4.1)
mosquitto_sub -h 192.168.0.87 -t "ocpp/#" -v

# Publish start command
mosquitto_pub -h 192.168.0.87 -t "ocpp/TEST001/command/start" -m '{"id_tag":"TEST"}'

# Publish stop command
mosquitto_pub -h 192.168.0.87 -t "ocpp/TEST001/command/stop" -m '{}'

# Publish power limit
mosquitto_pub -h 192.168.0.87 -t "ocpp/TEST001/command/limit" -m '{"power_w":5500}'

## Home LAN broker (production-mode test MT-100 only)
# mosquitto_sub -h 192.168.0.203 -t "ocpp/#" -v
```

### Wallbox Emulator Commands

```bash
# Start emulator (default config)
cd ocpp-test-wallbox
python -m ocpp_test_wallbox.main run

# Start with custom config
python -m ocpp_test_wallbox.main run --config config/test.yaml

# Start second instance on alternate port (for WB-109)
python -m ocpp_test_wallbox.main run --config config/test002.yaml --web-port 8081
```

```bash
# API: Check emulator state
curl -s http://localhost:8080/api/state | python -m json.tool

# API: Simulate plug-in
curl -s -X POST http://localhost:8080/api/plug

# API: Start charging
curl -s -X POST http://localhost:8080/api/start -H 'Content-Type: application/json' -d '{"id_tag":"TEST"}'

# API: Stop charging
curl -s -X POST http://localhost:8080/api/stop

# API: Unplug
curl -s -X POST http://localhost:8080/api/unplug

# API: Set phase mode
curl -s -X POST http://localhost:8080/api/phase -H 'Content-Type: application/json' -d '{"mode":"1-phase"}'

# API: Toggle authorization
curl -s -X POST http://localhost:8080/api/authorize -H 'Content-Type: application/json' -d '{"enabled":true}'
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

## 11. Test Classification & Execution Sequence

### Execution Phases

| Phase | Category | Tests | Requires Human | Requires DUT | Duration | Transition Procedure |
|-------|----------|------:|:--------------:|:------------:|----------|---------------------|
| 1 | Setup | 2 | No | Yes | 5 min | — (fresh flash) |
| 2 | Captive Portal | 4 | No | Yes | 15 min | — (NVS empty → AP) |
| 3 | MQTT Transport | 4 | No | Yes | 10 min | §2.11: Phase 2→3a, 3a→3b, 3b→3d, 3d→4 |
| 4 | MQTT Resilience | 2 | No | Yes | 5 min | §2.11: Phase 3d→4 (restore test mode) |
| 5 | Connection | 5 | No | Yes | 10 min | §2.11: Phase 4→5 (start emulator) |
| 6 | Charging | 4 | No | Yes | 15 min | — (emulator already connected) |
| 7 | Remote Commands | 4 | No | Yes | 10 min | — |
| 8 | Phase Switching | 5 | No | Yes | 20 min | §2.11: Phase 7→8 (init BCM 22) |
| 9 | Wallbox Emulator | 10 | No | Yes | 20 min | — |
| 10 | OTA Update | 4 | 1 test | Yes | 15 min | — |
| 11 | Edge Cases | 14 | 4 tests | Yes | 40 min | — |
| 12 | Long Duration | 4 | No | Yes | 7+ days | — |

### Manual-Only Tests

These tests require human interaction and cannot be fully automated:

| Test ID | Reason |
|---------|--------|
| EC-109 | Requires power cycle |
| EC-110 | Requires RF environment control |
| EC-112 | Requires special test firmware |
| EC-113 | Code review verification |
| EC-115 | Requires special test firmware |
| OTA-102 | Requires intentionally crashing firmware |

**Note:** TC-133 (voltage mismatch) is automated by keeping the wallbox emulator in the wrong phase mode. CP-100 (captive portal entry) is automated via BCM 27 → GPIO 14 wiring.

### GPIO Wiring for Automation

Wire Serial Portal Pi GPIOs to DUT pins:

| Pi GPIO (BCM) | DUT Pin | Direction | Function | Active Level | DUT Pull | Notes |
|---------------|---------|-----------|----------|-------------|----------|-------|
| 17 | EN/RESET | Pi → DUT | Reset chip | LOW | External pullup | Boot sequencing |
| 18 | GPIO 0 | Pi → DUT | Boot mode select | LOW | PULLUP | Download mode (strapping pin) |
| 27 | GPIO 14 | Pi → DUT | Config button | LOW | PULLUP | Captive portal trigger |
| 22 | GPIO 4 | DUT → Pi | Relay state readback | — | — | LOW=1-phase, HIGH=3-phase |

**Flash firmware (no DTR/CTS — discover PORT from `wt.get_devices()`):**
```python
import time
wt.gpio_set(18, 0)      # Hold GPIO 0 LOW (download mode)
wt.gpio_set(17, 0)      # EN LOW (reset)
time.sleep(0.2)
wt.gpio_set(17, "z")    # Release EN — DUT enters bootloader
# Flash via esptool with --before=no_reset --after=hard_reset:
#   esptool.py --chip esp32 --port "${PORT}?ign_set_control" \
#     --before=no_reset --after=hard_reset write_flash ...
wt.gpio_set(18, "z")    # Release GPIO 0 after flash completes
```

**Trigger captive portal (no reset needed):**
```python
wt.gpio_set(27, 0)      # Hold GPIO 14 LOW
time.sleep(5.5)          # Wait > 5 seconds
wt.gpio_set(27, "z")    # Release
# DUT enters AP mode without rebooting
```

---

## 12. Automated Test Coverage

### Test Files

| Test File | Tests | Source Under Test | Framework |
|-----------|------:|-------------------|-----------|
| `tests/test_setup.py` | 2 | Flash, provision, clean state | pytest-asyncio |
| `tests/test_captive_portal.py` | 4 | Captive portal | pytest-asyncio |
| `tests/test_mqtt_transport.py` | 4 | MQTT transport mode | pytest-asyncio |
| `tests/test_mqtt_resilience.py` | 2 | MQTT error handling and recovery | pytest-asyncio |
| `tests/test_connection.py` | 5 | OCPP WebSocket handling | pytest-asyncio |
| `tests/test_charging.py` | 4 | Transaction management | pytest-asyncio |
| `tests/test_remote.py` | 4 | MQTT command handling | pytest-asyncio |
| `tests/test_phase.py` | 5 | Phase switching logic | pytest-asyncio |
| `tests/test_power_profiles.py` | 3 | SetChargingProfile | pytest-asyncio |
| `tests/test_metering.py` | 2 | MeterValues processing | pytest-asyncio |
| `tests/test_wallbox_emulator.py` | 10 | Wallbox emulator integration | pytest-asyncio |
| `tests/test_ota.py` | 3 | OTA update | pytest-asyncio |
| `tests/test_errors.py` | 10 | Error handling | pytest-asyncio |
| `tests/test_long_duration.py` | 4 | Stability and endurance | pytest-asyncio |
| **Total** | **62** | | |

### Coverage Gaps

These areas are tested manually only (no automated tests yet):

- **Physical button interactions**: Requires GPIO wiring to automate
- **Power cycle recovery**: Requires controllable power supply
- **RF signal degradation**: Requires RF environment control
- **Watchdog with task hang**: Requires special test firmware

---

## 13. Test Report Template

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

CAPTIVE PORTAL
  CP-100  Enter Portal Mode          [ PASS / FAIL / SKIP ]
  CP-101  WiFi Provisioning          [ PASS / FAIL / SKIP ]
  CP-102  MQTT Configuration         [ PASS / FAIL / SKIP ]
  CP-103  DNS Redirect               [ PASS / FAIL / SKIP ]

MQTT TRANSPORT
  MT-100  Production Mode (ETH)      [ PASS / FAIL / SKIP ]
  MT-101  Test Mode (WiFi)           [ PASS / FAIL / SKIP ]
  MT-102  Switch Prod→Test           [ PASS / FAIL / SKIP ]
  MT-103  Test Mode No SSID          [ PASS / FAIL / SKIP ]

MQTT RESILIENCE (Pi WiFi AP — no wallbox needed)
  MR-100  Malformed MQTT Command     [ PASS / FAIL / SKIP ]
  MR-101  MQTT Broker Restart        [ PASS / FAIL / SKIP ]

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

WALLBOX EMULATOR
  WB-100  Emulator Boot & Connection [ PASS / FAIL / SKIP ]
  WB-101  Charging Cycle (API)       [ PASS / FAIL / SKIP ]
  WB-102  MeterValues 3-Phase        [ PASS / FAIL / SKIP ]
  WB-103  MeterValues 1-Phase        [ PASS / FAIL / SKIP ]
  WB-104  Authorization Flow         [ PASS / FAIL / SKIP ]
  WB-105  SetChargingProfile         [ PASS / FAIL / SKIP ]
  WB-106  SuspendedEVSE (0A)         [ PASS / FAIL / SKIP ]
  WB-107  Energy Meter Continuity    [ PASS / FAIL / SKIP ]
  WB-108  Auto-Reconnection          [ PASS / FAIL / SKIP ]
  WB-109  Multi-Client Limitation    [ PASS / FAIL / SKIP ]

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
Summary: ___/62 passed, ___ failed, ___ skipped
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
| 1.1 | 2026-02-09 | Claude | Added GPIO boot sequencing for WT32-ETH01 (no DTR/CTS); updated flash/erase commands to use SLOT3 (port 4003) with --before=no_reset; added MQTT Transport tests (MT-100–MT-103); renumbered sections |
| 1.2 | 2026-02-09 | Claude | Phase switching tests: added relay readback via Pi BCM 22, wallbox emulator phase sync, architecture diagram; TC-130–TC-134 fully automated (no manual tests); TC-133 simulates stuck relay via emulator mismatch |
| 1.3 | 2026-02-10 | Claude | Added wallbox emulator integration tests (WB-100 to WB-109), emulator setup instructions (Section 2.8), emulator HTTP API reference, Python helper functions, updated existing tests with emulator observation notes, added emulator commands to Section 10 |
| 1.4 | 2026-02-10 | Claude | Moved MQTT resilience tests (EC-105→MR-100, EC-106→MR-101) to Phase 4 before wallbox connection; runs on Pi WiFi AP for network isolation from home LAN |
| 1.5 | 2026-02-10 | Claude | Added prerequisite verification: DUT state matrix (§2.9), precondition check helpers (§2.10), phase transition procedures (§2.11); added Step 0 to all 62 test cases; replaced `wt.ssh_exec()` in MR-101 with `subprocess.run(["ssh", ...])` ; added automation notes to MT-101/MT-102 for `config_set` workaround; added Transition Procedure column to execution phases table |
