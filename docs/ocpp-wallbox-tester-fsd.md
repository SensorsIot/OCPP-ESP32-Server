# OCPP Wallbox Tester — Functional Specification (FSD)

## 1. Purpose

Define the functional specification for a Python-based OCPP 1.6J wallbox tester
that mimics a real charge point (CP). The ESP32 CSMS initiates and drives test
cases; the tester must behave like a wallbox capable of handling all scenarios
in `docs/OCPP-Test.md`.

## 2. Scope

### 2.1 In Scope
- OCPP 1.6J over WebSocket (JSON-RPC framing).
- Single connector (connectorId 1), connectorId 0 for CP-level status.
- Simulator acts as a CP that:
  - Boot/heartbeat/status messages.
  - Authorize/start/stop transactions.
  - MeterValues with per-phase current/voltage and energy.
  - Charging profile handling (A/W), composite schedule.
  - ChangeConfiguration/GetConfiguration.
  - ChangeAvailability, TriggerMessage, Reset.
  - Error scenarios and reconnect behavior.
- Web UI for live state, logs, and manual control.
- Runs locally in a Python venv on this machine.

### 2.2 Out of Scope
- TLS, certificate management, security profiles.
- Firmware update, diagnostics, log upload.
- Multi-connector charge points.

## 3. Assumptions
- ESP32 CSMS runs separately and initiates/executes the test cases.
- CP connects to the CSMS via WebSocket URL `ws://{host}:{port}/ocpp/{cpId}`.
- MeterValues cadence uses `MeterValueSampleInterval` with ±1s tolerance.
- Nominal line voltage: 230 V.
- 1-phase correction: CP reports 3-phase-equivalent power/energy while L2/L3
  currents are zero; CSMS applies /3 correction.

## 4. High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     OCPP Wallbox Tester                             │
│                                                                      │
│  ┌─────────────────────┐              ┌─────────────────────┐       │
│  │  OCPP CP Engine     │              │       Web UI        │       │
│  │                     │              │  - Live state      │       │
│  │  - WS client         │              │  - Logs            │       │
│  │  - OCPP handlers     │              │  - Manual actions  │       │
│  │  - State machine     │              │  - Config edit     │       │
│  │  - Metering model    │              │  - Scenario hints  │       │
│  └──────────┬──────────┘              └──────────┬──────────┘       │
│             │ HTTP/WS (local)                   │                   │
└─────────────┼────────────────────────────────────┼───────────────────┘
              │ WebSocket (OCPP 1.6J)
              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      ESP32 OCPP Server (CSMS)                        │
