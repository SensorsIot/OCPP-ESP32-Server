# OCPP Test Wallbox - Test Specification

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.0 |
| Status | Draft |
| Created | 2026-01-31 |
| Based on | evcc trace (evcc-20260129-230137-trace.log) |

## 1. Overview

This document defines the test cases for the OCPP Test Wallbox, a Python-based
charge point simulator that connects to the ESP32 OCPP Server (CSMS) via
WebSocket. The simulator reproduces real-world wallbox behavior as observed in
evcc OCPP traces.

### 1.1 Roles

| Role | Component | Protocol Side |
|------|-----------|---------------|
| **CSMS** (Central System) | ESP32 OCPP Server | Receives CP messages, sends commands |
| **CP** (Charge Point) | Test Wallbox (Python) | Sends status/meter data, executes commands |

### 1.2 Connection

```
Test Wallbox (CP)                          ESP32 (CSMS)
      |                                        |
      |--- WebSocket CONNECT ----------------->|
      |    ws://{host}:{port}/ocpp/{cpId}      |
      |    Sec-WebSocket-Protocol: ocpp1.6     |
      |                                        |
      |<-- 101 Switching Protocols ------------|
      |                                        |
```

## 2. OCPP Message Reference

### 2.1 CP -> CSMS (Charge Point initiates)

| Message | When | Key Fields |
|---------|------|------------|
| BootNotification | On connect | vendor, model, serialNumber, firmwareVersion |
| Heartbeat | Every `interval` seconds | (empty payload) |
| StatusNotification | On state change | connectorId, status, errorCode, timestamp |
| Authorize | Before transaction start | idTag |
| StartTransaction | Charging begins | connectorId, idTag, meterStart, timestamp |
| StopTransaction | Charging ends | transactionId, meterStop, timestamp, reason |
| MeterValues | Periodic during charging | connectorId, transactionId, meterValue[] |

### 2.2 CSMS -> CP (Central System initiates)

| Message | When | Key Fields |
|---------|------|------------|
| TriggerMessage | State sync / polling | requestedMessage |
| SetChargingProfile | Power control | connectorId, csChargingProfiles |
| GetCompositeSchedule | Verify active schedule | connectorId, duration |
| RemoteStartTransaction | Start charging | connectorId, idTag |
| RemoteStopTransaction | Stop charging | transactionId |
| ChangeConfiguration | Set CP config keys | key, value |
| GetConfiguration | Read CP config keys | key[] |
| ChangeAvailability | Enable/disable connector | connectorId, type |
| Reset | Soft/hard reset | type |

## 3. Charge Point Behavior Model

### 3.1 Connector States

```
                    BootNotification
                         |
                         v
     +-----------> Available <-----------+
     |                  |                |
     |            EV plugs in            |
     |                  |                |
     |                  v                |
     |             Preparing             |
     |                  |                |
     |          Authorize OK             |
     |                  |                |
     |                  v                |
     |         StartTransaction          |
     |                  |                |
     |                  v                |
     |             Charging ----------> SuspendedEV
     |                  |                    |
     |           StopTransaction             |
     |                  |                    |
     |                  v                    |
     |             Finishing <---------------+
     |                  |
     |            EV unplugs
     |                  |
     +------------------+
```

### 3.2 Meter Values Reported

The wallbox reports these measurands in MeterValues (as seen in evcc traces):

| Measurand | Unit | Context | Description |
|-----------|------|---------|-------------|
| Energy.Active.Import.Register | Wh | Sample.Periodic | Total energy delivered |
| Power.Active.Import | W | Sample.Periodic | Current charging power |
| Current.Import | A | Sample.Periodic | Current per phase |
| Voltage | V | Sample.Periodic | Voltage per phase |
| SoC | Percent | Sample.Periodic | State of charge (if EV reports) |

**Important**: The wallbox always reports values as if in 3-phase mode. When
operating in 1-phase mode, only L1 carries current, but some wallboxes still
report 3-phase-equivalent values. The CSMS applies the correction factor.

