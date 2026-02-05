"""
OCPP Dynamic Power Control Tests (TC-200 to TC-203).

Tests for changing power limits during active charging sessions.
"""

import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    ProfileApplied,
    MeterUpdate,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint


class TestTC200CurrentRampDown:
    """TC-200: Current Ramp Down During Charging.

    Purpose: Verify power can be reduced dynamically during charging.
    """

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_ramp_down_16a_to_10a(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify ramp down from 16A to 10A."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(16.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Reduce to 10A
            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "A",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 10.0}],
                    },
                },
            )

            assert result.status == "Accepted"
            assert wallbox.runtime.meter.current_limit_a == 10.0

            # Verify profile applied
            event = event_log.find(ProfileApplied)
            assert event is not None
            assert event.limit_a == 10.0

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_ramp_down_10a_to_6a(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify ramp down from 10A to 6A."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(10.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "A",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 6.0}],
                    },
                },
            )

            assert result.status == "Accepted"
            assert wallbox.runtime.meter.current_limit_a == 6.0


class TestTC201CurrentRampUp:
    """TC-201: Current Ramp Up During Charging.

    Purpose: Verify power can be increased dynamically during charging.
    """

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_ramp_up_6a_to_10a(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify ramp up from 6A to 10A."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(6.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_set_charging_profile(
                connector_id=1,
                cs_charging_profiles={
                    "chargingProfileId": 1,
                    "chargingSchedule": {
                        "chargingRateUnit": "A",
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 10.0}],
                    },
                },
            )

            assert result.status == "Accepted"
            assert wallbox.runtime.meter.current_limit_a == 10.0

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_ramp_up_10a_to_16a(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify ramp up from 10A to 16A."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_current_limit(10.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

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
            assert wallbox.runtime.meter.current_limit_a == 16.0


class TestTC202GetCompositeSchedule:
    """TC-202: GetCompositeSchedule Verification.

    Purpose: Verify GetCompositeSchedule returns current limits.
    """

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_get_composite_schedule_current(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify GetCompositeSchedule returns current limit."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.meter.set_current_limit(16.0)

        result = await wallbox.on_get_composite_schedule(
            connector_id=1,
            duration=3600
        )

        assert result.status == "Accepted"
        assert result.connector_id == 1
        assert "chargingSchedulePeriod" in result.charging_schedule
        assert len(result.charging_schedule["chargingSchedulePeriod"]) == 1
        assert result.charging_schedule["chargingSchedulePeriod"][0]["limit"] == 16.0

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_get_composite_schedule_after_change(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify GetCompositeSchedule reflects limit changes."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.meter.set_current_limit(10.0)

        result = await wallbox.on_get_composite_schedule(
            connector_id=1,
            duration=3600
        )

        assert result.status == "Accepted"
        assert result.charging_schedule["chargingSchedulePeriod"][0]["limit"] == 10.0

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_get_composite_schedule_rate_unit(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify GetCompositeSchedule returns correct rate unit."""
        wallbox.runtime.meter.set_current_limit(16.0)

        result = await wallbox.on_get_composite_schedule(
            connector_id=1,
            duration=3600
        )

        assert result.status == "Accepted"
        # Should return A or W depending on config
        assert result.charging_schedule["chargingRateUnit"] in ("A", "W")


class TestTC203RapidLimitChanges:
    """TC-203: Rapid Limit Changes (Stress).

    Purpose: Verify wallbox handles rapid successive limit changes.
    """

    @pytest.mark.power
    @pytest.mark.slow
    @pytest.mark.asyncio
    async def test_rapid_limit_changes(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify rapid successive limit changes are handled."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Apply multiple limit changes rapidly
            limits = [16.0, 10.0, 6.0, 10.0, 16.0, 6.0, 16.0]

            for limit in limits:
                result = await wallbox.on_set_charging_profile(
                    connector_id=1,
                    cs_charging_profiles={
                        "chargingProfileId": 1,
                        "chargingSchedule": {
                            "chargingRateUnit": "A",
                            "chargingSchedulePeriod": [{"startPeriod": 0, "limit": limit}],
                        },
                    },
                )

                assert result.status == "Accepted"
                assert wallbox.runtime.meter.current_limit_a == limit

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_zero_to_max_rapid(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify rapid changes between 0 and max."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Zero -> max -> zero -> max
            for limit in [0.0, 16.0, 0.0, 16.0]:
                result = await wallbox.on_set_charging_profile(
                    connector_id=1,
                    cs_charging_profiles={
                        "chargingProfileId": 1,
                        "chargingSchedule": {
                            "chargingRateUnit": "A",
                            "chargingSchedulePeriod": [{"startPeriod": 0, "limit": limit}],
                        },
                    },
                )

                assert result.status == "Accepted"
                assert wallbox.runtime.meter.current_limit_a == limit


class TestDynamicPowerMeterValues:
    """Tests for meter values during dynamic power changes."""

    @pytest.mark.power
    @pytest.mark.asyncio
    async def test_meter_reflects_limit_change(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter values update after limit change."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Set initial limit
            wallbox.runtime.meter.set_current_limit(16.0)
            await wallbox.send_meter_values()

            initial_meter = event_log.find(MeterUpdate)
            assert initial_meter is not None
            initial_power = initial_meter.power_w

            # Change limit
            event_log.clear()
            wallbox.runtime.meter.set_current_limit(10.0)
            await wallbox.send_meter_values()

            new_meter = event_log.find(MeterUpdate)
            assert new_meter is not None
            assert new_meter.power_w < initial_power  # Power should decrease