└─────────────────────────────────────────────────────────────────────┘
```

## 5. Functional Requirements

### 5.1 Connection & Registration
- Send `BootNotification` on connect.
- Accept `Heartbeat` interval from CSMS and send periodic `Heartbeat`.
- Send `StatusNotification` for CP (connectorId 0) and connector (1).
- Reconnect with backoff after disconnects; resend BootNotification.

### 5.2 State Machine
- States: Available, Preparing, Charging, SuspendedEVSE, SuspendedEV,
  Finishing, Unavailable, Faulted.
- Transitions follow `docs/OCPP-Test.md` state diagram.
- Triggered by:
  - EV plug/unplug simulation.
  - CSMS commands (RemoteStart/RemoteStop/ChangeAvailability/Reset).
  - Charging profile changes (e.g., limit 0 -> SuspendedEVSE).

### 5.3 Authorization & Transactions
- Support two modes:
  - **With Authorize**: CP sends Authorize before StartTransaction.
  - **Without Authorize**: CP starts directly (for free charging tests).
- StartTransaction uses meterStart (Wh), timestamp, idTag.
- StopTransaction uses meterStop (Wh), reason, timestamp.
- Support RemoteStartTransaction and RemoteStopTransaction.

### 5.4 Metering
- Generate meter values for:
  - `Energy.Active.Import.Register` (Wh, cumulative).
  - `Power.Active.Import` (W).
  - `Current.Import` per phase (A).
  - `Voltage` per phase (V).
- 3-phase: L1/L2/L3 current equal.
- 1-phase: L1 current non-zero, L2/L3 zero; power/energy 3-phase-equivalent.
- Update on interval and on significant changes (e.g., profile changes).

### 5.5 Charging Profiles
- Accept `SetChargingProfile` with A or W (if configured).
- Validate against `max_current_a` and supported units.
- Apply profile within 5 seconds.
- Support `GetCompositeSchedule` with a single-period schedule.

### 5.6 Configuration
- `GetConfiguration` returns keys listed in `OCPP-Test.md`.
- `ChangeConfiguration`:
  - Accept valid writable keys.
  - Reject invalid value types.
  - Reject read-only keys.
  - Return NotSupported for unknown keys.

### 5.7 TriggerMessage
- Accept TriggerMessage for:
  - StatusNotification
  - MeterValues
  - Heartbeat
  - BootNotification

### 5.8 Availability & Reset
- `ChangeAvailability` sets connector to Unavailable/Available.
- If charging, return Scheduled and apply after StopTransaction.
- `Reset`:
  - Soft: reconnect and resend BootNotification, retain config.
  - Hard: reset runtime state to defaults (config persists).

### 5.9 Error & Edge Cases
- Handle CSMS disconnects mid-transaction; keep charging offline and
  report accumulated energy after reconnect.
- Authorize rejected: do not start transaction.
- SetChargingProfile limit 0 => SuspendedEVSE and zero power/current.

### 5.10 Web UI
- Live view of:
  - Connection state, current CP state, phase mode, power limits.
  - Latest MeterValues.
  - Last N OCPP messages (RX/TX).
- Manual controls:
  - EV plug/unplug.
  - Start/stop charging (local override).
  - Force phase mode (1/3 phase).
  - Toggle authorize-required behavior.
- Config editor with save/reload.

## 6. Non-Functional Requirements
- Runs in a Python venv on this machine.
- Logs to `logs/` with rotation (file size or daily).
- Deterministic behavior for repeatable tests (seeded RNG optional).
- Responsive UI (< 250 ms local interactions).

## 7. Configuration Model

YAML file in `config/default.yaml`:

```yaml
wallbox:
  charge_point_id: "CP001"
  vendor: "TestWallbox"
  model: "TWB-22"
  serial_number: "SIM001"
  firmware_version: "1.0.0"
  max_current_a: 32
  num_connectors: 1
  phase_mode: "3-phase"   # "1-phase" or "3-phase"
  authorize_required: true

ocpp:
  server_url: "ws://192.168.4.1:9000/ocpp/CP001"
  heartbeat_interval: 60
  meter_value_sample_interval: 10
  supported_rate_units: ["Current", "Power"]

metering:
  voltage_v: 230
  power_factor: 1.0
  start_energy_wh: 0

web_ui:
  host: "127.0.0.1"
  port: 8080
```

## 8. Interfaces

### 8.1 OCPP 1.6J
- WebSocket client with subprotocol `ocpp1.6`.
- JSON-RPC message framing per OCPP 1.6J.

### 8.2 Web UI
- HTTP server for UI pages.
- WebSocket for live updates (state + message feed).

## 9. Test Coverage Mapping

The CP must support all tests in `docs/OCPP-Test.md`, including:
- TC-010–TC-016: connection, configuration, rate units.
- TC-017–TC-021: negative configuration/profile cases.
- TC-100–TC-115: charging sessions and power limits.
- TC-200–TC-203: dynamic power / stress.
- TC-300–TC-302: remote control + auth.
- TC-400–TC-407: phase switching.
- TC-500–TC-504: error and edge cases.
- TC-600–TC-601: metering accuracy.

## 10. Acceptance Criteria
- CP passes all CSMS-driven test cases from `OCPP-Test.md`.
- Web UI shows correct state and live OCPP traffic.
- MeterValues and state transitions align with timing/tolerance rules.
- Reconnects properly after disconnects without losing transaction state.

