# WiFi Connection Test Cases - Functional Specification

**Extracted from**: ocpp-esp32-fsd.md
**Version**: 1.1
**Date**: 2026-02-05
**Status**: Reference Document

---

## 1. Overview

This document consolidates all WiFi-related requirements and test cases for the ESP32 OCPP Server. These test cases verify WiFi connectivity, captive portal configuration, reconnection handling, and edge cases during network disruptions.

### 1.1 WiFi Architecture

The ESP32 OCPP Server uses a dual-network architecture:
- **Ethernet (W5500)**: OCPP WebSocket communication with wallbox
- **WiFi (Built-in)**: MQTT communication with energy management system

```
┌─────────────┐                    ┌─────────────┐
│   Wallbox   │◄──────────────────│  ESP32 OCPP │◄─────│    WiFi     │────►│  MQTT Broker   │
│             │    Ethernet         │    Server   │       │   Router    │     │                │
└─────────────┘    (OCPP)          └─────────────┘       └─────────────┘     └────────────────┘
```

---

## 2. WiFi Requirements

### 2.1 WiFi Station Mode (MQTT/Internet Network)

| ID | Requirement | Priority |
|----|-------------|----------|
| WIFI-001 | System SHALL connect to configured WiFi network in STA mode | Must |
| WIFI-002 | WiFi credentials SHALL be stored encrypted in NVS | Must |
| WIFI-003 | System SHALL automatically reconnect on WiFi disconnect | Must |
| WIFI-004 | System SHALL log WiFi connection status changes | Should |
| WIFI-005 | System SHALL support WPA2/WPA3 authentication | Must |
| WIFI-006 | MQTT client SHALL only use WiFi interface | Must |
| WIFI-007 | System SHALL sync time via NTP over WiFi | Should |

### 2.2 WiFi Access Point Mode (Configuration)

| ID | Requirement | Priority |
|----|-------------|----------|
| AP-001 | System SHALL start AP mode when no valid WiFi config exists | Must |
| AP-002 | System SHALL start AP mode when BTN_CONFIG held for 5 seconds | Must |
| AP-003 | AP SHALL use SSID format: `OCPP-ESP32-{MAC_LAST_4}` | Should |
| AP-004 | AP SHALL use open authentication (no password) for easy initial setup | May |
| AP-005 | AP SHALL assign IP 192.168.1.1 to clients | Should |
| AP-006 | System MAY run AP and STA concurrently (fallback mode) | May |

### 2.3 Test Mode (WiFi + Ethernet)

| ID | Requirement | Priority |
|----|-------------|----------|
| TEST-001 | System SHALL support a "Test mode" configurable via captive portal or NVS | Should |
| TEST-002 | In Test mode, WebSocket server SHALL listen on ALL interfaces (ETH + WiFi) | Should |
| TEST-003 | In Test mode, wallbox emulator MAY connect via WiFi instead of Ethernet | May |
| TEST-004 | Test mode SHALL be indicated via serial log and MQTT status | Should |
| TEST-005 | Test mode allows full operation without Ethernet hardware connected | Should |

---

## 3. Functional Test Cases

### 3.1 TC-300: Captive Portal Configuration

**Objective**: Verify captive portal allows complete system configuration.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Hold CONFIG button 5 seconds | AP mode activates (logged to serial) |
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

### 3.2 TC-301: OTA Firmware Update via WiFi

**Objective**: Verify firmware can be updated through WiFi portal.

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

---

## 4. Edge Case Test Cases

### 4.1 EC-100: WebSocket Disconnect During Charging

**Objective**: Verify wallbox communication handles network interruption.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Active charging session | Meter values publishing |
| 2 | Disconnect Ethernet cable | WebSocket connection lost |
| 3 | Charging continues | Vehicle still charging |
| 4 | Wait 30 seconds | Reconnection attempts logged |
| 5 | Re-enable WiFi AP | WiFi reconnects |
| 6 | Wallbox reconnects | BootNotification exchanged |
| 7 | Session state restored | Charging continues |

