from __future__ import annotations

import asyncio
import logging
from pathlib import Path

import click

from .config import AppConfig, load_config
from .service import WallboxService
from .wallbox_emulator.chargepoint import WallboxRuntime
from .wallbox_emulator.connector import ConnectorState
from .wallbox_emulator.ev_simulator import EvState
from .wallbox_emulator.meter import MeterState
from .wallbox_emulator.ocpp_client import WallboxClient
from .web_ui import WebUiServer


def setup_logging(config: AppConfig) -> None:
    handlers: list[logging.Handler] = []
    log_path = Path(config.logging.file)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    file_handler = logging.FileHandler(log_path)
    handlers.append(file_handler)

    if config.logging.console:
        handlers.append(logging.StreamHandler())

    logging.basicConfig(
        level=getattr(logging, config.logging.level.upper(), logging.INFO),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        handlers=handlers,
    )


async def run_wallbox(config_path: str) -> None:
    config = load_config(config_path)
    await run_wallbox_with_config(config)


@click.group()
def cli() -> None:
    """OCPP Wallbox Tester."""


@cli.command("run")
@click.option("--config", "config_path", default="config/default.yaml", show_default=True)
@click.option("--web-host", default=None)
@click.option("--web-port", type=int, default=None)
def run_command(config_path: str, web_host: str | None, web_port: int | None) -> None:
    """Run the wallbox tester."""
    if web_host or web_port:
        config = load_config(config_path)
        if web_host:
            config.web_ui.host = web_host
        if web_port:
            config.web_ui.port = web_port
        setup_logging(config)
        asyncio.run(run_wallbox_with_config(config))
    else:
        asyncio.run(run_wallbox(config_path))


async def run_wallbox_with_config(config: AppConfig) -> None:
    setup_logging(config)

    runtime = WallboxRuntime(
        connector=ConnectorState(connector_id=1),
        meter=MeterState(
            voltage_v=config.simulation.voltage_v,
            power_factor=config.simulation.power_factor,
            phase_mode=config.wallbox.phase_mode,
        ),
        ev=EvState(),
        authorize_required=config.wallbox.authorize_required,
        phase_mode=config.wallbox.phase_mode,
        heartbeat_interval=config.wallbox.heartbeat_interval_sec,
        meter_interval=config.simulation.meter_interval_sec,
    )

    wallbox_config = {
        "charge_point_id": config.wallbox.charge_point_id,
        "vendor": config.wallbox.vendor,
        "model": config.wallbox.model,
        "serial_number": config.wallbox.serial_number,
        "firmware_version": config.wallbox.firmware_version,
        "max_current_a": config.wallbox.max_current_a,
        "num_connectors": config.wallbox.num_connectors,
        "ocpp_server": config.wallbox.ocpp_server,
        "heartbeat_interval": config.wallbox.heartbeat_interval_sec,
        "reconnect_delay_sec": config.wallbox.reconnect_delay_sec,
        "supported_rate_units": ["Current", "Power"],
        "auto_plug": config.wallbox.auto_plug,
        "plug_in_delay_sec": config.simulation.plug_in_delay_sec,
        "StopTransactionOnInvalidId": "true",
        "StopTransactionOnEVSideDisconnect": "true",
    }

    service = WallboxService(config, runtime)
    client = WallboxClient(
        wallbox_config,
        runtime,
        on_cp_change=service.set_charge_point,
        log_callback=service.record_event,
    )

    if config.web_ui.enabled:
        ui = WebUiServer(service, host=config.web_ui.host, port=config.web_ui.port)
        await ui.start()

    await client.run()


if __name__ == "__main__":
    cli()