### 3.3 Charging Profile Response

When the CSMS sends SetChargingProfile, the CP:
1. Validates the profile (reject if invalid)
2. Stores it locally
3. Applies the current limit immediately
4. Adjusts actual charging current within ~5 seconds
5. MeterValues reflect the new power level

When the CSMS sends GetCompositeSchedule, the CP:
1. Computes the effective schedule from all stacked profiles
2. Returns the composite schedule for the requested duration

## 4. Test Cases

### 4.1 Connection and Registration

#### TC-010: Boot and Registration

**Purpose**: Verify basic OCPP connection handshake.

| Step | Direction | Message | Payload / Check |
|------|-----------|---------|-----------------|
| 1 | CP->CSMS | BootNotification | `vendor:"TestWallbox"`, `model:"TWB-22"`, `serialNumber:"SIM001"`, `firmwareVersion:"1.0.0"` |
| 2 | CSMS->CP | BootNotification.conf | `status:"Accepted"`, `interval:60`, `currentTime` set |
| 3 | CP->CSMS | StatusNotification | `connectorId:0`, `status:"Available"`, `errorCode:"NoError"` |
| 4 | CP->CSMS | StatusNotification | `connectorId:1`, `status:"Available"`, `errorCode:"NoError"` |

**Pass**: CSMS responds Accepted, CP transitions to Available.

#### TC-011: Heartbeat Keepalive

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Wait `interval` seconds after boot |
| 2 | CP->CSMS | Heartbeat | (empty payload) |
| 3 | CSMS->CP | Heartbeat.conf | `currentTime` is valid ISO 8601 |

**Pass**: Heartbeat exchanged at configured interval, time is valid.

#### TC-012: TriggerMessage (State Sync)

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | CSMS->CP | TriggerMessage | `requestedMessage:"StatusNotification"` |
| 2 | CP->CSMS | TriggerMessage.conf | `status:"Accepted"` |
| 3 | CP->CSMS | StatusNotification | Current connector status |

**Pass**: CP responds to trigger within 5 seconds.

#### TC-013: GetConfiguration

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | CSMS->CP | GetConfiguration | `key:[]` (all keys) |
| 2 | CP->CSMS | GetConfiguration.conf | Returns `configurationKey[]` with supported keys |

**Expected keys**: `MeterValuesSampledData`, `MeterValueSampleInterval`,
`HeartbeatInterval`, `NumberOfConnectors`, `ChargeProfileMaxStackLevel`.

#### TC-014: ChangeConfiguration

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | CSMS->CP | ChangeConfiguration | `key:"MeterValueSampleInterval"`, `value:"10"` |
| 2 | CP->CSMS | ChangeConfiguration.conf | `status:"Accepted"` |
| 3 | | | CP now reports MeterValues every 10 seconds |

---

### 4.2 Charging Sessions

#### TC-100: Basic Charge Cycle (3-phase, 16A, ~11 kW)

**Purpose**: Full charge cycle at nominal 3-phase residential power.

**Setup**: Phase mode = 3-phase, max current = 16A per phase.

