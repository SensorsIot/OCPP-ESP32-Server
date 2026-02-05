from __future__ import annotations

import asyncio
from collections import deque
from datetime import datetime, timezone
from typing import Any, Deque, Optional

from ocpp.v16.enums import ChargePointStatus

from .config import AppConfig
from .wallbox_emulator.chargepoint import WallboxChargePoint, WallboxRuntime


class WallboxService:
    def __init__(self, config: AppConfig, runtime: WallboxRuntime) -> None:
        self.config = config
        self.runtime = runtime
        self._cp: Optional[WallboxChargePoint] = None
        self._lock = asyncio.Lock()
        self._logs: Deque[dict[str, Any]] = deque(maxlen=200)

    def set_charge_point(self, cp: Optional[WallboxChargePoint]) -> None:
        self._cp = cp

    def get_state(self) -> dict[str, Any]:
        return {
            "connected": self._cp is not None,
            "connector_status": self.runtime.connector.status.value,
            "transaction_id": self.runtime.transaction_id,
            "phase_mode": self.runtime.phase_mode,
            "authorize_required": self.runtime.authorize_required,
            "current_limit_a": self.runtime.meter.current_limit_a,
            "energy_wh": round(self.runtime.meter.energy_wh, 1),
            "power_w": round(self.runtime.meter.instantaneous_power_w(), 1),
            "logs": list(self._logs),
        }

    def record_event(self, direction: str, action: str, detail: str) -> None:
        self._logs.append(
            {
                "ts": datetime.now(timezone.utc).isoformat(),
                "dir": direction,
                "action": action,
                "detail": detail,
            }
        )

    async def plug_in(self) -> None:
        async with self._lock:
            if self._cp:
                await self._cp.plug_in()
            else:
                self.runtime.connector.set_status(ChargePointStatus.preparing)

    async def unplug(self) -> None:
        async with self._lock:
            if self._cp:
                await self._cp.unplug()
            else:
                self.runtime.connector.set_status(ChargePointStatus.available)

    async def start_transaction(self, id_tag: str) -> None:
        async with self._lock:
            if self._cp:
                await self._cp.start_transaction(id_tag)

    async def stop_transaction(self, reason: str = "Remote") -> None:
        async with self._lock:
            if self._cp:
                await self._cp.stop_transaction(reason=reason)

    async def set_phase_mode(self, mode: str) -> None:
        async with self._lock:
            if self._cp:
                await self._cp.set_phase_mode(mode)
            else:
                self.runtime.phase_mode = mode
                self.runtime.meter.set_phase_mode(mode)

    async def set_authorize_required(self, enabled: bool) -> None:
        async with self._lock:
            if self._cp:
                await self._cp.set_authorize_required(enabled)
            else:
                self.runtime.authorize_required = enabled
