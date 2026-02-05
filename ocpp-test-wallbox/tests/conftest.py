"""
Pytest configuration and fixtures for OCPP Wallbox tests.

Provides common fixtures for test setup and teardown.
"""

import asyncio
import logging
from typing import AsyncGenerator, Generator, Optional
from unittest.mock import AsyncMock, MagicMock

import pytest
import pytest_asyncio

from src.testing import (
    EventLog,
    MqttObserver,
    StateChange,
    MessageReceived,
    MessageSent,
    MeterUpdate,
    ProfileApplied,
    TransactionStarted,
    TransactionStopped,
    AuthorizationEvent,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint, WallboxRuntime
from src.wallbox_emulator.connector import ConnectorState
from src.wallbox_emulator.meter import MeterState
from src.wallbox_emulator.ev_simulator import EvState


# Configure logging for tests
logging.basicConfig(level=logging.DEBUG)


@pytest.fixture
def event_loop():
    """Create an event loop for the test session."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture
def event_log() -> EventLog:
    """Create a fresh event log for each test."""
    return EventLog()


@pytest.fixture
def connector_state() -> ConnectorState:
    """Create a connector state for testing."""
    return ConnectorState(connector_id=1)


@pytest.fixture
def meter_state() -> MeterState:
    """Create a meter state for testing."""
    return MeterState()


@pytest.fixture
def ev_state() -> EvState:
    """Create an EV simulator state for testing."""
    return EvState()


@pytest.fixture
def wallbox_runtime(
    connector_state: ConnectorState,
    meter_state: MeterState,
    ev_state: EvState,
    event_log: EventLog,
) -> WallboxRuntime:
    """Create a wallbox runtime for testing."""
    runtime = WallboxRuntime(
        connector=connector_state,
        meter=meter_state,
        ev=ev_state,
    )
    runtime.events = event_log
    return runtime


@pytest.fixture
def wallbox_config() -> dict:
    """Default wallbox configuration for testing."""
    return {
        "vendor": "TestVendor",
        "model": "TestModel",
        "serial_number": "TEST001",
        "firmware_version": "1.0.0",
        "max_current_a": 32,
        "supported_rate_units": ["Current", "Power"],
    }


@pytest.fixture
def mock_connection() -> AsyncMock:
    """Create a mock WebSocket connection."""
    conn = AsyncMock()
    conn.send = AsyncMock()
    conn.recv = AsyncMock()
    conn.close = AsyncMock()
    return conn


@pytest_asyncio.fixture
async def wallbox(
    wallbox_runtime: WallboxRuntime,
    wallbox_config: dict,
    mock_connection: AsyncMock,
) -> WallboxChargePoint:
    """Create a wallbox charge point for testing."""
    cp = WallboxChargePoint(
        charge_point_id="TEST001",
        connection=mock_connection,
        runtime=wallbox_runtime,
        config=wallbox_config,
    )
    return cp


@pytest_asyncio.fixture
async def mqtt_observer(event_log: EventLog) -> AsyncGenerator[MqttObserver, None]:
    """Create an MQTT observer for testing.

    Note: This fixture requires an MQTT broker to be running.
    For unit tests, use mock_mqtt_observer instead.
    """
    observer = MqttObserver(
        event_log=event_log,
        host="localhost",
        port=1883,
        topics=["ocpp/#"],
    )
    yield observer
    if observer.connected:
        await observer.stop()


@pytest.fixture
def mock_mqtt_observer(event_log: EventLog) -> MagicMock:
    """Create a mock MQTT observer for unit tests."""
    observer = MagicMock(spec=MqttObserver)
    observer.event_log = event_log
    observer.connected = True
    observer.start = AsyncMock()
    observer.stop = AsyncMock()
    observer.publish = AsyncMock()
    return observer


# Assertion helpers

class EventAssertions:
    """Helper class for event-based assertions."""

    def __init__(self, event_log: EventLog):
        self.event_log = event_log

    async def assert_state_changed(
        self,
        expected_status: str,
        timeout: float = 5.0,
        connector_id: int = 1,
    ) -> StateChange:
        """Assert that connector state changed to expected status."""
        event = await self.event_log.wait_for(
            StateChange,
            timeout=timeout,
            new_status=expected_status,
            connector_id=connector_id,
        )
        return event

    async def assert_message_received(
        self,
        action: str,
        timeout: float = 5.0,
        **payload_filters,
    ) -> MessageReceived:
        """Assert that a specific OCPP message was received."""
        event = await self.event_log.wait_for(
            MessageReceived,
            timeout=timeout,
            action=action,
        )
        # Check payload filters if provided
        for key, value in payload_filters.items():
            assert event.payload.get(key) == value, \
                f"Expected {key}={value}, got {event.payload.get(key)}"
        return event

    async def assert_message_sent(
        self,
        action: str,
        timeout: float = 5.0,
        **payload_filters,
    ) -> MessageSent:
        """Assert that a specific OCPP message was sent."""
        event = await self.event_log.wait_for(
            MessageSent,
            timeout=timeout,
            action=action,
        )
        # Check payload filters if provided
        for key, value in payload_filters.items():
            assert event.payload.get(key) == value, \
                f"Expected {key}={value}, got {event.payload.get(key)}"
        return event

    async def assert_transaction_started(
        self,
        timeout: float = 5.0,
        id_tag: Optional[str] = None,
    ) -> TransactionStarted:
        """Assert that a transaction was started."""
        if id_tag:
            return await self.event_log.wait_for(
                TransactionStarted,
                timeout=timeout,
                id_tag=id_tag,
            )
        return await self.event_log.wait_for(
            TransactionStarted,
            timeout=timeout,
        )

    async def assert_transaction_stopped(
        self,
        timeout: float = 5.0,
        reason: Optional[str] = None,
    ) -> TransactionStopped:
        """Assert that a transaction was stopped."""
        if reason:
            return await self.event_log.wait_for(
                TransactionStopped,
                timeout=timeout,
                reason=reason,
            )
        return await self.event_log.wait_for(
            TransactionStopped,
            timeout=timeout,
        )

    async def assert_profile_applied(
        self,
        timeout: float = 5.0,
        limit_a: Optional[float] = None,
        limit_w: Optional[float] = None,
    ) -> ProfileApplied:
        """Assert that a charging profile was applied."""
        event = await self.event_log.wait_for(
            ProfileApplied,
            timeout=timeout,
        )
        if limit_a is not None:
            assert event.limit_a == limit_a, \
                f"Expected limit_a={limit_a}, got {event.limit_a}"
        if limit_w is not None:
            assert event.limit_w == limit_w, \
                f"Expected limit_w={limit_w}, got {event.limit_w}"
        return event

    async def assert_meter_reading(
        self,
        timeout: float = 5.0,
        min_power_w: Optional[float] = None,
        max_power_w: Optional[float] = None,
    ) -> MeterUpdate:
        """Assert meter values are within expected range."""
        event = await self.event_log.wait_for(
            MeterUpdate,
            timeout=timeout,
        )
        if min_power_w is not None:
            assert event.power_w >= min_power_w, \
                f"Expected power >= {min_power_w}W, got {event.power_w}W"
        if max_power_w is not None:
            assert event.power_w <= max_power_w, \
                f"Expected power <= {max_power_w}W, got {event.power_w}W"
        return event

    async def assert_authorization(
        self,
        id_tag: str,
        expected_status: str,
        timeout: float = 5.0,
    ) -> AuthorizationEvent:
        """Assert authorization result."""
        event = await self.event_log.wait_for(
            AuthorizationEvent,
            timeout=timeout,
            id_tag=id_tag,
        )
        assert event.status == expected_status, \
            f"Expected status={expected_status}, got {event.status}"
        return event

    def assert_no_event(
        self,
        event_type,
        **filters,
    ) -> None:
        """Assert that no event of the given type exists."""
        event = self.event_log.find(event_type, **filters)
        assert event is None, \
            f"Unexpected event found: {event}"

    def count_events(self, event_type, **filters) -> int:
        """Count events of a given type."""
        return self.event_log.count(event_type, **filters)


@pytest.fixture
def assertions(event_log: EventLog) -> EventAssertions:
    """Create event assertions helper."""
    return EventAssertions(event_log)


# Markers for test categorization

def pytest_configure(config):
    """Configure custom pytest markers."""
    config.addinivalue_line("markers", "connection: Connection tests")
    config.addinivalue_line("markers", "charging: Charging session tests")
    config.addinivalue_line("markers", "profile: Charging profile tests")
    config.addinivalue_line("markers", "remote: Remote operation tests")
    config.addinivalue_line("markers", "phase: Phase switching tests")
    config.addinivalue_line("markers", "error: Error handling tests")
    config.addinivalue_line("markers", "metering: Metering tests")
    config.addinivalue_line("markers", "slow: Slow tests that take > 10s")
    config.addinivalue_line("markers", "integration: Integration tests requiring real hardware")
