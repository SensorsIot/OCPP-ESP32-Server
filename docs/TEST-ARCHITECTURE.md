# Test Automation Architecture

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.0 |
| Status | Draft |
| Created | 2026-02-03 |
| Related | OCPP-Test.md, ocpp-wallbox-tester-fsd.md |

## 1. Overview

This document describes the architecture for automated test execution against
the ESP32 OCPP Server (CSMS). The test framework uses the Wallbox Emulator as
the Charge Point (CP) and captures all interactions to verify correct behavior.

### 1.1 Key Insight

The Wallbox Emulator is **the instrument** for testing the ESP32 CSMS:
- We control the wallbox behavior
- We capture wallbox reactions to CSMS commands
- We verify CSMS responses and MQTT output
- The wallbox's state changes ARE test results

### 1.2 System Under Test

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        TEST ENVIRONMENT                                  │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                      Test Runner (pytest)                          │ │
│  │  - Orchestrates test execution                                     │ │
│  │  - Controls wallbox actions (plug, start, stop)                    │ │
│  │  - Asserts on captured events                                      │ │
│  │  - Verifies MQTT messages                                          │ │
│  └─────────────────────────────┬──────────────────────────────────────┘ │
│                                │                                         │
│            ┌───────────────────┼───────────────────┐                    │
│            │                   │                   │                    │
│            ▼                   ▼                   ▼                    │
│  ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐        │
│  │ Wallbox Emulator │ │  MQTT Observer   │ │  Timing/Assert   │        │
│  │                  │ │                  │ │                  │        │
│  │ - OCPP client    │ │ - Subscribes to  │ │ - Timeout checks │        │
│  │ - State machine  │ │   ocpp/#         │ │ - Tolerance calc │        │
│  │ - Meter sim      │ │ - Captures msgs  │ │ - Event queries  │        │
│  │ - Event log      │ │ - Timestamps     │ │                  │        │
│  └────────┬─────────┘ └────────┬─────────┘ └──────────────────┘        │
│           │                    │                                        │
└───────────┼────────────────────┼────────────────────────────────────────┘
            │ OCPP 1.6J          │ MQTT
            │ WebSocket          │
            ▼                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    ESP32 OCPP Server (CSMS)                              │
│                    192.168.0.105:8887                                    │
│                                                                          │
│  - Receives CP messages (BootNotification, StartTransaction, etc.)      │
│  - Sends commands (SetChargingProfile, RemoteStart, etc.)               │
│  - Publishes to MQTT (status, session, meter, phase)                    │
└─────────────────────────────────────────────────────────────────────────┘
            │
            │ MQTT Publish
            ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       MQTT Broker                                        │
│                    192.168.0.203:1883                                    │
└─────────────────────────────────────────────────────────────────────────┘
```

## 2. Event Capture System

### 2.1 Why Event Capture?

Test assertions need to verify:
1. What OCPP messages the wallbox **received** from CSMS
2. What OCPP messages the wallbox **sent** to CSMS
3. What **state changes** occurred in the wallbox
4. What **meter values** were reported
5. What **MQTT messages** the CSMS published

All events are timestamped for timing verification.

### 2.2 Event Types

```python
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Optional

@dataclass
class Event:
    """Base class for all captured events"""
    timestamp: datetime
    event_type: str

@dataclass
class MessageReceived(Event):
    """OCPP message received from CSMS"""
    event_type: str = "message_received"
    action: str = ""           # e.g., "SetChargingProfile"
    message_id: str = ""
    payload: dict = None

@dataclass
class MessageSent(Event):
    """OCPP message sent to CSMS"""
    event_type: str = "message_sent"
    action: str = ""           # e.g., "StatusNotification"
    message_id: str = ""
    payload: dict = None
    response: dict = None      # CallResult received

@dataclass
class StateChange(Event):
    """Connector state transition"""
    event_type: str = "state_change"
    connector_id: int = 1
    old_status: str = ""       # e.g., "Available"
    new_status: str = ""       # e.g., "Preparing"

@dataclass
class MeterUpdate(Event):
    """Meter values snapshot"""
    event_type: str = "meter_update"
    power_w: float = 0.0
    energy_wh: float = 0.0
    current_l1_a: float = 0.0
    current_l2_a: float = 0.0
    current_l3_a: float = 0.0
    voltage_v: float = 230.0

