from __future__ import annotations

import asyncio
import contextlib
import logging
from typing import Any, Callable, Optional

import websockets

from .chargepoint import WallboxChargePoint, WallboxRuntime


class WallboxClient:
    def __init__(
        self,
        config: dict[str, Any],
        runtime: WallboxRuntime,
        on_cp_change: Optional[Callable[[Optional[WallboxChargePoint]], None]] = None,
        log_callback: Optional[Callable[[str, str, str], None]] = None,
    ) -> None:
        self.config = config
        self.runtime = runtime
        self.on_cp_change = on_cp_change
        self.log_callback = log_callback
        self.logger = logging.getLogger("wallbox.client")
        self._stop = asyncio.Event()
        self._reset_requested = asyncio.Event()
        self._connected = False

    def request_stop(self) -> None:
        self._stop.set()

    def request_reset(self, reset_type: str) -> None:
        self.logger.info("Reset requested: %s", reset_type)
        self._reset_requested.set()

    async def _heartbeat_loop(self, cp: WallboxChargePoint) -> None:
        while not self._stop.is_set():
            await asyncio.sleep(self.runtime.heartbeat_interval)
            await cp.send_heartbeat()

    async def _meter_loop(self, cp: WallboxChargePoint) -> None:
        while not self._stop.is_set():
            await asyncio.sleep(self.runtime.meter_interval)
            if self.runtime.transaction_id is not None:
                await cp.send_meter_values()

    async def _offline_meter_loop(self) -> None:
        last_ts = asyncio.get_event_loop().time()
        while not self._stop.is_set():
            await asyncio.sleep(self.runtime.meter_interval)
            now = asyncio.get_event_loop().time()
            delta = now - last_ts
            last_ts = now
            if self.runtime.transaction_id is not None:
                self.runtime.meter.advance(delta)

    async def _auto_plug_loop(self, cp: WallboxChargePoint) -> None:
        if not self.config.get("auto_plug"):
            return
        await asyncio.sleep(self.config.get("plug_in_delay_sec", 2))
        await cp.plug_in()

    async def run(self) -> None:
        offline_task = asyncio.create_task(self._offline_meter_loop())
        while not self._stop.is_set():
            try:
                async with websockets.connect(
                    self.config["ocpp_server"],
                    subprotocols=["ocpp1.6"],
                    ping_interval=20,
                    ping_timeout=20,
                ) as ws:
                    cp = WallboxChargePoint(
                        self.config["charge_point_id"],
                        ws,
                        self.runtime,
                        self.config,
                        reset_callback=self.request_reset,
                        log_callback=self.log_callback,
                    )
                    if self.log_callback:
                        self.log_callback("INFO", "WebSocket", "connected")
                    self._connected = True
                    if self.on_cp_change:
                        self.on_cp_change(cp)

                    start_task = asyncio.create_task(cp.start())
                    await asyncio.sleep(0)
                    await cp.send_boot_notification()
                    await cp.send_status_notification(0, self.runtime.connector.status)
                    await cp.send_status_notification(self.runtime.connector.connector_id, self.runtime.connector.status)

                    heartbeat_task = asyncio.create_task(self._heartbeat_loop(cp))
                    meter_task = asyncio.create_task(self._meter_loop(cp))
                    auto_plug_task = asyncio.create_task(self._auto_plug_loop(cp))

                    reset_task = asyncio.create_task(self._reset_requested.wait())
                    done, pending = await asyncio.wait(
                        [start_task, reset_task],
                        return_when=asyncio.FIRST_COMPLETED,
                    )

                    for task in (heartbeat_task, meter_task, auto_plug_task):
                        task.cancel()
                        with contextlib.suppress(asyncio.CancelledError):
                            await task

                    if self._reset_requested.is_set():
                        self._reset_requested.clear()
                        await ws.close()

                    for task in pending:
                        task.cancel()
                        with contextlib.suppress(asyncio.CancelledError):
                            await task

            except Exception as exc:  # pragma: no cover - network failure handling
                self.logger.warning("Connection error: %s", exc)
                if self.log_callback:
                    self.log_callback("INFO", "WebSocket", f"disconnected: {exc}")
                await asyncio.sleep(self.config.get("reconnect_delay_sec", 5))
            finally:
                self._connected = False
                if self.on_cp_change:
                    self.on_cp_change(None)

        offline_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await offline_task