| Step | Direction | Message | Payload / Check |
|------|-----------|---------|-----------------|
| 1 | | | CP in Available state |
| 2 | | | *Simulate EV plug-in* |
| 3 | CP->CSMS | StatusNotification | `connectorId:1`, `status:"Preparing"` |
| 4 | CSMS->CP | RemoteStartTransaction | `connectorId:1`, `idTag:"evcc"` |
| 5 | CP->CSMS | RemoteStartTransaction.conf | `status:"Accepted"` |
| 6 | CP->CSMS | Authorize | `idTag:"evcc"` |
| 7 | CSMS->CP | Authorize.conf | `idTagInfo.status:"Accepted"` |
| 8 | CP->CSMS | StartTransaction | `connectorId:1`, `idTag:"evcc"`, `meterStart:0` |
| 9 | CSMS->CP | StartTransaction.conf | `transactionId` assigned, `idTagInfo.status:"Accepted"` |
| 10 | CP->CSMS | StatusNotification | `connectorId:1`, `status:"Charging"` |
| 11 | CSMS->CP | SetChargingProfile | `limit:16.0`, `chargingRateUnit:"A"` |
| 12 | CP->CSMS | SetChargingProfile.conf | `status:"Accepted"` |
| 13 | CP->CSMS | MeterValues | `Power.Active.Import: ~11040W`, `Current.Import: ~16A`, `Voltage: ~230V` |
| 14 | | | *Repeat MeterValues every `interval` seconds* |
| 15 | | | *Simulate EV full / unplug* |
| 16 | CP->CSMS | StopTransaction | `transactionId`, `meterStop`, `reason:"EVDisconnected"` |
| 17 | CSMS->CP | StopTransaction.conf | `idTagInfo.status:"Accepted"` |
| 18 | CP->CSMS | StatusNotification | `connectorId:1`, `status:"Finishing"` |
| 19 | CP->CSMS | StatusNotification | `connectorId:1`, `status:"Available"` |

**Expected meter values (3-phase, 16A)**:
| Measurand | Value | Calculation |
|-----------|-------|-------------|
| Power.Active.Import | ~11,040 W | 3 x 230V x 16A |
| Current.Import (L1) | ~16 A | |
| Current.Import (L2) | ~16 A | |
| Current.Import (L3) | ~16 A | |
| Voltage (L1) | ~230 V | |
| Energy.Active.Import.Register | increasing | ~11 kWh/hour |

**Pass**: Full cycle completes, meter values within 5% of expected, all status
transitions correct.

#### TC-101: Basic Charge Cycle (1-phase, 16A, ~3.7 kW)

**Purpose**: Full charge cycle at 1-phase residential power.

**Setup**: Phase mode = 1-phase, max current = 16A.

Same flow as TC-100, but with:

| Step | Change from TC-100 |
|------|--------------------|
| 11 | SetChargingProfile: `limit:16.0` A |
| 13 | MeterValues differ (see below) |

**Expected meter values (1-phase, 16A)**:
| Measurand | Value | Notes |
|-----------|-------|-------|
| Power.Active.Import | ~11,040 W | Wallbox reports 3-phase equivalent |
| Current.Import (L1) | ~16 A | Only L1 active |
| Current.Import (L2) | 0 A | No current |
| Current.Import (L3) | 0 A | No current |
| Voltage (L1) | ~230 V | |

**CSMS correction** (applied by ESP32, not the wallbox):
| Measurand | Raw from wallbox | Corrected by CSMS | Factor |
|-----------|------------------|--------------------|--------|
| Power | ~11,040 W | ~3,680 W | / 3 |
| Energy | raw Wh | raw / 3 Wh | / 3 |

**Pass**: Wallbox reports raw values, CSMS publishes corrected values to MQTT.

#### TC-102: Charge at Reduced Current (3-phase, 8A, ~5.5 kW)

**Purpose**: Verify current limiting via SetChargingProfile.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1-10 | | | Same as TC-100 steps 1-10 |
| 11 | CSMS->CP | SetChargingProfile | `limit:8.0`, `chargingRateUnit:"A"` |
| 12 | CP->CSMS | SetChargingProfile.conf | `status:"Accepted"` |
| 13 | CP->CSMS | MeterValues | `Power.Active.Import: ~5520W`, `Current.Import: ~8A` |

**Expected**: Power drops from ~11 kW to ~5.5 kW within 10 seconds.

#### TC-103: Charge at Reduced Current (1-phase, 10A, ~2.3 kW)