@dataclass
class ProfileApplied(Event):
    """Charging profile applied"""
    event_type: str = "profile_applied"
    profile_id: int = 0
    limit_a: Optional[float] = None
    limit_w: Optional[float] = None
    rate_unit: str = "A"       # "A" or "W"

@dataclass
class TransactionEvent(Event):
    """Transaction started or stopped"""
    event_type: str = "transaction"
    action: str = ""           # "started" or "stopped"
    transaction_id: int = 0
    connector_id: int = 1
    id_tag: str = ""
    meter_value: int = 0       # meterStart or meterStop (Wh)
    reason: str = ""           # StopTransaction reason

@dataclass
class MqttMessage(Event):
    """MQTT message from CSMS"""
    event_type: str = "mqtt"
    topic: str = ""
    payload: dict = None
```

### 2.3 Event Log

```python
from typing import List, Type, TypeVar, Optional
import asyncio

T = TypeVar('T', bound=Event)

class EventLog:
    """Thread-safe event log with query capabilities"""

    def __init__(self):
        self._events: List[Event] = []
        self._lock = asyncio.Lock()

    async def add(self, event: Event) -> None:
        """Add an event to the log"""
        async with self._lock:
            self._events.append(event)

    def clear(self) -> None:
        """Clear all events (call before test action)"""
        self._events.clear()

    def find(
        self,
        event_type: Type[T],
        **filters
    ) -> Optional[T]:
        """Find first event matching type and filters"""
        for event in self._events:
            if isinstance(event, event_type):
                if all(getattr(event, k, None) == v for k, v in filters.items()):
                    return event
        return None

    def find_all(
        self,
        event_type: Type[T],
        **filters
    ) -> List[T]:
        """Find all events matching type and filters"""
        results = []
        for event in self._events:
            if isinstance(event, event_type):
                if all(getattr(event, k, None) == v for k, v in filters.items()):
                    results.append(event)
        return results

    def count(self, event_type: Type[T], **filters) -> int:
        """Count events matching type and filters"""
        return len(self.find_all(event_type, **filters))

    def wait_for(
        self,
        event_type: Type[T],
        timeout: float = 10.0,
        **filters
    ) -> T:
        """Wait for an event to appear (used in assertions)"""
        # Implementation uses asyncio.wait_for with polling
        pass

    @property
    def all(self) -> List[Event]:
        """Get all events (for debugging)"""
        return list(self._events)
```

## 3. Wallbox Emulator Integration

### 3.1 Event Emission Points

The wallbox emulator logs events at these points:

```python
class ChargePoint:
    def __init__(self, ...):
        self.events = EventLog()

    # === OCPP Message Handlers ===

    @on("SetChargingProfile")
    async def on_set_charging_profile(self, connector_id, cs_charging_profiles):
        # Log message received
        await self.events.add(MessageReceived(
            timestamp=datetime.utcnow(),
            action="SetChargingProfile",
            payload={"connector_id": connector_id, "cs_charging_profiles": cs_charging_profiles}
        ))

        # Apply profile
        schedule = cs_charging_profiles["chargingSchedule"]
        limit = schedule["chargingSchedulePeriod"][0]["limit"]
        unit = schedule["chargingRateUnit"]

        await self.events.add(ProfileApplied(
            timestamp=datetime.utcnow(),
            limit_a=limit if unit == "A" else None,
            limit_w=limit if unit == "W" else None,
            rate_unit=unit
        ))

        # Handle suspend at 0
        if limit == 0:
            await self._change_state(connector_id, "SuspendedEVSE")

        return {"status": "Accepted"}

    # === State Changes ===

    async def _change_state(self, connector_id: int, new_status: str):
        old_status = self.connector.status

        await self.events.add(StateChange(
            timestamp=datetime.utcnow(),
            connector_id=connector_id,
            old_status=old_status,
            new_status=new_status
        ))

        self.connector.status = new_status
        await self._send_status_notification(connector_id, new_status)

    # === Outgoing Messages ===

    async def _send_status_notification(self, connector_id: int, status: str):
        payload = {
            "connectorId": connector_id,
            "status": status,
            "errorCode": "NoError",
            "timestamp": datetime.utcnow().isoformat() + "Z"
        }

        response = await self.call(StatusNotification(**payload))

        await self.events.add(MessageSent(
            timestamp=datetime.utcnow(),
            action="StatusNotification",
            payload=payload,
            response=response
        ))

    # === Meter Updates ===

    async def _send_meter_values(self):
        meter = self.meter.get_values()

        await self.events.add(MeterUpdate(
            timestamp=datetime.utcnow(),
            power_w=meter["power_w"],
            energy_wh=meter["energy_wh"],
            current_l1_a=meter["current_l1"],
            current_l2_a=meter["current_l2"],
            current_l3_a=meter["current_l3"],
            voltage_v=meter["voltage"]
        ))

        # Send OCPP MeterValues message...
