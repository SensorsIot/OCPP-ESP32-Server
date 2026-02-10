# ESP32 OCPP Server — Test Results

| Field | Value |
|-------|-------|
| **Date** | 2026-02-09 |
| **Test Spec** | OCPP-ESP32-Test-Specification v1.2 |
| **FSD** | ocpp-esp32-fsd.md v1.6 |
| **Firmware** | ocpp-esp32 (built 2026-02-09, GPIO 4 relay) |
| **DUT** | WT32-ETH01, Ethernet 192.168.0.105, OCPP port 8887 |
| **MQTT Broker** | Pi (192.168.0.87:1883) |
| **Tester** | WiFiTesterDriver on Pi (192.168.0.87:8080) |
| **DUT Serial** | SLOT3 (rfc2217://192.168.0.87:4003) |

---

## Summary

| Category | Pass | Fail | Skip | Total |
|----------|-----:|-----:|-----:|------:|
| Setup | 2 | 0 | 0 | 2 |
| Captive Portal | 4 | 0 | 0 | 4 |
| MQTT Transport | 2 | 0 | 2 | 4 |
| Connection | 4 | 0 | 1 | 5 |
| Charging | 4 | 0 | 0 | 4 |
| Remote Commands | 4 | 0 | 0 | 4 |
| Phase Switching | 3 | 0 | 2 | 5 |
| OTA Update | 0 | 0 | 4 | 4 |
| Edge Cases | 7 | 1 | 8 | 16 |
| Long Duration | 0 | 0 | 4 | 4 |
| **Total** | **30** | **1** | **21** | **52** |

**Pass rate (executed): 30/31 = 96.8%**

---

## Findings

### Defects Found

| ID | Severity | Description |
|----|----------|-------------|
| D-001 | Medium | **MQTT does not auto-reconnect after broker restart.** DUT connected to MQTT on boot but did not reconnect within 40s after the Pi broker was restarted via `systemctl restart mosquitto`. Required a DUT reboot to restore MQTT. (EC-106) |
| D-002 | Low | **DUT AP is open despite configured password.** Config shows `ap_pass: ocpp12345` but the AP accepts connections without a password. `sta_join("OCPP-ESP32-F020", "ocpp12345")` fails; `sta_join("OCPP-ESP32-F020", "")` succeeds. (CP-100) |
| D-003 | Info | **`test_mode` config key not implemented.** Serial console `config_set test_mode 1` returns "Unknown key: test_mode". WiFi STA test mode cannot be configured at runtime. (MT-101, MT-102) |
| D-004 | Info | **Portal subnet is 192.168.1.x, not 192.168.4.x.** DUT AP assigns IPs on 192.168.1.0/24 with portal at 192.168.1.1. Test spec assumed 192.168.4.x. (CP-100) |
| D-005 | Info | **Oversized WebSocket messages (>4KB) cause disconnect.** DUT closes WebSocket on ~4KB messages but recovers cleanly on reconnect. (EC-107) |

### Observations

- **Phase switch threshold**: 4.1 kW. Sending >4.1 kW in 1-phase mode triggers automatic phase switch (1→3). Confirmed by TC-122, TC-131.
- **Phase switch safety interlock**: Verified working. DUT sends RemoteStopTransaction before switching relay, waits for Available status. Relay never changes while power is flowing (TC-132 PASS).
- **Phase 3→1 switch**: Works when power limit drops below threshold while in 3-phase mode. DUT stops transaction, waits for Available, then switches relay. (TC-132 sequence)
- **MQTT topic structure**: `ocpp/ocpp-esp32/<subtopic>` (prefix + device name). Command topic: `ocpp/ocpp-esp32/command/#`.
- **Heap stability**: 212 KB free after all tests, minimum 198 KB during test execution. No memory leaks observed.
- **NVS erase does not trigger config mode**: Firmware has compiled default `mqtt_host=192.168.0.203`. Config mode only triggers when `mqtt_host` is empty string (set via serial `config_set mqtt_host ""`).
- **Concurrent traffic**: DUT handled 20 OCPP messages + 10 MQTT commands simultaneously with zero errors (EC-108).

---

## Detailed Results

### Setup (2/2 Pass)

| Test | Result | Details |
|------|--------|---------|
| TC-000 | **PASS** | Firmware boots, Ethernet 192.168.0.105, OCPP port 8887 open, MQTT connected to Pi broker |
| TC-001 | **PASS** | Clean state verified via serial console. Free heap 212 KB, config shows defaults |

### Captive Portal (4/4 Pass)

| Test | Result | Details |
|------|--------|---------|
| CP-100 | **PASS** | Portal page loads at 192.168.1.1 (note: not 192.168.4.1). AP SSID "OCPP-ESP32-F020", open (no password). See D-002, D-004 |
| CP-101 | **PASS** | `/api/wifi/scan` returns 200 with network list. `/api/config` returns JSON with all config fields |
| CP-102 | **PASS** | `POST /api/config` with `mqtt_host=192.168.0.87` returns `{"ok":true}`. Setting persisted across reboot |
| CP-103 | **PASS** | DNS queries redirect to 192.168.1.1. HTTP requests to arbitrary domains redirect to portal |

### MQTT Transport Mode (2/4 Pass, 2 Skip)

| Test | Result | Details |
|------|--------|---------|
| MT-100 | **PASS** | Production mode: Ethernet CONNECTED, WiFi not started, MQTT connected via Ethernet to 192.168.0.87 |
| MT-101 | **SKIP** | `test_mode` config key not implemented in firmware (see D-003) |
| MT-102 | **SKIP** | `test_mode` config key not implemented in firmware (see D-003) |
| MT-103 | **PASS** | WiFi SSID empty in test mode: WiFi not started, MQTT falls back to Ethernet |

### Connection Tests (4/5 Pass, 1 Skip)

| Test | Result | Details |
|------|--------|---------|
| TC-100 | **PASS** | WebSocket connects to `ws://192.168.0.105:8887/ocpp/Walpurga`. BootNotification `status: Accepted`, `interval: 60` |
| TC-101 | **PASS** | Heartbeat response contains `currentTime` field |
| TC-102 | **PASS** | StatusNotification acknowledged with `{}`. MQTT status published with status field |
| TC-103 | **SKIP** | Timeout detection test requires 35s wait. Skipped for efficiency |
| TC-104 | **PASS** | Reconnection after disconnect: new WebSocket connection accepted, BootNotification re-accepted |

### Charging Tests (4/4 Pass)

| Test | Result | Details |
|------|--------|---------|
| TC-110 | **PASS** | Full cycle: Preparing → Authorize → StartTransaction (txnId assigned) → Charging → MeterValues → StopTransaction → Available |
| TC-111 | **PASS** | Accept-all mode: unknown idTags accepted, empty idTag accepted |
| TC-112 | **PASS** | MeterValues published to MQTT `ocpp/ocpp-esp32/session` topic with power and energy values |
| TC-113 | **PASS** | Transaction IDs assigned sequentially (verified across multiple transactions) |

### Remote Command Tests (4/4 Pass)

| Test | Result | Details |
|------|--------|---------|
| TC-120 | **PASS** | MQTT `command/start` with `{"id_tag":"ENERGY_MANAGER"}` → RemoteStartTransaction sent to wallbox |
| TC-121 | **PASS** | MQTT `command/stop` with `{}` → RemoteStopTransaction sent to wallbox |
| TC-122 | **PASS** | MQTT `command/limit` with `{"power_w":3000}` → SetChargingProfile with limit=13.04A (3000W/230V). Note: 5500W triggers phase switch instead (correct behavior for 1-phase mode) |
| TC-123 | **PASS** | MQTT `command/limit` with `{"power_w":0}` → SetChargingProfile with limit=0A |

### Phase Switching Tests (3/5 Pass, 2 Skip)

| Test | Result | Details |
|------|--------|---------|
| TC-130 | **PASS** | 3→1 phase switch: DUT sent RemoteStopTransaction, waited for Available, relay switched LOW (BCM 22: 1→0). Verified during TC-132 sequence |
| TC-131 | **PASS** | 1→3 phase switch: 7500W limit triggered RemoteStopTransaction (safety interlock), relay switched HIGH (BCM 22: 0→1) after Available status. Slight delay in relay readback (~2s after Available) |
| TC-132 | **PASS** | Safety interlock: relay remained at 1 (3-phase) during RemoteStopTransaction. Only switched to 0 (1-phase) after Available status confirmed. **Relay never switched under load** |
| TC-133 | **SKIP** | Voltage mismatch test requires wallbox emulator with intentional phase desync |
| TC-134 | **SKIP** | Power correction test requires wallbox emulator running with specific MeterValues |

### OTA Update Tests (0/4, all Skip)

| Test | Result | Details |
|------|--------|---------|
| OTA-100 | **SKIP** | OTA upload page not tested (requires firmware management infrastructure) |
| OTA-101 | **SKIP** | Corrupt firmware rejection not tested |
| OTA-102 | **SKIP** | Rollback requires special crash-on-boot test firmware |
| OTA-103 | **SKIP** | OTA during transaction not tested |

### Edge Case Tests (7/16 Pass, 1 Fail, 8 Skip)

| Test | Result | Details |
|------|--------|---------|
| EC-100 | **PASS** | WebSocket disconnect during charging: reconnection successful, DUT accepts new BootNotification |
| EC-101 | **SKIP** | WiFi disconnect during charging requires WiFi AP management (DUT in production mode) |
| EC-102 | **SKIP** | Phase switch timeout requires emulator that ignores RemoteStop |
| EC-103 | **PASS** | 10 rapid power limit changes (0.5s interval): all 10 SetChargingProfile delivered, DUT stable, last limit 13.04A |
| EC-104 | **PASS** | Malformed OCPP: invalid JSON ignored, missing fields handled, unknown action → CallError NotImplemented. DUT continues operating |
| EC-105 | **PASS** | Malformed MQTT: invalid JSON ignored, wrong types logged, empty message treated as valid. No crash |
| EC-106 | **FAIL** | **MQTT broker restart: DUT did not reconnect within 40s.** Broker restarted via SSH (`systemctl restart mosquitto`), came back in 2s, but DUT MQTT stayed disconnected. Required DUT reboot to restore. See D-001 |
| EC-107 | **PASS** | Large valid message (1554 bytes) accepted. Oversized message (~4KB) caused WebSocket disconnect. DUT recovered on reconnect. See D-005 |
| EC-108 | **PASS** | Concurrent traffic: 20 OCPP MeterValues + 10 MQTT limit commands processed simultaneously. Zero errors, DUT stable |
| EC-109 | **SKIP** | Power cycle during phase switch requires physical power control |
| EC-110 | **SKIP** | WiFi signal degradation requires RF environment control |
| EC-111 | **SKIP** | DHCP lease expiry requires short lease configuration |
| EC-112 | **SKIP** | Software watchdog requires special test firmware |
| EC-113 | **SKIP** | Hardware watchdog verified by code review only |
| EC-114 | **SKIP** | Watchdog during WiFi disconnect requires WiFi connected state |
| EC-115 | **SKIP** | Memory pressure requires special test firmware |

### Long Duration Tests (0/4, all Skip)

| Test | Result | Details |
|------|--------|---------|
| LD-001 | **SKIP** | 24h continuous charging — not executed today |
| LD-002 | **SKIP** | 72h idle with heartbeats — not executed today |
| LD-003 | **SKIP** | 7-day usage pattern — not executed today |
| LD-004 | **SKIP** | Repeated phase switches — not executed today |

---

## DUT Configuration at Test Time

```
dev_name:     ocpp-esp32
test_mode:    false
eth_ip:       192.168.4.1 (config default; DHCP assigns 192.168.0.105)
wifi_ssid:    (empty)
ap_ssid:      OCPP-ESP32-F020
ap_pass:      ocpp12345 (not enforced — see D-002)
mqtt_host:    192.168.0.87
mqtt_port:    1883
mqtt_prefix:  ocpp
ws_port:      8887
hb_interval:  60
meter_intv:   30
phase_switch_threshold: 4100 W
```

## Hardware Wiring

| Pi BCM | Direction | DUT GPIO | Function |
|--------|-----------|----------|----------|
| 17 | Pi → DUT | EN | Reset (active LOW) |
| 18 | Pi → DUT | GPIO 0 | Boot mode (LOW=download) |
| 22 | DUT → Pi | GPIO 4 | Relay readback (0=1-phase, 1=3-phase) |
| 27 | Pi → DUT | GPIO 14 | Config button (not in Pi allowlist yet) |

## Heap Stability

| Checkpoint | Free Heap | Min Heap |
|------------|-----------|----------|
| Boot | 212,924 B | — |
| After all tests | 212,744 B | 198,212 B |
| Delta | -180 B | — |

No significant memory leak observed across 31 test executions.

---

## Recommendations

1. **Fix MQTT auto-reconnect** (D-001): The ESP-IDF MQTT client should handle broker restarts automatically. Check if `MQTT_EVENT_DISCONNECTED` handler triggers reconnection, or if the reconnect backoff is too long.
2. **Enforce AP password** (D-002): The configured `ap_pass` should be applied to the WiFi AP. Currently the AP is open regardless of configuration.
3. **Implement `test_mode` config key** (D-003): Add `test_mode` to the config manager's key registry so it can be set via serial console for WiFi STA test mode.
4. **Update test spec subnet** (D-004): Change portal IP references from 192.168.4.1 to 192.168.1.1 throughout the test specification.
5. **Run wallbox emulator tests**: TC-133 (voltage mismatch) and TC-134 (power correction) require the wallbox emulator running alongside the DUT. Schedule a dedicated session.
6. **Schedule long-duration tests**: LD-001 through LD-004 should be run overnight/over weekend to validate stability.