**Setup**: Phase mode = 1-phase.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1-10 | | | Same as TC-101 steps 1-10 |
| 11 | CSMS->CP | SetChargingProfile | `limit:10.0`, `chargingRateUnit:"A"` |
| 12 | CP->CSMS | SetChargingProfile.conf | `status:"Accepted"` |
| 13 | CP->CSMS | MeterValues | `Power.Active.Import: ~6900W` (raw 3-phase equiv) |

**CSMS corrected**: ~2,300 W actual (6900 / 3).

#### TC-104: Charge at Maximum Current (3-phase, 32A, ~22 kW)

**Purpose**: Verify maximum power delivery.

Same flow as TC-100 with `limit:32.0`:

**Expected meter values (3-phase, 32A)**:
| Measurand | Value |
|-----------|-------|
| Power.Active.Import | ~22,080 W |
| Current.Import (each) | ~32 A |

#### TC-105: Charge at Maximum Current (1-phase, 32A, ~7.4 kW)

Same flow as TC-101 with `limit:32.0`:

| Measurand | Raw (wallbox) | Corrected (CSMS) |
|-----------|---------------|------------------|
| Power.Active.Import | ~22,080 W | ~7,360 W |
| Current.Import (L1) | ~32 A | ~32 A |

---

### 4.3 Dynamic Power Control

#### TC-200: Current Ramp Down During Charging

**Purpose**: Verify the wallbox responds to mid-session current changes.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1-12 | | | Start charging at 16A (TC-100 steps 1-12) |
| 13 | | | Verify MeterValues: ~16A, ~11 kW |
| 14 | CSMS->CP | SetChargingProfile | `limit:10.0` A |
| 15 | CP->CSMS | SetChargingProfile.conf | `status:"Accepted"` |
| 16 | CP->CSMS | MeterValues | `Current.Import: ~10A`, `Power: ~6900W` |
| 17 | CSMS->CP | SetChargingProfile | `limit:6.0` A |
| 18 | CP->CSMS | SetChargingProfile.conf | `status:"Accepted"` |
| 19 | CP->CSMS | MeterValues | `Current.Import: ~6A`, `Power: ~4140W` |

**Pass**: Each limit change reflected in MeterValues within 10 seconds.

#### TC-201: Current Ramp Up During Charging

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1-12 | | | Start charging at 6A |
| 13 | CSMS->CP | SetChargingProfile | `limit:10.0` A |
| 14 | | | MeterValues: ~10A |
| 15 | CSMS->CP | SetChargingProfile | `limit:16.0` A |
| 16 | | | MeterValues: ~16A |
| 17 | CSMS->CP | SetChargingProfile | `limit:32.0` A |
| 18 | | | MeterValues: ~32A |

#### TC-202: GetCompositeSchedule Verification

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging at 16A with TxDefaultProfile active |
| 2 | CSMS->CP | GetCompositeSchedule | `connectorId:1`, `duration:60` |
| 3 | CP->CSMS | GetCompositeSchedule.conf | `status:"Accepted"`, schedule shows `limit:16.0` |

#### TC-203: Rapid Limit Changes (Stress)

**Purpose**: Verify stability under rapid profile changes.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging in progress |
| 2 | CSMS->CP | SetChargingProfile | `limit:16.0` |
| 3 | CSMS->CP | SetChargingProfile | `limit:8.0` |
| 4 | CSMS->CP | SetChargingProfile | `limit:12.0` |
| 5 | CSMS->CP | SetChargingProfile | `limit:6.0` |
| 6 | CSMS->CP | SetChargingProfile | `limit:16.0` |
| 7 | | | Wait 10 seconds |
| 8 | CP->CSMS | MeterValues | Current reflects last limit (16A) |

**Pass**: No crash, no invalid state, final limit applied correctly.

---

### 4.4 Remote Start / Stop

