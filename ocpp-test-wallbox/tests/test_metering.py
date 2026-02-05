"""
OCPP Metering Tests (TC-600 to TC-601).

Tests for meter value accuracy and transaction boundaries.
"""

import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    MeterUpdate,
    TransactionStarted,
    TransactionStopped,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint


class TestTC600EnergyAccumulation:
    """TC-600: Energy Accumulation Over Time.

    Purpose: Verify energy meter accumulates correctly over time.
    """

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_energy_accumulates(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify energy increases over time during charging."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(16.0)
        wallbox.runtime.meter.set_phase_mode("3-phase")

        initial_energy = wallbox.runtime.meter.energy_wh

        # Simulate 60 seconds of charging
        wallbox.runtime.meter.advance(60.0)

        final_energy = wallbox.runtime.meter.energy_wh

        # Energy should have increased
        assert final_energy > initial_energy

        # Expected: 11040W * (60/3600) = 184 Wh
        expected_energy = 11040.0 * (60.0 / 3600.0)
        tolerance = expected_energy * 0.05  # 5% tolerance
        actual_increase = final_energy - initial_energy

        assert abs(actual_increase - expected_energy) < tolerance

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_energy_accumulates_at_reduced_power(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify energy accumulates correctly at reduced power."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(10.0)  # 10A
        wallbox.runtime.meter.set_phase_mode("3-phase")

        initial_energy = wallbox.runtime.meter.energy_wh

        # Simulate 60 seconds
        wallbox.runtime.meter.advance(60.0)

        final_energy = wallbox.runtime.meter.energy_wh

        # Expected: 6900W * (60/3600) = 115 Wh
        expected_energy = 6900.0 * (60.0 / 3600.0)
        tolerance = expected_energy * 0.05
        actual_increase = final_energy - initial_energy

        assert abs(actual_increase - expected_energy) < tolerance

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_no_energy_when_suspended(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify no energy accumulates when suspended (0A)."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.suspended_evse)
        wallbox.runtime.meter.set_current_limit(0.0)

        initial_energy = wallbox.runtime.meter.energy_wh

        # Simulate 60 seconds
        wallbox.runtime.meter.advance(60.0)

        final_energy = wallbox.runtime.meter.energy_wh

        # Energy should not have changed
        assert final_energy == initial_energy


class TestTC601MeterValuesBoundaries:
    """TC-601: Meter Values Match Transaction Boundaries.

    Purpose: Verify meter values at transaction start/stop match.
    """

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_meter_start_matches_transaction_start(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter_start equals energy_wh at transaction start."""
        # Set initial energy level
        wallbox.runtime.meter.energy_wh = 1000.0

        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Accepted"}

        start_response = MagicMock()
        start_response.id_tag_info = {"status": "Accepted"}
        start_response.transaction_id = 12345

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.side_effect = [auth_response, start_response, MagicMock()]

            wallbox.runtime.ev.plug_in()
            wallbox.runtime.connector.set_status(ChargePointStatus.preparing)

            await wallbox.start_transaction("evcc")

            # Verify meter_start in event matches energy_wh
            tx_event = event_log.find(TransactionStarted)
            assert tx_event is not None
            assert tx_event.meter_start == 1000

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_meter_stop_matches_transaction_stop(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter_stop equals energy_wh at transaction stop."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.energy_wh = 5500.0

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.stop_transaction(reason="Remote")

            # Verify meter_stop in event matches energy_wh
            tx_event = event_log.find(TransactionStopped)
            assert tx_event is not None
            assert tx_event.meter_stop == 5500

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_energy_difference_matches_charging_duration(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify energy consumed matches expected for charging duration."""
        initial_energy = 1000.0
        wallbox.runtime.meter.energy_wh = initial_energy
        wallbox.runtime.meter.set_current_limit(16.0)
        wallbox.runtime.meter.set_phase_mode("3-phase")

        auth_response = MagicMock()
        auth_response.id_tag_info = {"status": "Accepted"}

        start_response = MagicMock()
        start_response.id_tag_info = {"status": "Accepted"}
        start_response.transaction_id = 12345

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.side_effect = [
                auth_response, start_response, MagicMock(),
                MagicMock(), MagicMock(), MagicMock()
            ]

            wallbox.runtime.ev.plug_in()
            wallbox.runtime.connector.set_status(ChargePointStatus.preparing)

            await wallbox.start_transaction("evcc")

            # Simulate 5 minutes of charging
            wallbox.runtime.meter.advance(300.0)

            await wallbox.stop_transaction(reason="Remote")

            # Get events
            start_event = event_log.find(TransactionStarted)
            stop_event = event_log.find(TransactionStopped)

            assert start_event is not None
            assert stop_event is not None

            # Calculate energy consumed
            energy_consumed = stop_event.meter_stop - start_event.meter_start

            # Expected: 11040W * (300/3600) = 920 Wh
            expected_energy = 11040.0 * (300.0 / 3600.0)
            tolerance = expected_energy * 0.05

            assert abs(energy_consumed - expected_energy) < tolerance


class TestMeterValuesAccuracy:
    """Tests for meter value accuracy."""

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_power_calculation_3phase(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify power calculation for 3-phase."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.meter.set_phase_mode("3-phase")
        wallbox.runtime.meter.set_current_limit(16.0)

        # P = 3 * V * I * pf = 3 * 230 * 16 * 1.0 = 11040W
        assert wallbox.runtime.meter.power_w == 11040.0

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_current_values_3phase(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify current values for 3-phase."""
        wallbox.runtime.meter.set_phase_mode("3-phase")
        wallbox.runtime.meter.set_current_limit(16.0)

        assert wallbox.runtime.meter.current_l1 == 16.0
        assert wallbox.runtime.meter.current_l2 == 16.0
        assert wallbox.runtime.meter.current_l3 == 16.0

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_current_values_1phase(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify current values for 1-phase."""
        wallbox.runtime.meter.set_phase_mode("1-phase")
        wallbox.runtime.meter.set_current_limit(16.0)

        assert wallbox.runtime.meter.current_l1 == 16.0
        assert wallbox.runtime.meter.current_l2 == 0.0
        assert wallbox.runtime.meter.current_l3 == 0.0

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_sampled_values_format(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify sampled values have correct format."""
        wallbox.runtime.meter.set_phase_mode("3-phase")
        wallbox.runtime.meter.set_current_limit(16.0)
        wallbox.runtime.meter.energy_wh = 5000.0

        sampled = wallbox.runtime.meter.sampled_values()

        # Should have power, energy, currents, and voltages
        assert len(sampled) > 0

        # Check power value exists
        power_values = [v for v in sampled if v["measurand"] == "Power.Active.Import"]
        assert len(power_values) == 1
        assert power_values[0]["unit"] == "W"

        # Check energy value exists
        energy_values = [v for v in sampled if v["measurand"] == "Energy.Active.Import.Register"]
        assert len(energy_values) == 1
        assert energy_values[0]["unit"] == "Wh"

        # Check current values exist
        current_values = [v for v in sampled if v["measurand"] == "Current.Import"]
        assert len(current_values) == 3  # L1, L2, L3


class TestMeterUpdateEvents:
    """Tests for MeterUpdate event emission."""

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_meter_update_event_emitted(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify MeterUpdate event is emitted."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.meter.set_current_limit(16.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.send_meter_values()

            event = event_log.find(MeterUpdate)
            assert event is not None
            assert event.power_w == wallbox.runtime.meter.power_w
            assert event.current_l1_a == wallbox.runtime.meter.current_l1

    @pytest.mark.metering
    @pytest.mark.asyncio
    async def test_meter_update_not_emitted_without_transaction(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify no MeterUpdate when no transaction."""
        wallbox.runtime.transaction_id = None

        await wallbox.send_meter_values()

        event = event_log.find(MeterUpdate)
        assert event is None  # Should not emit without transaction
