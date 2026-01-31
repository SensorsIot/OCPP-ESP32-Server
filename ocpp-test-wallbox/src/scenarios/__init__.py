"""Test Scenarios - Automated test sequences."""

from .base import BaseScenario, ScenarioResult
from .basic_charge import BasicChargeScenario
from .phase_switch import PhaseSwitchScenario
from .power_limit import PowerLimitScenario
from .stress_test import StressTestScenario

__all__ = [
    "BaseScenario",
    "ScenarioResult",
    "BasicChargeScenario",
    "PhaseSwitchScenario",
    "PowerLimitScenario",
    "StressTestScenario",
]
