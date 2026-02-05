"""
OCPP Test Scenarios.

Implements test cases from OCPP-Test.md specification.
"""

import asyncio
import pytest
import pytest_asyncio
from unittest.mock import AsyncMock, MagicMock, patch
from datetime import datetime

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    StateChange,
    MessageSent,
    TransactionStarted,
    TransactionStopped,
    ProfileApplied,
    MeterUpdate,
    AuthorizationEvent,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint, WallboxRuntime
from src.wallbox_emulator.connector import ConnectorState
from src.wallbox_emulator.meter import MeterState
from src.wallbox_emulator.ev_simulator import EvState


class TestTC010BootAndRegistration:
    """TC-010: Boot and Registration.

    Purpose: Verify basic OCPP connection handshake.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_boot_notification_accepted(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify BootNotification is sent and accepted."""
        # Mock the response
        mock_response = MagicMock()
        mock_response.status = "Accepted"
        mock_response.interval = 300

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = mock_response

            # Send boot notification
            await wallbox.send_boot_notification()

            # Verify call was made
            mock_call.assert_called_once()

            # Verify heartbeat interval was updated
            assert wallbox.runtime.heartbeat_interval == 300

            # Verify event was logged
            event = event_log.find(MessageSent, action="BootNotification")
            assert event is not None
            assert event.response["status"] == "Accepted"

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_initial_status_notification(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify initial StatusNotification is sent as Available."""
        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Send status notification
            await wallbox.send_status_notification(1, ChargePointStatus.available)

            # Verify event was logged
            event = event_log.find(MessageSent, action="StatusNotification")
            assert event is not None
            assert event.payload["status"] == "Available"
            assert event.payload["connector_id"] == 1


class TestTC100ChargeAt11kW:
    """TC-100: Charge at 11 kW (3-phase, 16A, 11.04 kW).

    Purpose: Full charge cycle at nominal 3-phase residential power.
    """

    @pytest.mark.charging
    @pytest.mark.asyncio
    async def test_plug_in_changes_status_to_preparing(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify plugging in changes status to Preparing."""
        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.plug_in()

            # Verify state change event
            event = event_log.find(StateChange, new_status="Preparing")
            assert event is not None
            assert event.connector_id == 1

            # Verify connector status
            assert wallbox.runtime.connector.status == ChargePointStatus.preparing

    @pytest.mark.charging
    @pytest.mark.asyncio
    async def test_start_transaction_with_authorization(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify transaction starts with authorization."""
        # Mock authorize response
        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Accepted"}

        # Mock start transaction response
        start_response = MagicMock()
        start_response.id_tag_info = {"status": "Accepted"}
        start_response.transaction_id = 12345

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.side_effect = [auth_response, start_response, MagicMock()]

            # Plug in first
            wallbox.runtime.ev.plug_in()
            wallbox.runtime.connector.set_status(ChargePointStatus.preparing)

            # Start transaction
            await wallbox.start_transaction("evcc")

            # Verify authorization event
            auth_event = event_log.find(AuthorizationEvent, id_tag="evcc")
            assert auth_event is not None
            assert auth_event.status == "Accepted"

            # Verify transaction started event
            tx_event = event_log.find(TransactionStarted)
            assert tx_event is not None
            assert tx_event.transaction_id == 12345
            assert tx_event.id_tag == "evcc"

            # Verify state changed to Charging
            state_event = event_log.find(StateChange, new_status="Charging")
            assert state_event is not None

    @pytest.mark.charging
    @pytest.mark.asyncio
    async def test_charging_profile_applied(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify charging profile with 16A limit is applied."""
        # Set up active transaction
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Apply charging profile
            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "A",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 16.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # Verify profile applied event
            event = event_log.find(ProfileApplied)
            assert event is not None
            assert event.limit_a == 16.0
            assert event.rate_unit == "A"

            # Verify meter current limit was updated
            assert wallbox.runtime.meter.current_limit_a == 16.0

    @pytest.mark.charging
    @pytest.mark.asyncio
    async def test_meter_values_at_11kw(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter values at 11kW (3-phase, 16A)."""
        # Set up charging state
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(16.0)
        wallbox.runtime.meter.set_phase_mode("3-phase")

        # Simulate some charging
        wallbox.runtime.meter.advance(1.0)  # 1 second

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.send_meter_values()

            # Verify meter update event
            event = event_log.find(MeterUpdate)
            assert event is not None

            # 3-phase at 16A, 230V, pf=1.0 = 11040W
            # Allow 5% tolerance
            expected_power = 11040.0
            tolerance = expected_power * 0.05
            assert abs(event.power_w - expected_power) <= tolerance

    @pytest.mark.charging
    @pytest.mark.asyncio
    async def test_stop_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify transaction stops cleanly."""
        # Set up active transaction
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.energy_wh = 5000.0

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.stop_transaction(reason="Remote")

            # Verify transaction stopped event
            event = event_log.find(TransactionStopped)
            assert event is not None
            assert event.transaction_id == 12345
            assert event.reason == "Remote"
            assert event.meter_stop == 5000

            # Verify state transitions
            finishing = event_log.find(StateChange, new_status="Finishing")
            assert finishing is not None

            available = event_log.find(StateChange, new_status="Available")
            assert available is not None


class TestTC105SuspendCharging:
    """TC-105: Charge at 0 kW (Suspend, 0A, 0 kW).

    Purpose: Verify charging suspends cleanly when limit is set to zero.
    """

    @pytest.mark.charging
    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_zero_limit_suspends_charging(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify setting limit to 0 suspends charging."""
        # Set up active transaction
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Apply zero limit profile
            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "A",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 0.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # Verify state changed to SuspendedEVSE
            event = event_log.find(StateChange, new_status="SuspendedEVSE")
            assert event is not None

            # Verify connector status
            assert wallbox.runtime.connector.status == ChargePointStatus.suspended_evse

    @pytest.mark.charging
    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_resume_after_suspend(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify charging resumes when limit is restored."""
        # Set up suspended state
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.suspended_evse)
        wallbox.runtime.meter.set_current_limit(0.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Restore limit
            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "A",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 16.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # Verify state changed back to Charging
            event = event_log.find(StateChange, new_status="Charging")
            assert event is not None

            # Verify connector status
            assert wallbox.runtime.connector.status == ChargePointStatus.charging

    @pytest.mark.charging
    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_meter_values_during_suspend(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter shows zero power during suspend."""
        # Set up suspended state
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.suspended_evse)
        wallbox.runtime.meter.set_current_limit(0.0)
        wallbox.runtime.meter.advance(1.0)  # Advance to reflect zero current

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.send_meter_values()

            # Verify meter shows zero power
            event = event_log.find(MeterUpdate)
            assert event is not None
            assert event.power_w == 0.0
            assert event.current_l1_a == 0.0


class TestProfileValidation:
    """Additional tests for charging profile validation."""

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_reject_profile_exceeding_max_current(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify profile exceeding max current is rejected."""
        wallbox.runtime.transaction_id = 12345

        result = await wallbox.on_set_charging_profile(
            connector_id=1,
            cs_charging_profiles={
                "chargingProfileId": 1,
                "chargingSchedule": {
                    "chargingRateUnit": "A",
                    "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 100.0}],  # Exceeds 32A max
                },
            },
        )

        assert result.status == "Rejected"

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_power_based_profile(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify power-based (W) profile is applied correctly."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "W",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 11040.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # Verify profile applied event with W unit
            event = event_log.find(ProfileApplied)
            assert event is not None
            assert event.limit_w == 11040.0
            assert event.rate_unit == "W"

            # 11040W / (3 * 230V * 1.0) = 16A
            assert abs(wallbox.runtime.meter.current_limit_a - 16.0) < 0.1
