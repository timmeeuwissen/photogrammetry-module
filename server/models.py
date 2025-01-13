from dataclasses import dataclass
from typing import Optional
import time

@dataclass
class Board:
    ip_address: str
    token: str
    last_seen: float
    status: str = "idle"

    def is_alive(self) -> bool:
        return time.time() - self.last_seen < 30  # Consider board dead after 30s
