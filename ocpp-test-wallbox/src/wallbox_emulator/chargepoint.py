from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Callable, Dict, Optional

from ocpp.routing import on
from ocpp.v16 import ChargePoint
from ocpp.v16 import call, call_result
from ocpp.v16.enums import Action, ChargePointStatus

from .connector import ConnectorState
from .meter import MeterState
from .ev_simulator import EvState
from ..testing import (
    EventLog,
    MessageReceived,
    MessageSent,
    StateChange,
    MeterUpdate,
    ProfileApplied,
    TransactionStarted,
    TransactionStopped,
    AuthorizationEvent,
    ConnectionEvent,
)

ResetCallback = Callable[[str], None]
LogCallback = Callable[[str, str, str], None]


@dataclass
class WallboxRuntime:
    connector: ConnectorState
    meter: MeterState
    ev: EvState
    authorize_required: bool = True
    phase_mode: str = "3-phase"
    heartbeat_interval: int = 60
    meter_interval: int = 10
    transaction_id: Optional[int] = None
    current_id_tag: str = ""
    events: EventLog = field(default_factory=EventLog)


class WallboxChargePoint(ChargePoint):
    def __init__(
        self,
        charge_point_id: str,
        connection: Any,
        runtime: WallboxRuntime,
        config: Dict[str, Any],
        reset_callback: Optional[ResetCallback] = None,
        log_callback: Optional[LogCallback] = None,
    ) -> None:
        super().__init__(charge_point_id, connection)
        self.runtime = runtime
        self.config = config
        self.reset_callback = reset_callback
        self.log_callback = log_callback
        self.logger = logging.getLogger("wallbox.chargepoint")
        self._state_lock = asyncio.Lock()

    async def send_boot_notification(self) -> None:
        payload = call.BootNotification(
            charge_point_vendor=self.config["vendor"],
            charge_point_model=self.config["model"],
            charge_point_serial_number=self.config["serial_number"],
            firmware_version=self.config["firmware_version"],
        )
        self._log("TX", "BootNotification", f"{self.config['vendor']} {self.config['model']}")
        response = await self.call(payload)
        if response.status == "Accepted":
            self.runtime.heartbeat_interval = int(response.interval)
        self.logger.info("BootNotification response: %s", response)

        # Emit message sent event
        await self.runtime.events.add(MessageSent(
            action="BootNotification",
            payload={
                "vendor": self.config["vendor"],
                "model": self.config["model"],
                "serial_number": self.config["serial_number"],
                "firmware_version": self.config["firmware_version"],
            },
            response={"status": response.status, "interval": response.interval}
        ))

    async def send_status_notification(self, connector_id: int, status: ChargePointStatus) -> None:
        payload_dict = {
            "connector_id": connector_id,
            "status": status.value,
            "error_code": "NoError",
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        payload = call.StatusNotification(
            connector_id=connector_id,
            status=status,
            error_code="NoError",
            timestamp=payload_dict["timestamp"],
        )
        self._log("TX", "StatusNotification", f"connector={connector_id} status={status.value}")
        response = await self.call(payload)

        # Emit event for test framework
        await self.runtime.events.add(MessageSent(
            action="StatusNotification",
            payload=payload_dict,
            response={}
        ))

    async def send_heartbeat(self) -> None:
        self._log("TX", "Heartbeat", "interval")
        response = await self.call(call.Heartbeat())

        # Emit message sent event
        await self.runtime.events.add(MessageSent(
            action="Heartbeat",
            payload={},
            response={"currentTime": response.current_time if hasattr(response, 'current_time') else None}
        ))

    async def send_meter_values(self) -> None:
        if self.runtime.transaction_id is None:
            return

        sampled = self.runtime.meter.sampled_values()
        payload = call.MeterValues(
            connector_id=self.runtime.connector.connector_id,
            transaction_id=self.runtime.transaction_id,
            meter_value=[
                {
                    "timestamp": datetime.now(timezone.utc).isoformat(),
                    "sampledValue": sampled,
                }
            ],
        )
        self._log("TX", "MeterValues", f"tx={self.runtime.transaction_id}")
        await self.call(payload)

        # Emit meter update event for test framework
        await self.runtime.events.add(MeterUpdate(
            power_w=self.runtime.meter.power_w,
            energy_wh=self.runtime.meter.energy_wh,
            current_l1_a=self.runtime.meter.current_l1,
            current_l2_a=self.runtime.meter.current_l2,
            current_l3_a=self.runtime.meter.current_l3,
            voltage_v=self.runtime.meter.voltage_v,
        ))

    async def authorize(self, id_tag: str) -> bool:
        self._log("TX", "Authorize", f"id_tag={id_tag}")
        response = await self.call(call.Authorize(id_tag=id_tag))
        status = response.id_tag_info["status"]
        self.logger.info("Authorize response: %s", status)

        # Emit authorization event
        await self.runtime.events.add(AuthorizationEvent(
            id_tag=id_tag,
            status=status
        ))
        await self.runtime.events.add(MessageSent(
            action="Authorize",
            payload={"id_tag": id_tag},
            response={"status": status}
        ))

        return status == "Accepted"

    async def start_transaction(self, id_tag: str) -> None:
        async with self._state_lock:
            if self.runtime.transaction_id is not None:
                return

        if self.runtime.authorize_required:
            ok = await self.authorize(id_tag)
            if not ok:
                return

        meter_start = int(self.runtime.meter.energy_wh)
        payload = call.StartTransaction(
            connector_id=self.runtime.connector.connector_id,
            id_tag=id_tag,
            meter_start=meter_start,
            timestamp=datetime.now(timezone.utc).isoformat(),
        )
        self._log("TX", "StartTransaction", f"id_tag={id_tag} meter_start={meter_start}")
        response = await self.call(payload)
        if response.id_tag_info["status"] != "Accepted":
            return

        async with self._state_lock:
            self.runtime.transaction_id = int(response.transaction_id)
            self.runtime.current_id_tag = id_tag

        # Emit transaction started event
        await self.runtime.events.add(TransactionStarted(
            transaction_id=self.runtime.transaction_id,
            connector_id=self.runtime.connector.connector_id,
            id_tag=id_tag,
            meter_start=meter_start,
        ))
        await self.runtime.events.add(MessageSent(
            action="StartTransaction",
            payload={"connector_id": self.runtime.connector.connector_id, "id_tag": id_tag, "meter_start": meter_start},
            response={"transaction_id": self.runtime.transaction_id, "status": "Accepted"}
        ))

        # Emit state change event
        old_status = self.runtime.connector.status.value if hasattr(self.runtime.connector.status, 'value') else str(self.runtime.connector.status)
        await self.runtime.events.add(StateChange(
            connector_id=self.runtime.connector.connector_id,
            old_status=old_status,
            new_status="Charging",
        ))

        self.runtime.connector.set_status(ChargePointStatus.charging)
        await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.charging)

    async def stop_transaction(self, reason: str = "Remote") -> None:
        async with self._state_lock:
            tx_id = self.runtime.transaction_id

        if tx_id is None:
            return

        meter_stop = int(self.runtime.meter.energy_wh)
        payload = call.StopTransaction(
            transaction_id=tx_id,
            meter_stop=meter_stop,
            timestamp=datetime.now(timezone.utc).isoformat(),
            reason=reason,
        )
        self._log("TX", "StopTransaction", f"tx={tx_id} reason={reason}")
        await self.call(payload)

        # Emit transaction stopped event
        await self.runtime.events.add(TransactionStopped(
            transaction_id=tx_id,
            connector_id=self.runtime.connector.connector_id,
            meter_stop=meter_stop,
            reason=reason,
        ))
        await self.runtime.events.add(MessageSent(
            action="StopTransaction",
            payload={"transaction_id": tx_id, "meter_stop": meter_stop, "reason": reason},
            response={}
        ))

        async with self._state_lock:
            self.runtime.transaction_id = None

        # Emit state change events
        await self.runtime.events.add(StateChange(
            connector_id=self.runtime.connector.connector_id,
            old_status="Charging",
            new_status="Finishing",
        ))
        self.runtime.connector.set_status(ChargePointStatus.finishing)
        await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.finishing)

        await self.runtime.events.add(StateChange(
            connector_id=self.runtime.connector.connector_id,
            old_status="Finishing",
            new_status="Available",
        ))
        self.runtime.connector.set_status(ChargePointStatus.available)
        await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.available)

        if self.runtime.connector.pending_availability:
            await self._apply_pending_availability()

    async def plug_in(self) -> None:
        old_status = self.runtime.connector.status.value if hasattr(self.runtime.connector.status, 'value') else str(self.runtime.connector.status)
        self.runtime.ev.plug_in()
        self.runtime.connector.set_status(ChargePointStatus.preparing)

        # Emit state change event
        await self.runtime.events.add(StateChange(
            connector_id=self.runtime.connector.connector_id,
            old_status=old_status,
            new_status="Preparing",
        ))

        await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.preparing)

    async def unplug(self) -> None:
        old_status = self.runtime.connector.status.value if hasattr(self.runtime.connector.status, 'value') else str(self.runtime.connector.status)
        self.runtime.ev.unplug()
        if self.runtime.transaction_id is not None:
            await self.stop_transaction(reason="EVDisconnected")
        else:
            self.runtime.connector.set_status(ChargePointStatus.available)

            # Emit state change event
            await self.runtime.events.add(StateChange(
                connector_id=self.runtime.connector.connector_id,
                old_status=old_status,
                new_status="Available",
            ))

            await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.available)

    async def set_phase_mode(self, mode: str) -> None:
        self.runtime.phase_mode = mode
        self.runtime.meter.set_phase_mode(mode)

    async def set_authorize_required(self, enabled: bool) -> None:
        self.runtime.authorize_required = enabled

    async def _apply_pending_availability(self) -> None:
        availability = self.runtime.connector.pending_availability
        self.runtime.connector.pending_availability = None
        if availability == "Inoperative":
            self.runtime.connector.set_status(ChargePointStatus.unavailable)
            await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.unavailable)
        else:
            self.runtime.connector.set_status(ChargePointStatus.available)
            await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.available)

    @on(Action.remote_start_transaction)
    async def on_remote_start_transaction(self, connector_id: int, id_tag: str, **kwargs: Any) -> Dict[str, str]:
        self._log("RX", "RemoteStartTransaction", f"connector={connector_id} id_tag={id_tag}")

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="RemoteStartTransaction",
            payload={"connector_id": connector_id, "id_tag": id_tag}
        ))

        if self.runtime.connector.status in (ChargePointStatus.unavailable, ChargePointStatus.faulted):
            return call_result.RemoteStartTransaction(status="Rejected")

        async def _start() -> None:
            if not self.runtime.ev.plugged_in:
                await self.plug_in()
            await asyncio.sleep(0.1)
            await self.start_transaction(id_tag)

        asyncio.create_task(_start())
        return call_result.RemoteStartTransaction(status="Accepted")

    @on(Action.remote_stop_transaction)
    async def on_remote_stop_transaction(self, transaction_id: int, **kwargs: Any) -> Dict[str, str]:
        self._log("RX", "RemoteStopTransaction", f"tx={transaction_id}")

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="RemoteStopTransaction",
            payload={"transaction_id": transaction_id}
        ))

        if self.runtime.transaction_id != transaction_id:
            return call_result.RemoteStopTransaction(status="Rejected")
        asyncio.create_task(self.stop_transaction(reason="Remote"))
        return call_result.RemoteStopTransaction(status="Accepted")

    @on(Action.set_charging_profile)
    async def on_set_charging_profile(self, connector_id: int, cs_charging_profiles: Dict[str, Any]) -> Dict[str, str]:
        schedule = cs_charging_profiles.get("chargingSchedule", {})
        rate_unit = schedule.get("chargingRateUnit", "A")
        periods = schedule.get("chargingSchedulePeriod", [])

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="SetChargingProfile",
            payload={"connector_id": connector_id, "cs_charging_profiles": cs_charging_profiles}
        ))

        if not periods:
            return call_result.SetChargingProfile(status="Rejected")

        limit = float(periods[0].get("limit", 0.0))
        profile_id = cs_charging_profiles.get("chargingProfileId", 0)
        self._log("RX", "SetChargingProfile", f"unit={rate_unit} limit={limit}")

        supported = self.config["supported_rate_units"]
        if rate_unit == "W" and "Power" not in supported:
            return call_result.SetChargingProfile(status="NotSupported")
        if rate_unit == "A" and "Current" not in supported:
            return call_result.SetChargingProfile(status="NotSupported")

        if rate_unit == "W":
            if self.runtime.phase_mode == "1-phase":
                current = limit / (self.runtime.meter.voltage_v * self.runtime.meter.power_factor)
            else:
                current = limit / (3.0 * self.runtime.meter.voltage_v * self.runtime.meter.power_factor)
        else:
            current = limit

        if current > float(self.config["max_current_a"]):
            return call_result.SetChargingProfile(status="Rejected")

        self.runtime.meter.set_current_limit(current)

        # Emit profile applied event
        await self.runtime.events.add(ProfileApplied(
            profile_id=profile_id,
            limit_a=current if rate_unit == "A" else None,
            limit_w=limit if rate_unit == "W" else None,
            rate_unit=rate_unit,
        ))

        old_status = self.runtime.connector.status.value if hasattr(self.runtime.connector.status, 'value') else str(self.runtime.connector.status)
        if limit <= 0.0:
            self.runtime.connector.set_status(ChargePointStatus.suspended_evse)
            await self.runtime.events.add(StateChange(
                connector_id=self.runtime.connector.connector_id,
                old_status=old_status,
                new_status="SuspendedEVSE",
            ))
            await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.suspended_evse)
        elif self.runtime.transaction_id is not None:
            self.runtime.connector.set_status(ChargePointStatus.charging)
            if old_status != "Charging":
                await self.runtime.events.add(StateChange(
                    connector_id=self.runtime.connector.connector_id,
                    old_status=old_status,
                    new_status="Charging",
                ))
            await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.charging)

        return call_result.SetChargingProfile(status="Accepted")

    @on(Action.get_composite_schedule)
    async def on_get_composite_schedule(self, connector_id: int, duration: int, **kwargs: Any) -> Dict[str, Any]:
        self._log("RX", "GetCompositeSchedule", f"duration={duration}")

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="GetCompositeSchedule",
            payload={"connector_id": connector_id, "duration": duration}
        ))

        rate_unit = "A" if "Current" in self.config["supported_rate_units"] else "W"
        current = self.runtime.meter.current_limit_a
        if rate_unit == "W":
            if self.runtime.phase_mode == "1-phase":
                limit = current * self.runtime.meter.voltage_v * self.runtime.meter.power_factor
            else:
                limit = 3.0 * current * self.runtime.meter.voltage_v * self.runtime.meter.power_factor
        else:
            limit = current

        return call_result.GetCompositeSchedule(
            status="Accepted",
            connector_id=connector_id,
            schedule_start=datetime.now(timezone.utc).isoformat(),
            charging_schedule={
                "chargingRateUnit": rate_unit,
                "chargingSchedulePeriod": [{"startPeriod": 0, "limit": round(limit, 1)}],
            },
        )

    @on(Action.change_configuration)
    async def on_change_configuration(self, key: str, value: str, **kwargs: Any) -> Dict[str, str]:
        self._log("RX", "ChangeConfiguration", f"{key}={value}")

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="ChangeConfiguration",
            payload={"key": key, "value": value}
        ))

        read_only_keys = {
            "NumberOfConnectors",
            "ConnectorPhaseRotation",
            "ChargingScheduleAllowedChargingRateUnit",
            "ChargeProfileMaxStackLevel",
            "MaxChargingProfilesInstalled",
            "ChargingScheduleMaxPeriods",
        }
        if key in read_only_keys:
            return call_result.ChangeConfiguration(status="Rejected")

        if key == "HeartbeatInterval":
            if not value.isdigit():
                return call_result.ChangeConfiguration(status="Rejected")
            self.runtime.heartbeat_interval = int(value)
            return call_result.ChangeConfiguration(status="Accepted")

        if key == "MeterValueSampleInterval":
            if not value.isdigit():
                return call_result.ChangeConfiguration(status="Rejected")
            self.runtime.meter_interval = int(value)
            return call_result.ChangeConfiguration(status="Accepted")

        if key in ("StopTransactionOnInvalidId", "StopTransactionOnEVSideDisconnect"):
            if value.lower() not in ("true", "false"):
                return call_result.ChangeConfiguration(status="Rejected")
            self.config[key] = value.lower()
            return call_result.ChangeConfiguration(status="Accepted")

        return call_result.ChangeConfiguration(status="NotSupported")

    @on(Action.get_configuration)
    async def on_get_configuration(self, key: Optional[list[str]] = None, **kwargs: Any) -> Dict[str, Any]:
        self._log("RX", "GetConfiguration", f"keys={key or ['*']}")

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="GetConfiguration",
            payload={"key": key or []}
        ))

        configuration = {
            "NumberOfConnectors": ("1", True),
            "ConnectorPhaseRotation": ("1.RST", True),
            "ChargingScheduleAllowedChargingRateUnit": ("Current,Power" if "Power" in self.config["supported_rate_units"] else "Current", True),
            "ChargeProfileMaxStackLevel": ("3", True),
            "MaxChargingProfilesInstalled": ("5", True),
            "ChargingScheduleMaxPeriods": ("5", True),
            "MeterValueSampleInterval": (str(self.runtime.meter_interval), False),
            "MeterValuesSampledData": (
                "Energy.Active.Import.Register,Power.Active.Import,Current.Import,Voltage",
                False,
            ),
            "HeartbeatInterval": (str(self.runtime.heartbeat_interval), False),
            "StopTransactionOnInvalidId": (str(self.config.get("StopTransactionOnInvalidId", "true")), False),
            "StopTransactionOnEVSideDisconnect": (str(self.config.get("StopTransactionOnEVSideDisconnect", "true")), False),
        }

        keys = key or list(configuration.keys())
        response_keys = []
        unknown_keys = []
        for k in keys:
            if k in configuration:
                value, readonly = configuration[k]
                response_keys.append({"key": k, "readonly": readonly, "value": value})
            else:
                unknown_keys.append(k)

        return call_result.GetConfiguration(configuration_key=response_keys, unknown_key=unknown_keys)

    @on(Action.trigger_message)
    async def on_trigger_message(self, requested_message: str, **kwargs: Any) -> Dict[str, str]:
        self._log("RX", "TriggerMessage", requested_message)

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="TriggerMessage",
            payload={"requested_message": requested_message}
        ))

        async def _send() -> None:
            if requested_message == "StatusNotification":
                await self.send_status_notification(self.runtime.connector.connector_id, self.runtime.connector.status)
            elif requested_message == "Heartbeat":
                await self.send_heartbeat()
            elif requested_message == "MeterValues":
                await self.send_meter_values()
            elif requested_message == "BootNotification":
                await self.send_boot_notification()

        asyncio.create_task(_send())
        return call_result.TriggerMessage(status="Accepted")

    @on(Action.change_availability)
    async def on_change_availability(self, connector_id: int, type: str, **kwargs: Any) -> Dict[str, str]:
        self._log("RX", "ChangeAvailability", f"connector={connector_id} type={type}")

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="ChangeAvailability",
            payload={"connector_id": connector_id, "type": type}
        ))

        if self.runtime.transaction_id is not None:
            self.runtime.connector.pending_availability = type
            return call_result.ChangeAvailability(status="Scheduled")

        if type == "Inoperative":
            self.runtime.connector.set_status(ChargePointStatus.unavailable)
            await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.unavailable)
        else:
            self.runtime.connector.set_status(ChargePointStatus.available)
            await self.send_status_notification(self.runtime.connector.connector_id, ChargePointStatus.available)

        return call_result.ChangeAvailability(status="Accepted")

    @on(Action.reset)
    async def on_reset(self, type: str, **kwargs: Any) -> Dict[str, str]:
        self._log("RX", "Reset", f"type={type}")

        # Emit message received event
        await self.runtime.events.add(MessageReceived(
            action="Reset",
            payload={"type": type}
        ))

        if self.reset_callback:
            self.reset_callback(type)
        return call_result.Reset(status="Accepted")

    def _log(self, direction: str, action: str, detail: str) -> None:
        if self.log_callback:
            self.log_callback(direction, action, detail)
