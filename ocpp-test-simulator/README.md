# OCPP Test Simulator

Python-based test harness for the ESP32 OCPP Server. Provides:
- **Wallbox Emulator**: OCPP 1.6J charge point that connects via WebSocket
- **MQTT Client**: Sends commands and receives status from the OCPP server
- **Test Scenarios**: Automated test sequences for validation

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     OCPP Test Simulator                              │
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
└─────────────┼────────────────────────────────────┼───────────────────┘
              │ WebSocket                          │ MQTT
              │ (Ethernet)                         │ (WiFi)
              ▼                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      ESP32 OCPP Server                               │
└─────────────────────────────────────────────────────────────────────┘
```

## Directory Structure

```
ocpp-test-simulator/
├── README.md
├── requirements.txt          # Python dependencies
├── pyproject.toml           # Project configuration
├── config/
│   ├── default.yaml         # Default configuration
│   └── test_profiles.yaml   # Test scenario profiles
├── src/
│   ├── __init__.py
│   ├── main.py              # CLI entry point
│   ├── wallbox_emulator/    # OCPP charge point simulation
│   │   ├── __init__.py
│   │   ├── chargepoint.py   # Main charge point class
│   │   ├── connector.py     # Connector state machine
│   │   ├── meter.py         # Meter value simulation
│   │   ├── ocpp_client.py   # WebSocket OCPP client
│   │   └── ev_simulator.py  # EV behavior simulation
│   ├── mqtt_client/         # MQTT test client
│   │   ├── __init__.py
│   │   ├── client.py        # MQTT connection handler
│   │   ├── commands.py      # Command publishers
│   │   └── subscribers.py   # Status subscribers
│   └── scenarios/           # Test scenarios
│       ├── __init__.py
│       ├── base.py          # Base scenario class
│       ├── basic_charge.py  # Simple charging test
│       ├── phase_switch.py  # Phase switching test
│       ├── power_limit.py   # Power limiting test
│       └── stress_test.py   # Load/stress testing
├── tests/                   # Unit tests
│   ├── __init__.py
│   ├── test_chargepoint.py
│   ├── test_mqtt_client.py
│   └── test_scenarios.py
└── logs/                    # Runtime logs
    └── .gitkeep
```

## Installation

```bash
cd ocpp-test-simulator
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

## Usage

### Run Wallbox Emulator Only
```bash
python -m src.main wallbox --server ws://192.168.4.1:9000/ocpp/CP001
```

### Run MQTT Client Only
```bash
python -m src.main mqtt --broker 192.168.1.100 --prefix ocpp/CP001
```

### Run Full Test Scenario
```bash
python -m src.main scenario basic_charge \
    --ocpp-server ws://192.168.4.1:9000/ocpp/CP001 \
    --mqtt-broker 192.168.1.100
```

### Interactive Mode
```bash
python -m src.main interactive
```

## Test Scenarios

| Scenario | Description |
|----------|-------------|
| `basic_charge` | Start/stop charging, verify meter values |
| `phase_switch` | Test 1-phase to 3-phase switching sequence |
| `power_limit` | Apply power limits, verify compliance |
| `remote_control` | Remote start/stop via MQTT |
| `stress_test` | Rapid commands, connection drops |
| `authorization` | RFID authorization flow |

## Configuration

See `config/default.yaml` for all options:

```yaml
wallbox:
  charge_point_id: "CP001"
  vendor: "TestVendor"
  model: "Simulator"
  max_current: 32
  num_connectors: 1

mqtt:
  broker: "localhost"
  port: 1883
  username: ""
  password: ""
  prefix: "ocpp"

simulation:
  meter_interval_sec: 10
  voltage: 230
  power_factor: 0.98
```