**Pass Criteria**: Automatic recovery, no data loss.

### 4.2 EC-101: WiFi Disconnect During Charging

**Objective**: Verify MQTT/WiFi loss does not affect OCPP/Ethernet operation.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Charging session active | MQTT publishing |
| 2 | Disable WiFi AP | WiFi connection lost |
| 3 | OCPP continues | Wallbox communication OK |
| 4 | Messages queued | Buffer fills |
| 5 | Re-enable WiFi AP | WiFi reconnects |
| 6 | Queued messages sent | MQTT catches up |

**Pass Criteria**: OCPP unaffected, MQTT recovers automatically.

### 4.3 EC-110: WiFi Signal Strength Degradation

**Objective**: Verify system handles weak WiFi signal gracefully.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Normal operation | RSSI > -60 dBm |
| 2 | Increase distance to AP | RSSI decreases |
| 3 | Monitor at -70 dBm | Connection maintained |
| 4 | Monitor at -80 dBm | Possible packet loss |
| 5 | Monitor at -85 dBm | Reconnection attempts |
| 6 | Return to normal range | Connection stabilizes |

**Pass Criteria**: No crash, graceful degradation, automatic recovery.

### 4.4 EC-111: WiFi AP Channel Congestion

**Objective**: Verify system handles congested WiFi environment.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Connect to AP on channel 6 | Normal operation |
| 2 | Enable multiple interfering APs | Increased latency |
| 3 | Monitor MQTT message delivery | Messages delivered (slower) |
| 4 | Monitor reconnection behavior | May reconnect occasionally |
| 5 | Disable interfering APs | Performance returns to normal |

**Pass Criteria**: No data loss, eventual delivery of all messages.

### 4.5 EC-112: WiFi Credential Change While Running

**Objective**: Verify system handles WiFi password change.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System connected to WiFi | Normal operation |
| 2 | Change AP password | Connection lost |
| 3 | System attempts reconnect | Auth failures logged |
| 4 | After N failures | AP mode activates (fallback) |
| 5 | Reconfigure via portal | New credentials saved |
| 6 | System reconnects | Normal operation restored |

**Pass Criteria**: Fallback to AP mode, reconfiguration possible.

### 4.6 EC-113: Simultaneous AP and STA Mode

**Objective**: Verify concurrent AP/STA operation for recovery scenarios.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System in normal STA mode | Connected to home WiFi |
| 2 | Hold CONFIG button 3 sec | AP mode starts (STA continues) |
| 3 | Connect phone to AP | Can access portal |
| 4 | Verify STA still connected | MQTT still publishing |
| 5 | Release CONFIG button | AP mode timeout (60s) |
| 6 | AP deactivates | STA-only mode |

**Pass Criteria**: Both modes functional simultaneously, clean transition.

### 4.7 EC-114: WiFi Reconnect During OTA Update

**Objective**: Verify OTA handles WiFi interruption safely.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start OTA update | Download begins |
| 2 | At 50%, disconnect WiFi | Download paused |
| 3 | WiFi reconnects | Download resumes or restarts |
| 4 | Update completes | System reboots |
| 5 | Verify firmware | Correct version running |

**Pass Criteria**: No brick, either resume or clean restart of update.

### 4.8 EC-115: DHCP Lease Expiry

**Objective**: Verify system handles DHCP lease renewal.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Set short DHCP lease (60s) | System gets IP |
| 2 | Wait for lease expiry | Renewal attempt |
| 3 | Verify IP maintained | Same or new IP |
| 4 | Verify MQTT connection | Reconnects if IP changed |
| 5 | Normal operation continues | No user intervention needed |

**Pass Criteria**: Automatic lease renewal, connection recovery.

### 4.9 EC-116: Software Watchdog - Task Timeout Detection

