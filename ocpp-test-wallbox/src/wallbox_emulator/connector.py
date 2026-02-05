from __future__ import annotations

from dataclasses import dataclass

from ocpp.v16.enums import ChargePointStatus


@dataclass
class ConnectorState:
    connector_id: int = 1
    status: ChargePointStatus = ChargePointStatus.available
    availability: str = "Operative"
    pending_availability: str | None = None

    def set_status(self, status: ChargePointStatus) -> None:
        self.status = status

    def set_availability(self, availability: str) -> None:
        self.availability = availability

    def set_pending_availability(self, availability: str | None) -> None:
        self.pending_availability = availability