#### TC-300: Remote Start Transaction

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | CP in Available, EV plugged in (Preparing) |
| 2 | CSMS->CP | RemoteStartTransaction | `connectorId:1`, `idTag:"evcc"` |
| 3 | CP->CSMS | RemoteStartTransaction.conf | `status:"Accepted"` |
| 4 | CP->CSMS | Authorize | `idTag:"evcc"` |
| 5 | CSMS->CP | Authorize.conf | `idTagInfo.status:"Accepted"` |
| 6 | CP->CSMS | StartTransaction | `connectorId:1`, `idTag:"evcc"`, `meterStart` |
| 7 | CSMS->CP | StartTransaction.conf | `transactionId` assigned |
| 8 | CP->CSMS | StatusNotification | `status:"Charging"` |

#### TC-301: Remote Stop Transaction

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging in progress, transactionId known |
| 2 | CSMS->CP | RemoteStopTransaction | `transactionId` |
| 3 | CP->CSMS | RemoteStopTransaction.conf | `status:"Accepted"` |
| 4 | CP->CSMS | StopTransaction | `transactionId`, `meterStop`, `reason:"Remote"` |
| 5 | CSMS->CP | StopTransaction.conf | accepted |
| 6 | CP->CSMS | StatusNotification | `status:"Finishing"` |
| 7 | CP->CSMS | StatusNotification | `status:"Available"` (after EV unplug) |

#### TC-302: Remote Start Without EV (Rejected)

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | CP in Available, no EV plugged in |
| 2 | CSMS->CP | RemoteStartTransaction | `connectorId:1`, `idTag:"evcc"` |
| 3 | CP->CSMS | RemoteStartTransaction.conf | `status:"Rejected"` (no EV) |

---

### 4.5 Phase Switching

#### TC-400: Phase Switch 3-phase to 1-phase (Mid-Session)

**Purpose**: Verify the CSMS-initiated phase switch sequence.

**Note**: Phase switching is controlled by the CSMS (ESP32), not the wallbox.
The wallbox just sees RemoteStop, then relay changes externally, then RemoteStart.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging at 3-phase, 16A (~11 kW) |
| 2 | | | *CSMS decides to switch to 1-phase* |
| 3 | CSMS->CP | RemoteStopTransaction | `transactionId` |
| 4 | CP->CSMS | RemoteStopTransaction.conf | `status:"Accepted"` |
| 5 | CP->CSMS | StopTransaction | `transactionId`, `meterStop`, `reason:"Remote"` |
| 6 | CP->CSMS | StatusNotification | `status:"Finishing"` |
| 7 | CP->CSMS | StatusNotification | `status:"Available"` |
| 8 | | | *CSMS waits safety delay (5s), switches relay* |
| 9 | CSMS->CP | RemoteStartTransaction | `connectorId:1`, `idTag:"evcc"` |
| 10 | CP->CSMS | Authorize + StartTransaction | New transaction |
| 11 | CP->CSMS | StatusNotification | `status:"Charging"` |
| 12 | CSMS->CP | SetChargingProfile | `limit:16.0` A |
| 13 | CP->CSMS | MeterValues | Now 1-phase: L1=16A, L2=0A, L3=0A |

**Pass**: Transaction stopped cleanly, new transaction started, meter values
reflect 1-phase operation.

#### TC-401: Phase Switch 1-phase to 3-phase (Mid-Session)

Same as TC-400 in reverse. After switch:

| Measurand | Before (1-phase) | After (3-phase) |
|-----------|-------------------|------------------|
| Current L1 | ~16 A | ~16 A |
| Current L2 | 0 A | ~16 A |
| Current L3 | 0 A | ~16 A |
| Power | ~3,680 W (corrected) | ~11,040 W |

#### TC-402: Phase Switch Without Active Transaction

**Purpose**: Verify phase switch when no charging session is active.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | CP in Available, no transaction, 3-phase mode |
| 2 | | | *CSMS receives power limit < 4.1 kW via MQTT* |
| 3 | | | *No RemoteStop needed (no active transaction)* |
| 4 | | | *CSMS verifies status is Available* |
| 5 | | | *CSMS waits safety delay (5s), switches relay to 1-phase* |
| 6 | | | *CSMS publishes phase result to MQTT* |
| 7 | CSMS->CP | TriggerMessage | `requestedMessage:"StatusNotification"` |
| 8 | CP->CSMS | StatusNotification | `status:"Available"` (unchanged) |

