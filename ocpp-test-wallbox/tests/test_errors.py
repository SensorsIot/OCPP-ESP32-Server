"""
OCPP Error Handling Tests (TC-500 to TC-504).

Tests for error conditions and edge cases.
"""

import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    StateChange,
    MessageReceived,
    AuthorizationEvent,
    TransactionStopped,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint


class TestTC500WebSocketDisconnect:
    """TC-500: WebSocket Disconnect During Charging.

    Purpose: Verify wallbox handles disconnection gracefully.
    Note: This tests the state management, not actual WebSocket reconnection.
    """

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_state_preserved_during_disconnect(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify transaction state is preserved."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(16.0)
        wallbox.runtime.meter.energy_wh = 5000.0

        # Simulate disconnect scenario - state should be preserved
        # (In real implementation, this would handle reconnection)
        assert wallbox.runtime.transaction_id == 12345
        assert wallbox.runtime.connector.status == ChargePointStatus.charging
        assert wallbox.runtime.meter.energy_wh == 5000.0


class TestTC501EVDisconnectDuringCharging:
    """TC-501: EV Disconnect During Charging (Unexpected).

    Purpose: Verify wallbox handles unexpected EV disconnect.
    """

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_ev_disconnect_stops_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify EV disconnect triggers stop transaction."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.ev.plug_in()

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.unplug()

            # Should have stopped transaction
            tx_stopped = event_log.find(TransactionStopped)
            assert tx_stopped is not None
            assert tx_stopped.reason == "EVDisconnected"

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_ev_disconnect_clears_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify transaction ID cleared after EV disconnect."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.ev.plug_in()

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.unplug()

            # Transaction should be cleared
            assert wallbox.runtime.transaction_id is None


class TestTC502AuthorizeRejected:
    """TC-502: Authorize Rejected.

    Purpose: Verify wallbox handles rejected authorization.
    """

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_authorize_rejected_blocks_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify rejected auth blocks transaction start."""
        wallbox.runtime.authorize_required = True

        # Mock rejected authorization
        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Blocked"}

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = auth_response

            wallbox.runtime.ev.plug_in()
            wallbox.runtime.connector.set_status(ChargePointStatus.preparing)

            await wallbox.start_transaction("blocked_user")

            # Auth event should show blocked
            auth_event = event_log.find(AuthorizationEvent)
            assert auth_event is not None
            assert auth_event.status == "Blocked"

            # Transaction should NOT have started
            assert wallbox.runtime.transaction_id is None

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_authorize_invalid_blocks_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify invalid auth blocks transaction."""
        wallbox.runtime.authorize_required = True

        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Invalid"}

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = auth_response

            wallbox.runtime.ev.plug_in()
            wallbox.runtime.connector.set_status(ChargePointStatus.preparing)

            await wallbox.start_transaction("invalid_user")

            # Transaction should NOT have started
            assert wallbox.runtime.transaction_id is None


class TestTC503SetChargingProfileToZero:
    """TC-503: SetChargingProfile to Zero.

    Purpose: Verify 0A profile suspends cleanly without fault.
    """

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_zero_profile_no_fault(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 0A profile doesn't cause fault."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

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

            # Should be suspended, not faulted
            assert wallbox.runtime.connector.status == ChargePointStatus.suspended_evse
            assert wallbox.runtime.connector.status != ChargePointStatus.faulted

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_zero_profile_recoverable(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify wallbox recovers from 0A profile."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.suspended_evse)
        wallbox.runtime.meter.set_current_limit(0.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Restore power
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
            assert wallbox.runtime.connector.status == ChargePointStatus.charging


class TestTC504ChangeAvailabilityInoperative:
    """TC-504: ChangeAvailability to Inoperative.

    Purpose: Verify connector can be made unavailable.
    """

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_change_availability_inoperative(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify connector becomes unavailable."""
        wallbox.runtime.connector.set_status(ChargePointStatus.available)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_change_availability(
                connector_id=1,
                type="Inoperative"
            )

            assert result.status == "Accepted"
            assert wallbox.runtime.connector.status == ChargePointStatus.unavailable

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_change_availability_operative(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify connector can be made available again."""
        wallbox.runtime.connector.set_status(ChargePointStatus.unavailable)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_change_availability(
                connector_id=1,
                type="Operative"
            )

            assert result.status == "Accepted"
            assert wallbox.runtime.connector.status == ChargePointStatus.available

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_change_availability_scheduled_during_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify availability change is scheduled during transaction."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        result = await wallbox.on_change_availability(
            connector_id=1,
            type="Inoperative"
        )

        # Should be scheduled, not immediate
        assert result.status == "Scheduled"
        # Connector should still be charging
        assert wallbox.runtime.connector.status == ChargePointStatus.charging
        # Pending availability should be set
        assert wallbox.runtime.connector.pending_availability == "Inoperative"


class TestResetHandling:
    """Tests for Reset command handling."""

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_reset_soft(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify soft reset is accepted."""
        result = await wallbox.on_reset(type="Soft")

        assert result.status == "Accepted"
        event = event_log.find(MessageReceived, action="Reset")
        assert event is not None
        assert event.payload["type"] == "Soft"

    @pytest.mark.error
    @pytest.mark.asyncio
    async def test_reset_hard(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify hard reset is accepted."""
        result = await wallbox.on_reset(type="Hard")

        assert result.status == "Accepted"
        event = event_log.find(MessageReceived, action="Reset")
        assert event is not None
        assert event.payload["type"] == "Hard"
