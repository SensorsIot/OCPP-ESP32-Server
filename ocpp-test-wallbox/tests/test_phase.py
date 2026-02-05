"""
OCPP Phase Switching Tests (TC-400 to TC-407).

Tests for switching between 1-phase and 3-phase charging.
"""

import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    MeterUpdate,
    ProfileApplied,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint


class TestTC400PhaseSwitchTo1Phase:
    """TC-400: Phase Switch 3-phase to 1-phase (Mid-Session).

    Purpose: Verify switching from 3-phase to 1-phase during charging.
    """

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_switch_3phase_to_1phase(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 3-phase to 1-phase switch."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.phase_mode = "3-phase"
        wallbox.runtime.meter.set_phase_mode("3-phase")

        # Switch to 1-phase
        await wallbox.set_phase_mode("1-phase")

        assert wallbox.runtime.phase_mode == "1-phase"
        assert wallbox.runtime.meter.phase_mode == "1-phase"

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_1phase_meter_values_after_switch(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter shows 1-phase values after switch."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_phase_mode("1-phase")
        wallbox.runtime.meter.set_current_limit(16.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.send_meter_values()

            event = event_log.find(MeterUpdate)
            assert event is not None
            # L2 and L3 should be 0 for 1-phase
            assert event.current_l2_a == 0.0
            assert event.current_l3_a == 0.0


class TestTC401PhaseSwitchTo3Phase:
    """TC-401: Phase Switch 1-phase to 3-phase (Mid-Session).

    Purpose: Verify switching from 1-phase to 3-phase during charging.
    """

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_switch_1phase_to_3phase(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify 1-phase to 3-phase switch."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.phase_mode = "1-phase"
        wallbox.runtime.meter.set_phase_mode("1-phase")

        # Switch to 3-phase
        await wallbox.set_phase_mode("3-phase")

        assert wallbox.runtime.phase_mode == "3-phase"
        assert wallbox.runtime.meter.phase_mode == "3-phase"

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_3phase_meter_values_after_switch(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter shows 3-phase values after switch."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_phase_mode("3-phase")
        wallbox.runtime.meter.set_current_limit(16.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            await wallbox.send_meter_values()

            event = event_log.find(MeterUpdate)
            assert event is not None
            # All phases should have current for 3-phase
            assert event.current_l1_a == 16.0
            assert event.current_l2_a == 16.0
            assert event.current_l3_a == 16.0


class TestTC402PhaseSwitchWithoutTransaction:
    """TC-402: Phase Switch Without Active Transaction.

    Purpose: Verify phase can be changed when not charging.
    """

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_switch_phase_idle(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify phase switch when idle (no transaction)."""
        wallbox.runtime.transaction_id = None
        wallbox.runtime.connector.set_status(ChargePointStatus.available)
        wallbox.runtime.phase_mode = "3-phase"

        # Switch to 1-phase while idle
        await wallbox.set_phase_mode("1-phase")

        assert wallbox.runtime.phase_mode == "1-phase"

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_switch_phase_preparing(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify phase switch when preparing."""
        wallbox.runtime.transaction_id = None
        wallbox.runtime.connector.set_status(ChargePointStatus.preparing)
        wallbox.runtime.phase_mode = "3-phase"

        await wallbox.set_phase_mode("1-phase")

        assert wallbox.runtime.phase_mode == "1-phase"


class TestTC405MeterContinuityAcrossPhaseSwitch:
    """TC-405: Meter Continuity Across Phase Switch.

    Purpose: Verify energy meter remains continuous across phase switch.
    """

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_energy_continuous_after_switch(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify energy accumulation continues across phase switch."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.meter.set_phase_mode("3-phase")
        wallbox.runtime.meter.set_current_limit(16.0)

        # Accumulate some energy
        wallbox.runtime.meter.advance(60.0)  # 1 minute
        energy_before = wallbox.runtime.meter.energy_wh

        # Switch phases
        await wallbox.set_phase_mode("1-phase")

        # Energy should be unchanged
        assert wallbox.runtime.meter.energy_wh == energy_before

        # Accumulate more energy
        wallbox.runtime.meter.advance(60.0)  # Another minute
        energy_after = wallbox.runtime.meter.energy_wh

        # Energy should have increased
        assert energy_after > energy_before


class TestTC406PowerLimitAfterPhaseSwitch:
    """TC-406: Power Limit Change Immediately After Phase Switch.

    Purpose: Verify power limit can be changed right after phase switch.
    """

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_set_limit_after_switch(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify limit can be set immediately after phase switch."""
        wallbox.runtime.transaction_id = 12345
        wallbox.runtime.connector.set_status(ChargePointStatus.charging)
        wallbox.runtime.phase_mode = "3-phase"
        wallbox.runtime.meter.set_phase_mode("3-phase")
        wallbox.runtime.meter.set_current_limit(16.0)

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            # Switch to 1-phase
            await wallbox.set_phase_mode("1-phase")

            # Immediately set new limit
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


class TestTC407MQTTPhaseStatus:
    """TC-407: MQTT Phase Status and Correction Factor Update.

    Purpose: Verify phase status is tracked correctly.
    Note: MQTT publishing is tested in integration tests.
    """

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_phase_mode_tracked(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify phase mode is tracked in runtime."""
        wallbox.runtime.phase_mode = "3-phase"

        await wallbox.set_phase_mode("1-phase")
        assert wallbox.runtime.phase_mode == "1-phase"

        await wallbox.set_phase_mode("3-phase")
        assert wallbox.runtime.phase_mode == "3-phase"

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_meter_phase_sync(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify meter phase mode stays in sync with runtime."""
        await wallbox.set_phase_mode("1-phase")
        assert wallbox.runtime.meter.phase_mode == "1-phase"

        await wallbox.set_phase_mode("3-phase")
        assert wallbox.runtime.meter.phase_mode == "3-phase"


class TestPhaseValidation:
    """Tests for phase mode validation."""

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_invalid_phase_mode_rejected(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify invalid phase mode raises error."""
        with pytest.raises(ValueError):
            wallbox.runtime.meter.set_phase_mode("2-phase")  # Invalid

    @pytest.mark.phase
    @pytest.mark.asyncio
    async def test_valid_phase_modes(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify valid phase modes are accepted."""
        wallbox.runtime.meter.set_phase_mode("1-phase")
        assert wallbox.runtime.meter.phase_mode == "1-phase"

        wallbox.runtime.meter.set_phase_mode("3-phase")
        assert wallbox.runtime.meter.phase_mode == "3-phase"