**Pass**: Relay switched without stop/start sequence, CP stays Available,
MQTT phase topic shows `phase_mode:"1-phase"`.

#### TC-403: Phase Switch Timeout (Wallbox Doesn't Stop)

**Purpose**: Verify safe abort when wallbox fails to reach Available state.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging at 3-phase, 16A |
| 2 | CSMS->CP | RemoteStopTransaction | `transactionId` |
| 3 | CP->CSMS | RemoteStopTransaction.conf | `status:"Accepted"` |
| 4 | CP->CSMS | StopTransaction | `transactionId`, `meterStop` |
| 5 | CP->CSMS | StatusNotification | `status:"Finishing"` |
| 6 | | | *CP stays in Finishing, never sends Available* |
| 7 | | | *30 second timeout expires* |
| 8 | | | *CSMS aborts phase switch* |

**Pass**: Relay NOT switched, CSMS publishes error to MQTT:
`{"success":false, "error":"timeout waiting for Available"}`.
Phase mode remains 3-phase. Transaction is NOT automatically restarted.

#### TC-404: Phase Switch Rejected (Already In Progress)

**Purpose**: Verify second switch request is rejected while one is active.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Phase switch 3→1 in progress (STOPPING state) |
| 2 | | | *MQTT command arrives: switch to 3-phase* |
| 3 | | | *CSMS rejects: ESP_ERR_INVALID_STATE* |

**Pass**: Second request rejected, first switch continues normally.

#### TC-405: Meter Continuity Across Phase Switch

**Purpose**: Verify no energy is lost or double-counted across the switch.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging at 3-phase, 16A, energy = 5000 Wh |
| 2 | | | *Phase switch 3→1 initiated* |
| 3 | CP->CSMS | StopTransaction | `meterStop: 5500` (Wh at stop) |
| 4 | | | *Relay switches, RemoteStart* |
| 5 | CP->CSMS | StartTransaction | `meterStart: 5500` (same as meterStop) |
| 6 | CP->CSMS | MeterValues | Energy continues incrementing from 5500 |

**Checks**:
- `meterStart` of new transaction == `meterStop` of old transaction
- No gap or overlap in energy accounting
- MQTT session message includes correct `energy_kwh` for each transaction

#### TC-406: Power Limit Change Immediately After Phase Switch

**Purpose**: Verify new current limit is applied right after restart.

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging at 3-phase, 16A (~11 kW) |
| 2 | | | *MQTT command: power_limit = 3.0 kW* |
| 3 | | | *CSMS determines: need 1-phase (< 4.1 kW threshold)* |
| 4 | | | *Phase switch sequence executes (TC-400 steps 3-11)* |
| 5 | CSMS->CP | SetChargingProfile | `limit:13.0` A (3000W / 230V) |
| 6 | CP->CSMS | SetChargingProfile.conf | `status:"Accepted"` |
| 7 | CP->CSMS | MeterValues | `Current.Import L1: ~13A`, L2=0, L3=0 |

**CSMS corrected power**: ~3,000 W (raw ~9,000 W / 3).

**Pass**: Correct current limit calculated for new phase mode and applied immediately.

#### TC-407: MQTT Phase Status and Correction Factor Update

**Purpose**: Verify MQTT publishes reflect phase mode change correctly.

