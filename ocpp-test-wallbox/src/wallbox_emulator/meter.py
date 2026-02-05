from __future__ import annotations

from dataclasses import dataclass
from typing import List, Dict


@dataclass
class MeterState:
    voltage_v: float = 230.0
    power_factor: float = 1.0
    phase_mode: str = "3-phase"  # "1-phase" or "3-phase"
    current_limit_a: float = 0.0
    energy_wh: float = 0.0

    def set_phase_mode(self, mode: str) -> None:
        if mode not in ("1-phase", "3-phase"):
            raise ValueError(f"Invalid phase mode: {mode}")
        self.phase_mode = mode

    def set_current_limit(self, current_a: float) -> None:
        self.current_limit_a = max(0.0, current_a)

    @property
    def power_w(self) -> float:
        """Current power in Watts."""
        return self.instantaneous_power_w()

    @property
    def current_l1(self) -> float:
        """Current on L1 in Amps."""
        return self.current_limit_a

    @property
    def current_l2(self) -> float:
        """Current on L2 in Amps (0 for 1-phase)."""
        return self.current_limit_a if self.phase_mode == "3-phase" else 0.0

    @property
    def current_l3(self) -> float:
        """Current on L3 in Amps (0 for 1-phase)."""
        return self.current_limit_a if self.phase_mode == "3-phase" else 0.0

    def instantaneous_power_w(self) -> float:
        # Reported power is always 3-phase equivalent in this simulator.
        return 3.0 * self.voltage_v * self.current_limit_a * self.power_factor

    def advance(self, delta_seconds: float) -> None:
        power_w = self.instantaneous_power_w()
        self.energy_wh += power_w * (delta_seconds / 3600.0)

    def sampled_values(self) -> List[Dict[str, str]]:
        power_w = self.instantaneous_power_w()
        energy_wh = self.energy_wh

        if self.phase_mode == "3-phase":
            currents = {"L1": self.current_limit_a, "L2": self.current_limit_a, "L3": self.current_limit_a}
        else:
            currents = {"L1": self.current_limit_a, "L2": 0.0, "L3": 0.0}

        values: List[Dict[str, str]] = [
            {
                "value": f"{power_w:.0f}",
                "measurand": "Power.Active.Import",
                "unit": "W",
                "context": "Sample.Periodic",
                "location": "Outlet",
            },
            {
                "value": f"{energy_wh:.0f}",
                "measurand": "Energy.Active.Import.Register",
                "unit": "Wh",
                "context": "Sample.Periodic",
                "location": "Outlet",
            },
        ]

        for phase, current in currents.items():
            values.append(
                {
                    "value": f"{current:.1f}",
                    "measurand": "Current.Import",
                    "unit": "A",
                    "context": "Sample.Periodic",
                    "location": "Outlet",
                    "phase": phase,
                }
            )

        for phase in ("L1", "L2", "L3"):
            values.append(
                {
                    "value": f"{self.voltage_v:.1f}",
                    "measurand": "Voltage",
                    "unit": "V",
                    "context": "Sample.Periodic",
                    "location": "Outlet",
                    "phase": phase,
                }
            )

        return values