```

### 3.2 MQTT Observer

```python
class MqttObserver:
    """Captures MQTT messages from CSMS for test assertions"""

    def __init__(self, broker: str, port: int = 1883):
        self.broker = broker
        self.port = port
        self.events = EventLog()
        self._client = None

    async def start(self, topics: List[str] = ["ocpp/#"]):
        """Start observing MQTT topics"""
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self._client.on_message = self._on_message
        self._client.connect(self.broker, self.port)
        for topic in topics:
            self._client.subscribe(topic)
        self._client.loop_start()

    async def stop(self):
        """Stop observing"""
        if self._client:
            self._client.loop_stop()
            self._client.disconnect()

    def _on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
        except:
            payload = {"raw": msg.payload.decode()}

        # Add to event log synchronously (called from MQTT thread)
        asyncio.run_coroutine_threadsafe(
            self.events.add(MqttMessage(
                timestamp=datetime.utcnow(),
                topic=msg.topic,
                payload=payload
            )),
            self._loop
        )
```

## 4. Test Structure

### 4.1 Directory Layout

```
ocpp-test-wallbox/
├── src/
│   ├── wallbox_emulator/
│   │   ├── chargepoint.py      # OCPP handlers with event logging
│   │   ├── connector.py        # Connector state machine
│   │   ├── meter.py            # Meter simulation
│   │   └── ev_simulator.py     # EV behavior
│   ├── testing/
│   │   ├── __init__.py
│   │   ├── events.py           # Event dataclasses
│   │   ├── event_log.py        # EventLog class
│   │   ├── mqtt_observer.py    # MQTT capture
│   │   ├── assertions.py       # Custom assertions
│   │   └── fixtures.py         # pytest fixtures
│   └── tests/
│       ├── conftest.py         # Shared fixtures
│       ├── test_connection.py  # TC-010 to TC-021
│       ├── test_charging.py    # TC-100 to TC-115
│       ├── test_power.py       # TC-200 to TC-203
│       ├── test_remote.py      # TC-300 to TC-302
│       ├── test_phase.py       # TC-400 to TC-407
│       ├── test_errors.py      # TC-500 to TC-504
│       └── test_metering.py    # TC-600 to TC-601
└── pytest.ini
```

### 4.2 Fixtures (conftest.py)

```python
import pytest
import asyncio
from src.wallbox_emulator import ChargePoint
from src.testing import MqttObserver

@pytest.fixture
async def wallbox():
    """Provide a connected wallbox emulator"""
    cp = ChargePoint(
        charge_point_id="TestWB",
        server_url="ws://192.168.0.105:8887/ocpp/TestWB"
    )
    await cp.connect()
    await cp.wait_for_boot_accepted()
    yield cp
    await cp.disconnect()

@pytest.fixture
async def mqtt_observer():
    """Provide an MQTT observer"""
    observer = MqttObserver(broker="192.168.0.203")
    await observer.start(topics=["ocpp/#"])
    yield observer
    await observer.stop()

@pytest.fixture
def assert_within():
    """Assertion helper for timing checks"""
    def _assert(actual, expected, tolerance_pct=5):
        tolerance = expected * tolerance_pct / 100
        assert expected - tolerance <= actual <= expected + tolerance, \
            f"Expected {expected}±{tolerance_pct}%, got {actual}"
    return _assert