**Objective**: Verify software watchdog detects hung tasks and recovers.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | All tasks healthy |
| 2 | Monitor serial log | Health checks every 5 seconds |
| 3 | Simulate task hang (test firmware) | Stop task heartbeat updates |
| 4 | Wait 60+ seconds | Software watchdog triggers |
| 5 | Check serial log | "Task timeout - triggering reboot" |
| 6 | System reboots automatically | ESP.restart() called |
| 7 | After reboot | Normal operation resumes |
| 8 | Verify WiFi reconnects | MQTT connection restored |

**Pass Criteria**: Hung task detected within 65s, automatic recovery via reboot, WiFi/MQTT restored.

**Note**: Requires test firmware with deliberate task hang capability, or verification via code review.

### 4.10 EC-117: Hardware Watchdog Recovery

**Objective**: Verify hardware watchdog provides failsafe recovery.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | Hardware WDT active |
| 2 | Check serial log at startup | "Hardware WDT initialized" message |
| 3 | Monitor normal operation | WDT fed every health check cycle |
| 4 | If watchdog task hangs | Hardware WDT triggers after timeout |
| 5 | System panic and reboot | Automatic hardware recovery |
| 6 | After reboot | WiFi and MQTT reconnect |

**Pass Criteria**: Hardware watchdog provides failsafe recovery if software watchdog itself fails.

**Note**: This is a safety-net test. In normal operation, software watchdog handles recovery before hardware WDT triggers.

### 4.11 EC-118: Watchdog Stability During WiFi Disconnect

**Objective**: Verify watchdog does not false-trigger during normal WiFi reconnection.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running, WiFi connected | Watchdog task active |
| 2 | Disable WiFi AP | Connection lost |
| 3 | Wait 5 minutes | Extended disconnect period |
| 4 | Monitor serial log | Health checks continue (every 5s) |
| 5 | No watchdog resets | System remains stable |
| 6 | Re-enable WiFi AP | Connection restored |
| 7 | MQTT reconnects | Normal operation resumes |

**Pass Criteria**: Watchdog operates independently of WiFi state, no false triggers during reconnection.

### 4.12 EC-119: Critical Memory Watchdog

**Objective**: Verify system reboots before memory exhaustion causes crash.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | Heap above threshold |
| 2 | Monitor heap via MQTT/serial | free_heap values logged |
| 3 | Simulate memory pressure | Allocate memory (test firmware) |
| 4 | Heap drops below warning threshold | "Low heap" warning logged |
| 5 | Heap drops below critical threshold | "Critical heap - triggering reboot" |
| 6 | System reboots automatically | Preventive recovery |
| 7 | After reboot | Memory recovered, WiFi reconnects |

**Pass Criteria**: Critical memory exhaustion triggers preventive reboot before crash.

### 4.13 EC-120: Watchdog During OTA Update

**Objective**: Verify watchdog does not interfere with OTA updates.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Start OTA firmware update | Upload begins |
| 2 | Monitor watchdog during upload | WDT continues to be fed |
| 3 | Update takes > 60 seconds | No watchdog timeout |
| 4 | Update completes | Success, system reboots |
| 5 | New firmware starts | Watchdog initializes |
| 6 | WiFi reconnects | Normal operation |

**Pass Criteria**: OTA update completes without watchdog interference.

---

## 5. Test Environment Setup

### 5.1 Required Equipment

- ESP32 OCPP Server device
- WiFi router with configurable settings
- MQTT broker (Mosquitto recommended)
- Network analyzer (Wireshark optional)
- Phone/laptop for captive portal testing

### 5.2 Network Configuration

```
WiFi Network: TestNetwork
Password: testpassword123
DHCP Range: 192.168.1.100-200
MQTT Broker: 192.168.1.50:1883
```

### 5.3 Monitoring Commands

```bash
# Monitor WiFi events on ESP32 serial
idf.py monitor

# Monitor MQTT messages
mosquitto_sub -h 192.168.1.50 -t "ocpp/#" -v

# Check WiFi signal strength
iw dev wlan0 link
```

---

## 6. Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-05 | Initial extraction from ocpp-esp32-fsd.md |
| 1.1 | 2026-02-05 | Added watchdog test cases (EC-116 to EC-120) |
