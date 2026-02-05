from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml


@dataclass
class WallboxConfig:
    charge_point_id: str = "CP001"
    vendor: str = "TestWallbox"
    model: str = "TWB-22"
    serial_number: str = "SIM001"
    firmware_version: str = "1.0.0"
    max_current_a: float = 32.0
    num_connectors: int = 1

    ocpp_server: str = "ws://127.0.0.1:9000/ocpp/CP001"
    heartbeat_interval_sec: int = 60
    reconnect_delay_sec: int = 5

    phase_mode: str = "3-phase"  # "1-phase" or "3-phase"
    authorize_required: bool = True
    auto_plug: bool = False


@dataclass
class MqttConfig:
    broker: str = "localhost"
    port: int = 1883
    username: str = ""
    password: str = ""
    client_id: str = "ocpp-test-wallbox"
    topic_prefix: str = "ocpp"
    qos: int = 1


@dataclass
class SimulationConfig:
    meter_interval_sec: int = 10
    voltage_v: float = 230.0
    power_factor: float = 1.0

    ev_battery_kwh: float = 60.0
    ev_initial_soc_percent: float = 20.0
    ev_target_soc_percent: float = 80.0
    ev_max_charge_rate_kw: float = 11.0

    plug_in_delay_sec: int = 2
    charge_start_delay_sec: int = 1


@dataclass
class WebUiConfig:
    host: str = "0.0.0.0"
    port: int = 8080
    enabled: bool = True


@dataclass
class LoggingConfig:
    level: str = "INFO"
    file: str = "logs/simulator.log"
    console: bool = True


@dataclass
class AppConfig:
    wallbox: WallboxConfig = field(default_factory=WallboxConfig)
    mqtt: MqttConfig = field(default_factory=MqttConfig)
    simulation: SimulationConfig = field(default_factory=SimulationConfig)
    web_ui: WebUiConfig = field(default_factory=WebUiConfig)
    logging: LoggingConfig = field(default_factory=LoggingConfig)


def _merge_dataclass(default_obj: Any, data: dict[str, Any]) -> Any:
    for key, value in data.items():
        if hasattr(default_obj, key):
            setattr(default_obj, key, value)
    return default_obj


def load_config(path: str | Path) -> AppConfig:
    path = Path(path)
    if not path.exists():
        raise FileNotFoundError(f"Config file not found: {path}")
    raw = yaml.safe_load(path.read_text()) or {}

    config = AppConfig()
    config.wallbox = _merge_dataclass(config.wallbox, raw.get("wallbox", {}))
    config.mqtt = _merge_dataclass(config.mqtt, raw.get("mqtt", {}))
    config.simulation = _merge_dataclass(config.simulation, raw.get("simulation", {}))
    config.web_ui = _merge_dataclass(config.web_ui, raw.get("web_ui", {}))
    config.logging = _merge_dataclass(config.logging, raw.get("logging", {}))

    return config
