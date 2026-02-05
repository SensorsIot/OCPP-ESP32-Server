from __future__ import annotations

from dataclasses import dataclass


@dataclass
class EvState:
    plugged_in: bool = False

    def plug_in(self) -> None:
        self.plugged_in = True

    def unplug(self) -> None:
        self.plugged_in = False
