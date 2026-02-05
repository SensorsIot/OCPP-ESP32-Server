"""
OCPP Remote Control Tests (TC-300 to TC-302).

Tests for remote start and stop transaction functionality.
"""

import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    StateChange,
    MessageReceived,
    TransactionStarted,
    TransactionStopped,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint


class TestTC300RemoteStartTransaction:
    """TC-300: Remote Start Transaction.

    Purpose: Verify CSMS can start a transaction remotely.
    """

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_start_accepted(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStartTransaction is accepted."""
        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Accepted"}

        start_response = MagicMock()
        start_response.id_tag_info = {"status": "Accepted"}
        start_response.transaction_id = 12345

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.side_effect = [MagicMock(), auth_response, start_response, MagicMock()]

            result = await wallbox.on_remote_start_transaction(
                connector_id=1,
                id_tag="evcc"
            )

            assert result.status == "Accepted"

            # Verify message received event
            event = event_log.find(MessageReceived, action="RemoteStartTransaction")
            assert event is not None
            assert event.payload["id_tag"] == "evcc"

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_start_triggers_plug_in(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStart plugs in if EV not connected."""
        assert not wallbox.runtime.ev.plugged_in

        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Accepted"}

        start_response = MagicMock()
        start_response.id_tag_info = {"status": "Accepted"}
        start_response.transaction_id = 12345

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.side_effect = [MagicMock(), auth_response, start_response, MagicMock()]

            result = await wallbox.on_remote_start_transaction(
                connector_id=1,
                id_tag="evcc"
            )

            assert result.status == "Accepted"

            # Wait for async task
            for _ in range(10):
                await asyncio.sleep(0.05)
                if wallbox.runtime.ev.plugged_in:
                    break

            # EV should be plugged in
            assert wallbox.runtime.ev.plugged_in

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_start_rejected_when_unavailable(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStart rejected when connector unavailable."""
        wallbox.runtime.connector.set_status(ChargePointStatus.unavailable)

        result = await wallbox.on_remote_start_transaction(
            connector_id=1,
            id_tag="evcc"
        )

        assert result.status == "Rejected"

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_start_rejected_when_faulted(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStart rejected when connector faulted."""
        wallbox.runtime.connector.set_status(ChargePointStatus.faulted)

        result = await wallbox.on_remote_start_transaction(
            connector_id=1,
            id_tag="evcc"
        )

        assert result.status == "Rejected"


class TestTC301RemoteStopTransaction:
    """TC-301: Remote Stop Transaction.

    Purpose: Verify CSMS can stop a transaction remotely.
    """

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_stop_accepted(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStopTransaction is accepted."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_remote_stop_transaction(
                transaction_id=12345
            )

            assert result.status == "Accepted"

            # Verify message received event
            event = event_log.find(MessageReceived, action="RemoteStopTransaction")
            assert event is not None
            assert event.payload["transaction_id"] == 12345

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_stop_triggers_stop_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStop triggers StopTransaction."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_remote_stop_transaction(
                transaction_id=12345
            )

            assert result.status == "Accepted"

            # Wait for async task
            for _ in range(10):
                await asyncio.sleep(0.05)
                if event_log.find(TransactionStopped):
                    break

            # Transaction should be stopped
            tx_stopped = event_log.find(TransactionStopped)
            assert tx_stopped is not None
            assert tx_stopped.reason == "Remote"

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_stop_rejected_wrong_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStop rejected for wrong transaction ID."""
        wallbox.runtime.transaction_id = 12345

        result = await wallbox.on_remote_stop_transaction(
            transaction_id=99999  # Wrong transaction ID
        )

        assert result.status == "Rejected"

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_stop_rejected_no_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStop rejected when no transaction active."""
        wallbox.runtime.transaction_id = None

        result = await wallbox.on_remote_stop_transaction(
            transaction_id=12345
        )

        assert result.status == "Rejected"


class TestTC302RemoteStartWithoutEV:
    """TC-302: Remote Start Without EV (Rejected scenario).

    Purpose: Verify behavior when remote start is attempted but fails.
    Note: Our implementation auto-plugs, so this tests unavailable state instead.
    """

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_start_unavailable_connector(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify RemoteStart fails on unavailable connector."""
        wallbox.runtime.connector.set_status(ChargePointStatus.unavailable)

        result = await wallbox.on_remote_start_transaction(
            connector_id=1,
            id_tag="evcc"
        )

        assert result.status == "Rejected"

        # Verify no transaction started
        tx_event = event_log.find(TransactionStarted)
        assert tx_event is None


class TestRemoteOperationsSequence:
    """Tests for remote operation sequences."""

    @pytest.mark.remote
    @pytest.mark.asyncio
    async def test_remote_start_then_stop(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify full remote start/stop sequence."""
        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Accepted"}

        start_response = MagicMock()
        start_response.id_tag_info = {"status": "Accepted"}
        start_response.transaction_id = 12345

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.side_effect = [
                MagicMock(),  # plug_in status
                auth_response,
                start_response,
                MagicMock(),  # start charging status
                MagicMock(),  # stop transaction
                MagicMock(),  # finishing status
                MagicMock(),  # available status
            ]

            # Start transaction remotely
            start_result = await wallbox.on_remote_start_transaction(
                connector_id=1,
                id_tag="evcc"
            )
            assert start_result.status == "Accepted"

            # Wait for transaction to start
            for _ in range(10):
                await asyncio.sleep(0.05)
                if wallbox.runtime.transaction_id:
                    break

            assert wallbox.runtime.transaction_id == 12345

            # Stop transaction remotely
            stop_result = await wallbox.on_remote_stop_transaction(
                transaction_id=12345
            )
            assert stop_result.status == "Accepted"

            # Wait for transaction to stop
            for _ in range(10):
                await asyncio.sleep(0.05)
                if wallbox.runtime.transaction_id is None:
                    break

            assert wallbox.runtime.transaction_id is None
