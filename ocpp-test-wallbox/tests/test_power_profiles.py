"""
OCPP Power-Based Profile Tests (TC-110 to TC-115).

Tests for charging profiles using Watts (W) instead of Amps (A).
"""

import pytest
from unittest.mock import AsyncMock, MagicMock, patch

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    StateChange,
    ProfileApplied,
    MeterUpdate,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint


class TestTC110ChargeAt11kW_Power:
    """TC-110: Charge at 11 kW (3-phase, 11040W).

    Purpose: Verify power-based profile at nominal 3-phase power.
    """

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_11kw_power_profile(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 11040W power-based profile."""
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

            # Verify profile applied event
            event = event_log.find(ProfileApplied)
            assert event is not None
            assert event.limit_w == 11040.0
            assert event.rate_unit == "W"

            # 11040W / (3 * 230V) ≈ 16A
            assert abs(wallbox.runtime.meter.current_limit_a - 16.0) < 0.1


class TestTC111ChargeAt3_7kW_Power:
    """TC-111: Charge at 3.7 kW (1-phase, 3680W).

    Purpose: Verify power-based profile for 1-phase charging.
    """

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_3_7kw_power_profile(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 3680W power-based profile for 1-phase."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.phase_mode = "1-phase"
        wallbox.runtime.meter.set_phase_mode("1-phase")

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "W",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 3680.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # 3680W / 230V = 16A for 1-phase
            assert abs(wallbox.runtime.meter.current_limit_a - 16.0) < 0.1


class TestTC112ChargeAt7kW_Power:
    """TC-112: Charge at 7 kW (3-phase, 6900W).

    Purpose: Verify power-based profile at reduced power.
    """

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_7kw_power_profile(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 6900W power-based profile."""
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
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 6900.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # 6900W / (3 * 230V) = 10A
            assert abs(wallbox.runtime.meter.current_limit_a - 10.0) < 0.1


class TestTC113ChargeAt4_1kW_Power:
    """TC-113: Charge at 4.1 kW (3-phase, 4140W).

    Purpose: Verify power-based profile at phase switch threshold.
    """

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_4_1kw_power_profile(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 4140W power-based profile (phase switch threshold)."""
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
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 4140.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # 4140W / (3 * 230V) = 6A
            assert abs(wallbox.runtime.meter.current_limit_a - 6.0) < 0.1


class TestTC114ChargeAt2kW_Power:
    """TC-114: Charge at 2 kW (1-phase, 2000W).

    Purpose: Verify power-based profile for low power 1-phase charging.
    """

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_2kw_power_profile(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 2000W power-based profile for 1-phase."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.phase_mode = "1-phase"
        wallbox.runtime.meter.set_phase_mode("1-phase")

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "W",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 2000.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # 2000W / 230V ≈ 8.7A
            assert abs(wallbox.runtime.meter.current_limit_a - 8.7) < 0.1


class TestTC115SuspendAt0W:
    """TC-115: Charge at 0 kW (Suspend, 0W).

    Purpose: Verify power-based profile suspends at zero.
    """

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_0w_suspends_charging(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 0W profile suspends charging."""
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
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 0.0}],
                    },
                },
            )

            assert result.status == "Accepted"

            # Verify suspended state
            state_event = event_log.find(StateChange, new_status="SuspendedEVSE")
            assert state_event is not None
            assert wallbox.runtime.connector.status == ChargePointStatus.suspended_evse

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_0w_meter_shows_zero(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter shows zero power when suspended via W profile."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.suspended_evse)
        wallbox.runtime.meter.set_current_limit(0.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.send_meter_values()

            event = event_log.find(MeterUpdate)
            assert event is not None
            assert event.power_w == 0.0


class TestPowerConversion:
    """Tests for power-to-current conversion accuracy."""

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_3phase_power_conversion(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 3-phase power to current conversion."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.phase_mode = "3-phase"

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Test various power levels
            test_cases = [
                (11040.0, 16.0),  # 11kW -> 16A
                (6900.0, 10.0),   # 6.9kW -> 10A
                (4140.0, 6.0),    # 4.14kW -> 6A
            ]

            for power_w, expected_a in test_cases:
                result = await wallbox.on_set_charging_profile(
                    connector_id=1,
                    cs_charging_profiles={
                        "chargingProfileId": 1,
                        "chargingSchedule": {
                            "chargingRateUnit": "W",
                            "chargingSchedulePeriod": [{"startPeriod": 0, "limit": power_w}],
                        },
                    },
                )

                assert result.status == "Accepted"
                assert abs(wallbox.runtime.meter.current_limit_a - expected_a) < 0.1, \
                    f"Power {power_w}W should convert to {expected_a}A, got {wallbox.runtime.meter.current_limit_a}A"

    @pytest.mark.profile
    @pytest.mark.asyncio
    async def test_1phase_power_conversion(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 1-phase power to current conversion."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.phase_mode = "1-phase"
        wallbox.runtime.meter.set_phase_mode("1-phase")

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Test 1-phase conversion
            # 3680W / 230V = 16A
            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "W",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 3680.0}],
                    },
                },
            )

            assert result.status == "Accepted"
            assert abs(wallbox.runtime.meter.current_limit_a - 16.0) < 0.1
