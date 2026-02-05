"""
OCPP Connection Tests (TC-011 to TC-021).

Tests for connection, configuration, and protocol validation.
"""

import asyncio
import pytest
from unittest.mock import AsyncMock, MagicMock, patch

from ocpp.v16.enums import ChargePointStatus

from src.testing import (
    EventLog,
    MessageSent,
    MessageReceived,
    ProfileApplied,
)
from src.wallbox_emulator.chargepoint import WallboxChargePoint


class TestTC011Heartbeat:
    """TC-011: Heartbeat Keepalive.

    Purpose: Verify heartbeat messages are sent at configured interval.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_heartbeat_sent(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify Heartbeat message is sent."""
        mock_response = MagicMock()
        mock_response.current_time = "2026-02-03T12:00:00Z"

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = mock_response

            await wallbox.send_heartbeat()

            mock_call.assert_called_once()
            event = event_log.find(MessageSent, action="Heartbeat")
            assert event is not None


class TestTC012TriggerMessage:
    """TC-012: TriggerMessage (State Sync).

    Purpose: Verify wallbox responds to TriggerMessage requests.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_trigger_status_notification(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify TriggerMessage for StatusNotification."""
        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_trigger_message(requested_message="StatusNotification")

            assert result.status == "Accepted"
            event = event_log.find(MessageReceived, action="TriggerMessage")
            assert event is not None
            assert event.payload["requested_message"] == "StatusNotification"

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_trigger_heartbeat(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify TriggerMessage for Heartbeat."""
        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_trigger_message(requested_message="Heartbeat")

            assert result.status == "Accepted"

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_trigger_meter_values(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify TriggerMessage for MeterValues."""
        wallbox.runtime.transaction_id = 12345

        with patch.object(wallbox, 'call', new_callable=AsyncMock) as mock_call:
            mock_call.return_value = MagicMock()

            result = await wallbox.on_trigger_message(requested_message="MeterValues")

            assert result.status == "Accepted"


class TestTC013GetConfiguration:
    """TC-013: GetConfiguration.

    Purpose: Verify wallbox returns configuration values.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_get_all_configuration(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify GetConfiguration returns all keys."""
        result = await wallbox.on_get_configuration(key=None)

        assert hasattr(result, "configuration_key")
        assert len(result.configuration_key) > 0

        # Check expected keys exist
        keys = {item["key"] for item in result.configuration_key}
        assert "MeterValueSampleInterval" in keys
        assert "HeartbeatInterval" in keys
        assert "NumberOfConnectors" in keys

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_get_specific_configuration(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify GetConfiguration returns specific key."""
        result = await wallbox.on_get_configuration(key=["MeterValueSampleInterval"])

        assert len(result.configuration_key) == 1
        assert result.configuration_key[0]["key"] == "MeterValueSampleInterval"

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_get_unknown_configuration(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify GetConfiguration returns unknown keys."""
        result = await wallbox.on_get_configuration(key=["UnknownKey123"])

        assert hasattr(result, "unknown_key")
        assert "UnknownKey123" in result.unknown_key

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_get_charging_rate_unit(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify ChargingScheduleAllowedChargingRateUnit is returned."""
        result = await wallbox.on_get_configuration(
            key=["ChargingScheduleAllowedChargingRateUnit"]
        )

        assert len(result.configuration_key) == 1
        value = result.configuration_key[0]["value"]
        assert "Current" in value  # Must support at least Current


class TestTC014ChangeConfiguration:
    """TC-014: ChangeConfiguration (Meter Interval).

    Purpose: Verify configuration can be changed.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_change_meter_interval(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify MeterValueSampleInterval can be changed."""
        result = await wallbox.on_change_configuration(
            key="MeterValueSampleInterval",
            value="30"
        )

        assert result.status == "Accepted"
        assert wallbox.runtime.meter_interval == 30

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_change_heartbeat_interval(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify HeartbeatInterval can be changed."""
        result = await wallbox.on_change_configuration(
            key="HeartbeatInterval",
            value="120"
        )

        assert result.status == "Accepted"
        assert wallbox.runtime.heartbeat_interval == 120


class TestTC017ChangeConfigurationUnknownKey:
    """TC-017: ChangeConfiguration Unknown Key (Negative).

    Purpose: Verify unknown keys are rejected.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_change_unknown_key_rejected(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify unknown configuration key returns NotSupported."""
        result = await wallbox.on_change_configuration(
            key="UnknownConfigKey",
            value="somevalue"
        )

        assert result.status == "NotSupported"


class TestTC018ChangeConfigurationReadOnly:
    """TC-018: ChangeConfiguration Read-Only Key (Negative).

    Purpose: Verify read-only keys cannot be changed.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_change_readonly_key_rejected(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify read-only keys are rejected."""
        result = await wallbox.on_change_configuration(
            key="NumberOfConnectors",
            value="2"
        )

        assert result.status == "Rejected"

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_change_connector_phase_rotation_rejected(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify ConnectorPhaseRotation is read-only."""
        result = await wallbox.on_change_configuration(
            key="ConnectorPhaseRotation",
            value="1.RST"
        )

        assert result.status == "Rejected"


class TestTC019ChangeConfigurationInvalidValue:
    """TC-019: ChangeConfiguration Invalid Value (Negative).

    Purpose: Verify invalid values are rejected.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_change_meter_interval_invalid(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify non-numeric MeterValueSampleInterval is rejected."""
        result = await wallbox.on_change_configuration(
            key="MeterValueSampleInterval",
            value="invalid"
        )

        assert result.status == "Rejected"

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_change_heartbeat_interval_invalid(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify non-numeric HeartbeatInterval is rejected."""
        result = await wallbox.on_change_configuration(
            key="HeartbeatInterval",
            value="abc"
        )

        assert result.status == "Rejected"


class TestTC015ConfigureRateUnitCurrent:
    """TC-015: Configure Charging Rate Unit — Current (A).

    Purpose: Verify current-based charging profiles work.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_current_based_profile_accepted(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify current-based (A) profile is accepted."""
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
                        "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 16.0}],
                    },
                },
            )

            assert result.status == "Accepted"
            assert wallbox.runtime.meter.current_limit_a == 16.0


class TestTC016ConfigureRateUnitPower:
    """TC-016: Configure Charging Rate Unit — Power (W).

    Purpose: Verify power-based charging profiles work.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_power_based_profile_accepted(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify power-based (W) profile is accepted."""
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
            # 11040W / (3 * 230V) ≈ 16A
            assert abs(wallbox.runtime.meter.current_limit_a - 16.0) < 0.1


class TestTC020SetChargingProfileAboveMax:
    """TC-020: SetChargingProfile Above Max Current (Negative).

    Purpose: Verify profiles exceeding max current are rejected.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_profile_above_max_rejected(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify profile above max current is rejected."""
        wallbox.runtime.transaction_id = 12345

        result = await wallbox.on_set_charging_profile(
            connector_id=1,
            cs_charging_profiles={
                "chargingProfileId": 1,
                "chargingSchedule": {
                    "chargingRateUnit": "A",
                    "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 100.0}],
                },
            },
        )

        assert result.status == "Rejected"


class TestTC021SetChargingProfileUnsupportedUnit:
    """TC-021: SetChargingProfile Unsupported Unit (Negative).

    Purpose: Verify unsupported rate units are rejected.
    """

    @pytest.mark.connection
    @pytest.mark.asyncio
    async def test_unsupported_unit_rejected(
        self,
        wallbox: WallboxChargePoint,
        event_log: EventLog,
    ):
        """Verify unsupported rate unit is rejected."""
        # Remove Power support
        wallbox.config["supported_rate_units"] = ["Current"]
        wallbox.runtime.transaction_id = 12345

        result = await wallbox.on_set_charging_profile(
            connector_id=1,
            cs_charging_profiles={
                "chargingProfileId": 1,
                "chargingSchedule": {
                    "chargingRateUnit": "W",
                    "chargingSchedulePeriod": [{"startPeriod": 0, "limit": 5000.0}],
                },
            },
        )

        assert result.status == "NotSupported"