```

### 4.3 Example Test: TC-105 (Suspend at 0A)

```python
import pytest
from src.testing.events import (
    MessageReceived, MessageSent, StateChange,
    MeterUpdate, ProfileApplied, MqttMessage
)

@pytest.mark.asyncio
async def test_tc105_suspend_at_zero(wallbox, mqtt_observer, assert_within):
    """
    TC-105: Charge at 0 kW (Suspend)

    Verify charging suspends cleanly when limit is set to zero.
    """
    # === SETUP: Start charging at 16A ===
    await wallbox.plug_ev()
    await wallbox.start_transaction(id_tag="evcc")
    await wallbox.wait_for_state("Charging", timeout=10)

    # Verify initial charging power
    initial_meter = wallbox.events.find(MeterUpdate)
    assert_within(initial_meter.power_w, 11040)  # 3-phase 16A

    # Clear event log before test action
    wallbox.events.clear()
    mqtt_observer.events.clear()

    # === ACTION: Send 0A via MQTT command to CSMS ===
    # (CSMS will send SetChargingProfile to wallbox)
    await mqtt_publish(
        "ocpp/ocpp-esp32/command/limit",
        {"connector_id": 1, "current_limit_a": 0}
    )

    # === WAIT: Allow time for CSMS to send profile ===
    await asyncio.sleep(10)

    # === VERIFY: Wallbox received SetChargingProfile ===
    profile_rx = wallbox.events.find(
        MessageReceived,
        action="SetChargingProfile"
    )
    assert profile_rx is not None, "Wallbox did not receive SetChargingProfile"
    assert profile_rx.payload["cs_charging_profiles"]["chargingSchedule"] \
        ["chargingSchedulePeriod"][0]["limit"] == 0

    # === VERIFY: Wallbox applied profile internally ===
    profile_applied = wallbox.events.find(ProfileApplied)
    assert profile_applied is not None, "Profile was not applied"
    assert profile_applied.limit_a == 0

    # === VERIFY: State changed to SuspendedEVSE ===
    state_change = wallbox.events.find(
        StateChange,
        new_status="SuspendedEVSE"
    )
    assert state_change is not None, "State did not change to SuspendedEVSE"

    # === VERIFY: StatusNotification sent ===
    status_sent = wallbox.events.find(
        MessageSent,
        action="StatusNotification"
    )
    assert status_sent is not None
    assert status_sent.payload["status"] == "SuspendedEVSE"

    # === VERIFY: MeterValues show 0W ===
    meter = wallbox.events.find_all(MeterUpdate)[-1]  # Latest
    assert meter.power_w == 0, f"Power should be 0W, got {meter.power_w}"
    assert meter.current_l1_a == 0
    assert meter.current_l2_a == 0
    assert meter.current_l3_a == 0

    # === VERIFY: MQTT status published ===
    mqtt_status = mqtt_observer.events.find(
        MqttMessage,
        topic="ocpp/ocpp-esp32/status"
    )
    assert mqtt_status is not None, "CSMS did not publish status to MQTT"
    assert mqtt_status.payload["status"] == "SuspendedEVSE"

    # === RESUME: Set limit back to 16A ===
    wallbox.events.clear()

    await mqtt_publish(
        "ocpp/ocpp-esp32/command/limit",
        {"connector_id": 1, "current_limit_a": 16}
    )

    await asyncio.sleep(10)

    # === VERIFY: Charging resumes ===
    state_change = wallbox.events.find(
        StateChange,
        new_status="Charging"
    )
    assert state_change is not None, "State did not return to Charging"

    meter = wallbox.events.find_all(MeterUpdate)[-1]
    assert_within(meter.power_w, 11040)
```

## 5. Assertion Helpers

### 5.1 Custom Assertions

```python
# src/testing/assertions.py

