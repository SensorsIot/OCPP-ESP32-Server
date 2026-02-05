# Test Execution Plan

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.0 |
| Status | Draft |
| Created | 2026-02-03 |
| Based on | Current codebase analysis |

## 1. Current Implementation Status

### Ready for Testing
| Component | Status | Notes |
|-----------|--------|-------|
| OCPP Charge Point Emulator | **Complete** | Full OCPP 1.6J message support |
| WebSocket Client | **Complete** | Auto-reconnection, heartbeat, meter loops |
| Connector State Machine | **Complete** | All OCPP states supported |
| Meter Simulation | **Complete** | 1-phase/3-phase, power/current/energy |
| Charging Profiles | **Complete** | A and W units, validation |
| Configuration System | **Complete** | YAML-based, dataclass models |
| Web UI Dashboard | **Complete** | Real-time state, controls, message trace |
| Service Layer | **Complete** | State management, event logging |
| CLI Entry Point | **Complete** | `python -m src.main run` |

### Not Yet Implemented
| Component | Status | Impact |
|-----------|--------|--------|
| Test Scenarios | **Empty stubs** | No automated test sequences |
| MQTT Client | **Empty stubs** | Cannot test MQTT integration |
| Unit Tests | **Empty stubs** | No automated validation |
| EV Battery Simulation | **Minimal** | Only plug state, no SOC curves |

## 2. Test Execution Modes

### Mode A: Manual Testing via Web UI (Available Now)

**Best for**: Initial validation, debugging, exploratory testing

```
┌─────────────────────────────────────────────────────────────────┐
│                    Manual Test Setup                             │
│                                                                  │
│  ┌──────────────────┐         ┌──────────────────────────────┐  │
│  │  OCPP Server     │◄───────│  Wallbox Emulator            │  │
│  │  (Real or Mock)  │ WS     │  + Web UI on :8080           │  │
│  │  :9000           │        │                              │  │
│  └──────────────────┘        └──────────────────────────────┘  │
│                                        │                        │
│                              ┌─────────▼─────────┐              │
│                              │    Browser        │              │
│                              │  http://localhost │              │
│                              │      :8080        │              │
│                              └───────────────────┘              │
└─────────────────────────────────────────────────────────────────┘
```

**Capabilities**:
- Plug/unplug EV simulation
- Start/stop transactions
- Switch between 1-phase and 3-phase modes
- Toggle authorization requirement
- Monitor real-time power, current, energy
- View OCPP message trace

### Mode B: CLI-Driven Testing (Available Now)

**Best for**: Repeatable manual tests, CI smoke tests

Run the emulator and interact programmatically via the Web UI API endpoints:
- `POST /api/plug` - Plug in EV
- `POST /api/start` - Start transaction
- `POST /api/stop` - Stop transaction
- `POST /api/unplug` - Unplug EV
- `POST /api/phase` - Set phase mode
- `GET /api/state` - Get current state

### Mode C: Automated Scenarios (Requires Implementation)

**Best for**: Full test suite execution, regression testing

The scenario framework exists but files are empty. Once implemented:
```bash
python -m src.main scenario basic_charge --server ws://...
python -m src.main scenario phase_switch --server ws://...
```

## 3. Infrastructure Requirements

### 3.1 Option A: Test Against Mock OCPP Server

For development without the ESP32 hardware:

```bash
# Install a mock OCPP server (example using ocpp library)
pip install ocpp websockets

# Create a simple CSMS mock (see Section 6)
python mock_csms.py
```

### 3.2 Option B: Test Against ESP32 OCPP Server

For integration testing with real hardware:

| Requirement | Details |
|-------------|---------|
| ESP32 Device | Running OCPP server firmware |
| Network | Wallbox on same network as ESP32 |
| Configuration | Update `ocpp_server` URL in config |

### 3.3 Dependencies Installation

```bash
cd ocpp-test-wallbox
python -m venv venv
source venv/bin/activate  # or `venv\Scripts\activate` on Windows
pip install -r requirements.txt
```

## 4. Test Execution Steps

### Phase 1: Smoke Test (Manual)

**Goal**: Verify basic connectivity and OCPP handshake

**Steps**:
1. Start OCPP server (mock or real)
2. Configure `config/default.yaml`:
   ```yaml
   wallbox:
     ocpp_server: "ws://SERVER_IP:9000/ocpp/CP001"
   ```
3. Start wallbox emulator:
   ```bash
   python -m src.main run
   ```