| Step | Check | Topic | Expected |
|------|-------|-------|----------|
| 1 | Before switch (3-phase) | `ocpp/{id}/phase` | `phase_mode:"3-phase"`, `power_correction_factor:1.0` |
| 2 | During switch | `ocpp/{id}/phase` | `switching_in_progress:true`, `switch_state:"stopping"` |
| 3 | After switch (1-phase) | `ocpp/{id}/phase` | `phase_mode:"1-phase"`, `power_correction_factor:3.0` |
| 4 | Switch result | `ocpp/{id}/phase/result` | `success:true`, `old_mode:"3-phase"`, `new_mode:"1-phase"`, `transaction_stopped:{id}`, `transaction_started:{id}` |
| 5 | Meter values after switch | `ocpp/{id}/meter` | Power values divided by 3 compared to raw |

**Pass**: All MQTT topics updated, correction factor changes from 1.0 to 3.0,
published meter values use corrected power.

---

### 4.6 Error and Edge Cases

#### TC-500: WebSocket Disconnect During Charging

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging in progress |
| 2 | | | *Simulate WebSocket disconnect* |
| 3 | | | CP continues charging (offline mode) |
| 4 | | | *Reconnect after 10 seconds* |
| 5 | CP->CSMS | BootNotification | Re-registration |
| 6 | CSMS->CP | TriggerMessage | `requestedMessage:"StatusNotification"` |
| 7 | CP->CSMS | StatusNotification | `status:"Charging"` (still charging) |
| 8 | CP->CSMS | MeterValues | Energy accumulated during disconnect |

**Pass**: No data loss, charging continued, meter values account for offline period.

#### TC-501: EV Disconnect During Charging (Unexpected)

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging in progress |
| 2 | | | *EV cable pulled / EV disconnects* |
| 3 | CP->CSMS | StatusNotification | `status:"SuspendedEV"` or `status:"Finishing"` |
| 4 | CP->CSMS | StopTransaction | `reason:"EVDisconnected"` |
| 5 | CP->CSMS | StatusNotification | `status:"Available"` |

#### TC-502: Authorize Rejected

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | CP->CSMS | Authorize | `idTag:"UNKNOWN_TAG"` |
| 2 | CSMS->CP | Authorize.conf | `idTagInfo.status:"Invalid"` |
| 3 | | | CP does NOT start transaction |
| 4 | CP->CSMS | StatusNotification | Remains `Preparing` or returns to `Available` |

#### TC-503: SetChargingProfile Below Minimum

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | Charging in progress at 16A |
| 2 | CSMS->CP | SetChargingProfile | `limit:0.0` A |
| 3 | CP->CSMS | SetChargingProfile.conf | `status:"Accepted"` |
| 4 | CP->CSMS | StatusNotification | `status:"SuspendedEVSE"` (no power offered) |
| 5 | CP->CSMS | MeterValues | Power: 0 W, Current: 0 A |

**Pass**: Wallbox suspends charging, does not fault.

#### TC-504: ChangeAvailability to Inoperative

| Step | Direction | Message | Check |
|------|-----------|---------|-------|
| 1 | | | CP in Available state |
| 2 | CSMS->CP | ChangeAvailability | `connectorId:1`, `type:"Inoperative"` |
| 3 | CP->CSMS | ChangeAvailability.conf | `status:"Accepted"` |
| 4 | CP->CSMS | StatusNotification | `status:"Unavailable"` |
| 5 | | | CP rejects any RemoteStartTransaction |
| 6 | CSMS->CP | ChangeAvailability | `connectorId:1`, `type:"Operative"` |
| 7 | CP->CSMS | StatusNotification | `status:"Available"` |

---

### 4.7 Metering Accuracy

#### TC-600: Energy Accumulation Over Time

**Purpose**: Verify energy meter increments correctly.

| Phase Mode | Current | Duration | Expected Energy |
|------------|---------|----------|-----------------|
| 3-phase | 16 A | 1 hour | ~11.04 kWh |
| 3-phase | 32 A | 1 hour | ~22.08 kWh |
| 1-phase | 16 A | 1 hour | ~3.68 kWh (corrected) |
| 1-phase | 32 A | 1 hour | ~7.36 kWh (corrected) |