class Assertions:
    """Custom assertions for OCPP testing"""

    @staticmethod
    def assert_message_received(
        events: EventLog,
        action: str,
        within_seconds: float = 5.0,
        **payload_checks
    ):
        """Assert wallbox received a specific OCPP message"""
        msg = events.find(MessageReceived, action=action)
        assert msg is not None, f"Did not receive {action}"

        for key, expected in payload_checks.items():
            actual = msg.payload.get(key)
            assert actual == expected, \
                f"{action}.{key}: expected {expected}, got {actual}"

    @staticmethod
    def assert_message_sent(
        events: EventLog,
        action: str,
        within_seconds: float = 5.0,
        **payload_checks
    ):
        """Assert wallbox sent a specific OCPP message"""
        msg = events.find(MessageSent, action=action)
        assert msg is not None, f"Did not send {action}"

        for key, expected in payload_checks.items():
            actual = msg.payload.get(key)
            assert actual == expected, \
                f"{action}.{key}: expected {expected}, got {actual}"

    @staticmethod
    def assert_state_transition(
        events: EventLog,
        from_status: str,
        to_status: str,
        within_seconds: float = 5.0
    ):
        """Assert a state transition occurred"""
        change = events.find(
            StateChange,
            old_status=from_status,
            new_status=to_status
        )
        assert change is not None, \
            f"No transition from {from_status} to {to_status}"

    @staticmethod
    def assert_power_within(
        events: EventLog,
        expected_w: float,
        tolerance_pct: float = 5.0
    ):
        """Assert power is within tolerance"""
        meter = events.find_all(MeterUpdate)
        assert len(meter) > 0, "No meter updates recorded"

        latest = meter[-1]
        tolerance = expected_w * tolerance_pct / 100
        assert expected_w - tolerance <= latest.power_w <= expected_w + tolerance, \
            f"Power {latest.power_w}W not within {tolerance_pct}% of {expected_w}W"

    @staticmethod
    def assert_mqtt_published(
        events: EventLog,
        topic_pattern: str,
        within_seconds: float = 2.0,
        **payload_checks
    ):
        """Assert MQTT message was published"""
        for msg in events.find_all(MqttMessage):
            if topic_pattern in msg.topic:
                if all(msg.payload.get(k) == v for k, v in payload_checks.items()):
                    return  # Found matching message

        assert False, f"No MQTT message matching {topic_pattern} with {payload_checks}"
```

## 6. Timing and Tolerance

### 6.1 Default Timing Rules

| Condition | Default | Override |
|-----------|---------|----------|
| OCPP response time | ≤ 5 seconds | Per-test `timeout` param |
| State change notification | ≤ 5 seconds | Per-test |
| MeterValues interval | `MeterValueSampleInterval` ± 1s | Config |
| Charging profile application | ≤ 5 seconds | Per-test |
| MQTT publication | ≤ 2 seconds | Per-test |

### 6.2 Default Tolerance Rules

| Measurement | Tolerance |
|-------------|-----------|
| Power (W) | ±5% |
| Current (A) | ±5% |
| Voltage (V) | ±5% |
| Energy (Wh) | ±2% |

### 6.3 Timing Verification

```python
async def assert_within_time(
    coro,
    max_seconds: float,
    description: str = "Operation"
):
    """Assert an async operation completes within time limit"""
    start = time.time()
    try:
        result = await asyncio.wait_for(coro, timeout=max_seconds)
        elapsed = time.time() - start
        return result, elapsed
    except asyncio.TimeoutError:
        raise AssertionError(
            f"{description} did not complete within {max_seconds}s"
        )