4. Open browser: `http://localhost:8080`
5. Verify:
   - [ ] Connection status shows "connected"
   - [ ] BootNotification sent (check message trace)
   - [ ] StatusNotification shows "Available"
   - [ ] Heartbeat messages appear periodically

**Expected Results**:
- Web UI shows connected state
- Message trace shows BootNotification -> Accepted
- Connector status: Available

### Phase 2: Basic Charging Cycle (Manual)

**Goal**: Validate TC-100 equivalent (basic charge cycle)

**Steps**:
1. Complete Phase 1 (connected, Available)
2. In Web UI, click "Plug In"
3. Verify StatusNotification -> Preparing
4. Click "Start Transaction" (enter idTag if auth required)
5. Verify:
   - [ ] Authorize sent (if enabled)
   - [ ] StartTransaction sent
   - [ ] StatusNotification -> Charging
   - [ ] MeterValues appearing periodically
6. Observe power/energy incrementing
7. Click "Stop Transaction"
8. Verify:
   - [ ] StopTransaction sent
   - [ ] StatusNotification -> Finishing -> Available
9. Click "Unplug"

**Expected Results**:
- Full transaction lifecycle completes
- Energy accumulates during charging
- All state transitions correct

### Phase 3: Phase Mode Testing (Manual)

**Goal**: Validate 1-phase vs 3-phase meter reporting

**Steps**:
1. Start charging in 3-phase mode (default)
2. Note meter values in Web UI:
   - Power: ~11,040 W at 16A
   - L1/L2/L3 current: ~16A each
3. Stop transaction
4. In Web UI, select "1-phase" mode
5. Start new transaction
6. Note meter values:
   - Power: ~11,040 W (raw, uncorrected)
   - L1 current: ~16A
   - L2/L3 current: 0A
7. Verify the raw power is still 3-phase equivalent (CSMS applies /3 correction)

**Expected Results**:
- 3-phase: All phases show equal current
- 1-phase: Only L1 has current, L2/L3 are zero
- Power reported as 3-phase equivalent in both modes

### Phase 4: Charging Profile Response (Manual)

**Goal**: Validate SetChargingProfile handling

**Requires**: OCPP server that sends SetChargingProfile commands

**Steps**:
1. Start charging
2. From CSMS, send SetChargingProfile with limit: 10A
3. Verify in Web UI:
   - [ ] Current limit updates to 10A
   - [ ] MeterValues reflect new power level (~6,900W at 3-phase)
4. Send SetChargingProfile with limit: 0A
5. Verify:
   - [ ] StatusNotification -> SuspendedEVSE
   - [ ] Power drops to 0W
6. Send SetChargingProfile with limit: 16A
7. Verify charging resumes

**Expected Results**:
- Charging power responds to profile changes within 5 seconds
- Zero limit triggers SuspendedEVSE state

### Phase 5: Remote Control (Manual)

**Goal**: Validate RemoteStart/RemoteStop from CSMS

**Requires**: OCPP server that can send RemoteStartTransaction/RemoteStopTransaction

**Steps**:
1. Plug in EV (status: Preparing)
2. From CSMS, send RemoteStartTransaction
3. Verify transaction starts
4. From CSMS, send RemoteStopTransaction
5. Verify transaction stops with reason: "Remote"

## 5. Test Mapping to OCPP-Test.md

| Test Case | Manual Method | Status |
|-----------|---------------|--------|
| TC-010: Boot and Registration | Phase 1 | Testable now |
| TC-011: Heartbeat Keepalive | Phase 1 (observe) | Testable now |
| TC-012: TriggerMessage | Requires CSMS | Testable with CSMS |
| TC-013: GetConfiguration | Requires CSMS | Testable with CSMS |
| TC-014: ChangeConfiguration | Requires CSMS | Testable with CSMS |
| TC-100: Charge at 11 kW | Phase 2 + 4 | Testable now |
| TC-101: Charge at 3.7 kW (1-phase) | Phase 3 | Testable now |
| TC-105: Suspend at 0A | Phase 4 | Testable with CSMS |
| TC-200: Current Ramp Down | Phase 4 | Testable with CSMS |
| TC-300: Remote Start | Phase 5 | Testable with CSMS |
| TC-301: Remote Stop | Phase 5 | Testable with CSMS |
| TC-400: Phase Switch | Phase 3 | Partial (manual mode change) |

## 6. Mock CSMS Server (Optional)

For testing without ESP32 hardware, create a simple mock:

```python
# mock_csms.py - Simple OCPP 1.6J CSMS for testing
import asyncio
import logging
from datetime import datetime, timezone
from ocpp.routing import on
from ocpp.v16 import ChargePoint as cp, call, call_result
from ocpp.v16.enums import RegistrationStatus, AuthorizationStatus
import websockets

logging.basicConfig(level=logging.INFO)

class MockCSMS(cp):
    transaction_id = 1000

    @on('BootNotification')
    def on_boot_notification(self, charge_point_vendor, charge_point_model, **kwargs):
        return call_result.BootNotificationPayload(
            current_time=datetime.now(timezone.utc).isoformat(),
            interval=60,
            status=RegistrationStatus.accepted
        )

    @on('Heartbeat')
    def on_heartbeat(self):
        return call_result.HeartbeatPayload(
            current_time=datetime.now(timezone.utc).isoformat()
        )

    @on('StatusNotification')
    def on_status_notification(self, connector_id, error_code, status, **kwargs):
        logging.info(f"Status: connector={connector_id}, status={status}")
        return call_result.StatusNotificationPayload()

    @on('Authorize')
    def on_authorize(self, id_tag):
        return call_result.AuthorizePayload(
            id_tag_info={'status': AuthorizationStatus.accepted}
        )

    @on('StartTransaction')
    def on_start_transaction(self, connector_id, id_tag, meter_start, timestamp, **kwargs):
        self.transaction_id += 1
        return call_result.StartTransactionPayload(
            transaction_id=self.transaction_id,
            id_tag_info={'status': AuthorizationStatus.accepted}
        )

    @on('StopTransaction')
    def on_stop_transaction(self, meter_stop, timestamp, transaction_id, **kwargs):
        return call_result.StopTransactionPayload()

    @on('MeterValues')
    def on_meter_values(self, connector_id, meter_value, **kwargs):
        for mv in meter_value:
            for sv in mv.get('sampled_value', []):
                logging.info(f"Meter: {sv.get('measurand', 'unknown')}={sv.get('value')}")
        return call_result.MeterValuesPayload()

async def on_connect(websocket, path):
    charge_point_id = path.strip('/').split('/')[-1]
    cp = MockCSMS(charge_point_id, websocket)
    logging.info(f"ChargePoint {charge_point_id} connected")
    await cp.start()

async def main():
    server = await websockets.serve(
        on_connect,
        '0.0.0.0',
        9000,
        subprotocols=['ocpp1.6']
    )
    logging.info("Mock CSMS running on ws://0.0.0.0:9000")
    await server.wait_closed()

if __name__ == '__main__':
    asyncio.run(main())
```

## 7. Next Steps for Full Automation

### Priority 1: Implement Basic Scenario Runner

Create `src/scenarios/base.py` with:
- BaseScenario class with setup/execute/teardown
- Assertion helpers for message validation
- Timing verification utilities

### Priority 2: Implement Core Test Scenarios

| Scenario | File | Maps to Test Cases |
|----------|------|-------------------|
| BasicChargeScenario | basic_charge.py | TC-100, TC-101, TC-106 |
| PowerLimitScenario | power_limit.py | TC-102–TC-105, TC-200–TC-201 |
| PhaseSwitchScenario | phase_switch.py | TC-400–TC-407 |
| StressTestScenario | stress_test.py | TC-203 |

### Priority 3: Add CSMS Control Interface

To send commands TO the wallbox (SetChargingProfile, RemoteStart, etc.),
the test framework needs a way to inject CSMS commands. Options:
1. Extend mock CSMS with command injection API
2. Use a test CSMS library with programmatic control
3. Add HTTP API to mock CSMS for test control

### Priority 4: Implement MQTT Client (if needed)

For testing ESP32 CSMS MQTT integration:
- Implement `src/mqtt_client/` components
- Subscribe to CSMS status topics
- Publish commands (start, stop, phase switch)

## 8. Quick Start Commands

```bash
# 1. Install dependencies
cd ocpp-test-wallbox
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# 2. Start mock CSMS (in terminal 1)
python mock_csms.py

# 3. Start wallbox emulator (in terminal 2)
python -m src.main run

# 4. Open Web UI
# Browser: http://localhost:8080

# 5. Run manual tests per Phase 1-5 above
```

## 9. Test Reporting

For each test session, record:

| Field | Value |
|-------|-------|
| Date | |
| Tester | |
| CSMS Version | (Mock / ESP32 commit) |
| Wallbox Config | (attach YAML) |
| Tests Executed | |
| Pass/Fail | |
| Notes | |

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-03 | Initial plan based on codebase analysis |