**Check**: `Energy.Active.Import.Register` increments = `Power.Active.Import` x
elapsed_seconds / 3600, within 2% tolerance.

#### TC-601: Meter Values Match Transaction Boundaries

| Check | Condition |
|-------|-----------|
| `StartTransaction.meterStart` | Matches last `Energy.Active.Import.Register` before start |
| `StopTransaction.meterStop` | Matches last `Energy.Active.Import.Register` at stop |
| `meterStop - meterStart` | Equals total energy delivered during transaction |

---

## 5. Power Reference Tables

### 5.1 3-Phase Power at Various Currents

| Current (A) | Power (W) | Power (kW) | Calculation |
|-------------|-----------|------------|-------------|
| 6 | 4,140 | 4.1 | 3 x 230 x 6 |
| 8 | 5,520 | 5.5 | 3 x 230 x 8 |
| 10 | 6,900 | 6.9 | 3 x 230 x 10 |
| 12 | 8,280 | 8.3 | 3 x 230 x 12 |
| 14 | 9,660 | 9.7 | 3 x 230 x 14 |
| 16 | 11,040 | 11.0 | 3 x 230 x 16 |
| 20 | 13,800 | 13.8 | 3 x 230 x 20 |
| 24 | 16,560 | 16.6 | 3 x 230 x 24 |
| 32 | 22,080 | 22.1 | 3 x 230 x 32 |

### 5.2 1-Phase Power at Various Currents

| Current (A) | Raw Wallbox (W) | CSMS Corrected (W) | Corrected (kW) |
|-------------|-----------------|---------------------|----------------|
| 6 | 4,140 | 1,380 | 1.4 |
| 8 | 5,520 | 1,840 | 1.8 |
| 10 | 6,900 | 2,300 | 2.3 |
| 16 | 11,040 | 3,680 | 3.7 |
| 32 | 22,080 | 7,360 | 7.4 |

### 5.3 Phase Switch Threshold

| Current Power | Target Phase | Reason |
|---------------|-------------|--------|
| < 4.1 kW | 1-phase | Single phase sufficient |
| >= 4.1 kW | 3-phase | Requires multi-phase |

---

## 6. Simulator Configuration

```yaml
wallbox:
  charge_point_id: "CP001"
  vendor: "TestWallbox"
  model: "TWB-22"
  serial_number: "SIM001"
  firmware_version: "1.0.0"
  max_current_a: 32
  num_connectors: 1
  supported_measurands:
    - Energy.Active.Import.Register
    - Power.Active.Import
    - Current.Import
    - Voltage
  configuration_keys:
    MeterValueSampleInterval: "10"
    MeterValuesSampledData: "Energy.Active.Import.Register,Power.Active.Import,Current.Import,Voltage"
    HeartbeatInterval: "60"
    NumberOfConnectors: "1"
    ChargeProfileMaxStackLevel: "3"
```

## 7. Test Execution Order

| Priority | Test Cases | Dependency |
|----------|------------|------------|
| 1 | TC-010, TC-011 | None (connection basics) |
| 2 | TC-012, TC-013, TC-014 | TC-010 (connected) |
| 3 | TC-100, TC-101 | TC-010 (full charge cycles) |
| 4 | TC-300, TC-301 | TC-100 (remote control) |
| 5 | TC-200, TC-201, TC-202 | TC-100 (dynamic power) |
| 6 | TC-102, TC-103, TC-104, TC-105 | TC-200 (power variants) |
| 7 | TC-400, TC-401, TC-402 | TC-300 (phase switching basics) |
| 7b | TC-405, TC-406, TC-407 | TC-400 (phase switch details) |
| 7c | TC-403, TC-404 | TC-400 (phase switch error cases) |
| 8 | TC-500, TC-501, TC-502, TC-503, TC-504 | TC-100 (error cases) |
| 9 | TC-600, TC-601 | TC-100 (metering accuracy) |
| 10 | TC-203 | TC-200 (stress) |