```

## 7. Test Runner UI

### 7.1 Overview

The test runner exposes a web UI for real-time test progress and results.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Test Runner Web UI (:8081)                            │
├─────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Test Suite Progress                                    [Run All] │    │
│  ├─────────────────────────────────────────────────────────────────┤    │
│  │  ████████████████████░░░░░░░░░░░░  18/50 tests  (36%)           │    │
│  │  Running: TC-105 Suspend at 0A                                   │    │
│  │  Elapsed: 00:02:34                                               │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
│  ┌──────────────────────────────┐  ┌──────────────────────────────┐    │
│  │  Results Summary             │  │  Live Event Feed             │    │
│  ├──────────────────────────────┤  ├──────────────────────────────┤    │
│  │  ✓ Passed:  15               │  │  12:00:05 → SetChargingProf  │    │
│  │  ✗ Failed:   2               │  │  12:00:05 ← ProfileApplied   │    │
│  │  ○ Skipped:  1               │  │  12:00:06 → StateChange      │    │
│  │  ◌ Pending: 32               │  │  12:00:06 ← StatusNotif...   │    │
│  └──────────────────────────────┘  └──────────────────────────────┘    │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Test Results                                          [Export]  │    │
│  ├─────────────────────────────────────────────────────────────────┤    │
│  │  ✓ TC-010 Boot and Registration              0.8s    [Details]  │    │
│  │  ✓ TC-011 Heartbeat Keepalive               62.1s    [Details]  │    │
│  │  ✓ TC-012 TriggerMessage                     1.2s    [Details]  │    │
│  │  ✗ TC-013 GetConfiguration                   5.0s    [Details]  │    │
│  │    └─ AssertionError: Missing key 'MeterValueSampleInterval'    │    │
│  │  ▶ TC-105 Suspend at 0A                    running   [Live]     │    │
│  │  ○ TC-106 Charge Without Auth               skipped  [Details]  │    │
│  │  ◌ TC-200 Current Ramp Down                 pending             │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

### 7.2 UI Components

| Component | Description |
|-----------|-------------|
| **Progress Bar** | Overall test suite progress with percentage |
| **Current Test** | Name and status of currently running test |
| **Results Summary** | Pass/fail/skip/pending counts |
| **Live Event Feed** | Real-time stream of events from current test |
| **Test Results List** | Expandable list with timing and error details |
| **Controls** | Run All, Run Selected, Stop, Re-run Failed |

### 7.3 Test Runner Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Test Runner Service                              │
│                                                                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐         │
│  │  Test Executor  │  │  Results Store  │  │   Web Server    │         │
│  │                 │  │                 │  │   (FastAPI)     │         │
│  │  - pytest       │  │  - test_id      │  │                 │         │
│  │  - async runner │  │  - status       │  │  /api/tests     │         │
│  │  - event hooks  │  │  - duration     │  │  /api/run       │         │
│  │                 │  │  - events[]     │  │  /api/results   │         │
│  │                 │  │  - error        │  │  /ws/events     │         │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘         │
│           │                    │                    │                   │
│           └────────────────────┴────────────────────┘                   │
│                                │                                        │
│                    ┌───────────▼───────────┐                           │
│                    │    WebSocket Hub      │                           │
│                    │  (broadcast events)   │                           │
│                    └───────────────────────┘                           │
└─────────────────────────────────────────────────────────────────────────┘
                                 │
                    WebSocket (live updates)
                                 │
                    ┌────────────▼────────────┐
                    │      Browser UI            │
                    │  http://192.168.0.160:8081 │
                    └────────────────────────────┘
```

### 7.4 API Endpoints

```python
# Test Runner API

GET  /api/tests              # List all test cases
GET  /api/tests/{id}         # Get test case details
GET  /api/results            # Get all results (current session)
GET  /api/results/{id}       # Get result for specific test
GET  /api/results/{id}/events # Get event log for test

POST /api/run                # Run all tests
POST /api/run/{id}           # Run specific test
POST /api/run/category/{cat} # Run tests by category
POST /api/stop               # Stop current run
POST /api/rerun-failed       # Re-run failed tests

WS   /ws/events              # WebSocket for live events
```

### 7.5 WebSocket Events

```typescript
// Events pushed to browser

interface TestStarted {
  type: "test_started";
  test_id: string;
  test_name: string;
  timestamp: string;
}

interface TestCompleted {
  type: "test_completed";
  test_id: string;
  status: "passed" | "failed" | "skipped";
  duration_ms: number;
  error?: string;
}

interface TestEvent {
  type: "test_event";
  test_id: string;
  event: {
    event_type: string;  // "message_sent", "state_change", etc.
    timestamp: string;
    details: object;
  };
}

interface ProgressUpdate {
  type: "progress";
  completed: number;
  total: number;
  current_test: string;
}
```

### 7.6 Result Storage

