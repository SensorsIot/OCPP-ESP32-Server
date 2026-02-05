"""
Event types for test framework.

All events that can be captured during test execution.
"""

from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Dict, Optional


@dataclass
class Event:
    """Base class for all captured events"""
    timestamp: datetime = field(default_factory=datetime.utcnow)
    event_type: str = "base"

    def __str__(self) -> str:
        return f"[{self.timestamp.isoformat()}] {self.event_type}"


@dataclass
class MessageReceived(Event):
    """OCPP message received from CSMS"""
    event_type: str = field(default="message_received", init=False)
    action: str = ""
    message_id: str = ""
    payload: Dict[str, Any] = field(default_factory=dict)

    def __str__(self) -> str:
        return f"[{self.timestamp.isoformat()}] RX {self.action}: {self.payload}"


@dataclass
class MessageSent(Event):
    """OCPP message sent to CSMS"""
    event_type: str = field(default="message_sent", init=False)
    action: str = ""
    message_id: str = ""
    payload: Dict[str, Any] = field(default_factory=dict)
    response: Optional[Dict[str, Any]] = None

    def __str__(self) -> str:
        return f"[{self.timestamp.isoformat()}] TX {self.action}: {self.payload}"


@dataclass
class StateChange(Event):
    """Connector state transition"""
    event_type: str = field(default="state_change", init=False)
    connector_id: int = 1
    old_status: str = ""
    new_status: str = ""

    def __str__(self) -> str:
        return f"[{self.timestamp.isoformat()}] State: {self.old_status} -> {self.new_status} (conn={self.connector_id})"


@dataclass
class MeterUpdate(Event):
    """Meter values snapshot"""
    event_type: str = field(default="meter_update", init=False)
    power_w: float = 0.0
    energy_wh: float = 0.0
    current_l1_a: float = 0.0
    current_l2_a: float = 0.0
    current_l3_a: float = 0.0
    voltage_v: float = 230.0

    def __str__(self) -> str:
        return (
            f"[{self.timestamp.isoformat()}] Meter: "
            f"P={self.power_w:.0f}W E={self.energy_wh:.0f}Wh "
            f"I=[{self.current_l1_a:.1f},{self.current_l2_a:.1f},{self.current_l3_a:.1f}]A"
        )


@dataclass
class ProfileApplied(Event):
    """Charging profile applied"""
    event_type: str = field(default="profile_applied", init=False)
    profile_id: int = 0
    limit_a: Optional[float] = None
    limit_w: Optional[float] = None
    rate_unit: str = "A"

    def __str__(self) -> str:
        if self.rate_unit == "A":
            return f"[{self.timestamp.isoformat()}] Profile: limit={self.limit_a}A"
        else:
            return f"[{self.timestamp.isoformat()}] Profile: limit={self.limit_w}W"


@dataclass
class TransactionStarted(Event):
    """Transaction started"""
    event_type: str = field(default="transaction_started", init=False)
    transaction_id: int = 0
    connector_id: int = 1
    id_tag: str = ""
    meter_start: int = 0

    def __str__(self) -> str:
        return (
            f"[{self.timestamp.isoformat()}] TxStart: "
            f"tx={self.transaction_id} tag={self.id_tag} meter={self.meter_start}Wh"
        )


@dataclass
class TransactionStopped(Event):
    """Transaction stopped"""
    event_type: str = field(default="transaction_stopped", init=False)
    transaction_id: int = 0
    connector_id: int = 1
    meter_stop: int = 0
    reason: str = ""

    def __str__(self) -> str:
        return (
            f"[{self.timestamp.isoformat()}] TxStop: "
            f"tx={self.transaction_id} meter={self.meter_stop}Wh reason={self.reason}"
        )


@dataclass
class MqttMessage(Event):
    """MQTT message captured from broker"""
    event_type: str = field(default="mqtt", init=False)
    topic: str = ""
    payload: Dict[str, Any] = field(default_factory=dict)

    def __str__(self) -> str:
        return f"[{self.timestamp.isoformat()}] MQTT {self.topic}: {self.payload}"


@dataclass
class ConnectionEvent(Event):
    """WebSocket connection event"""
    event_type: str = field(default="connection", init=False)
    action: str = ""  # "connected", "disconnected", "reconnecting"
    url: str = ""
    error: Optional[str] = None

    def __str__(self) -> str:
        if self.error:
            return f"[{self.timestamp.isoformat()}] Connection: {self.action} - {self.error}"
        return f"[{self.timestamp.isoformat()}] Connection: {self.action}"


@dataclass
class AuthorizationEvent(Event):
    """Authorization result"""
    event_type: str = field(default="authorization", init=False)
    id_tag: str = ""
    status: str = ""  # "Accepted", "Blocked", "Invalid", etc.

    def __str__(self) -> str:
        return f"[{self.timestamp.isoformat()}] Auth: {self.id_tag} -> {self.status}"