```python
@dataclass
class TestResult:
    test_id: str           # e.g., "TC-105"
    test_name: str         # e.g., "Suspend at 0A"
    category: str          # e.g., "charging"
    status: str            # "passed", "failed", "skipped", "running", "pending"
    started_at: datetime
    completed_at: Optional[datetime]
    duration_ms: int
    events: List[Event]    # Full event log
    error: Optional[str]   # Error message if failed
    error_trace: Optional[str]  # Stack trace

class ResultsStore:
    """In-memory store for test results"""

    def __init__(self):
        self.results: Dict[str, TestResult] = {}
        self.run_id: str = ""
        self.started_at: datetime = None

    def start_run(self) -> str:
        """Start a new test run, return run_id"""
        self.run_id = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
        self.started_at = datetime.utcnow()
        self.results.clear()
        return self.run_id

    def start_test(self, test_id: str, test_name: str, category: str):
        self.results[test_id] = TestResult(
            test_id=test_id,
            test_name=test_name,
            category=category,
            status="running",
            started_at=datetime.utcnow(),
            completed_at=None,
            duration_ms=0,
            events=[],
            error=None,
            error_trace=None
        )

    def add_event(self, test_id: str, event: Event):
        if test_id in self.results:
            self.results[test_id].events.append(event)

    def complete_test(self, test_id: str, status: str, error: str = None):
        if test_id in self.results:
            result = self.results[test_id]
            result.status = status
            result.completed_at = datetime.utcnow()
            result.duration_ms = int(
                (result.completed_at - result.started_at).total_seconds() * 1000
            )
            result.error = error

    def export_html(self, path: str):
        """Export results to HTML report"""
        pass

    def export_json(self, path: str):
        """Export results to JSON"""
        pass
```

### 7.7 Running with UI

**Access URL**: http://192.168.0.160:8081

```bash
# Start test runner with web UI
cd /workspaces/OCPP-Server/ocpp-test-wallbox
python run_test_ui.py

# Or run in background
nohup python run_test_ui.py > /tmp/test_ui.log 2>&1 &
```

The UI allows you to:
- Enter a test filter pattern (e.g., `TC-010`, `TC-100`, or leave empty for all)
- Click "Run Tests" to execute
- View live progress updates via WebSocket
- See pass/fail counts, duration, and individual test status

## 8. Test Execution (CLI)

### 8.1 Running Tests via pytest

```bash
# Run all tests (no UI)
pytest src/tests/ -v

# Run specific test file
pytest src/tests/test_charging.py -v

# Run specific test case
pytest src/tests/test_charging.py::test_tc105_suspend_at_zero -v

# Run with timing info
pytest src/tests/ -v --durations=10

# Generate static HTML report
pytest src/tests/ -v --html=report.html
```

### 8.2 Test Markers

```python
# pytest.ini
[pytest]
markers =
    connection: Connection and registration tests (TC-010 to TC-021)
    charging: Charging session tests (TC-100 to TC-115)
    power: Dynamic power control tests (TC-200 to TC-203)
    remote: Remote start/stop tests (TC-300 to TC-302)
    phase: Phase switching tests (TC-400 to TC-407)
    error: Error and edge case tests (TC-500 to TC-504)
    metering: Metering accuracy tests (TC-600 to TC-601)
    slow: Tests that take > 30 seconds
```

### 8.3 Test Order

Tests should run in dependency order:
1. `test_connection.py` - Must pass before others
2. `test_charging.py` - Basic cycles
3. `test_power.py` - Requires charging to work
4. `test_remote.py` - Requires charging to work
5. `test_phase.py` - Requires remote control
6. `test_errors.py` - Edge cases
7. `test_metering.py` - Accuracy validation

## 9. Debugging

### 9.1 Event Log Dump

```python
def dump_events(events: EventLog, filter_type=None):
    """Print all captured events for debugging"""
    for event in events.all:
        if filter_type is None or isinstance(event, filter_type):
            print(f"[{event.timestamp.isoformat()}] {event}")
```

### 9.2 Test Failure Analysis

When a test fails, the event log provides:
1. Exact sequence of events
2. Timestamps for timing analysis
3. Full message payloads
4. State transition history

Example failure output:
```
AssertionError: State did not change to SuspendedEVSE

Event log:
[2026-02-03T12:00:00.000Z] MessageReceived(action='SetChargingProfile', payload={...})
[2026-02-03T12:00:00.050Z] ProfileApplied(limit_a=0)
[2026-02-03T12:00:00.100Z] MeterUpdate(power_w=0, ...)
# Missing: StateChange to SuspendedEVSE
```

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-03 | Initial architecture document |
