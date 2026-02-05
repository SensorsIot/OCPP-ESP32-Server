"""
Event log for capturing and querying test events.
"""

import asyncio
from datetime import datetime
from typing import Callable, Generic, List, Optional, Type, TypeVar

from .events import Event

T = TypeVar('T', bound=Event)


class EventLog:
    """Thread-safe event log with query capabilities"""

    def __init__(self):
        self._events: List[Event] = []
        self._lock = asyncio.Lock()
        self._listeners: List[Callable[[Event], None]] = []

    async def add(self, event: Event) -> None:
        """Add an event to the log"""
        async with self._lock:
            self._events.append(event)
            # Notify listeners
            for listener in self._listeners:
                try:
                    listener(event)
                except Exception:
                    pass  # Don't let listener errors break the log

    def add_sync(self, event: Event) -> None:
        """Add an event synchronously (for non-async contexts)"""
        self._events.append(event)
        for listener in self._listeners:
            try:
                listener(event)
            except Exception:
                pass

    def add_listener(self, callback: Callable[[Event], None]) -> None:
        """Add a listener for new events"""
        self._listeners.append(callback)

    def remove_listener(self, callback: Callable[[Event], None]) -> None:
        """Remove a listener"""
        if callback in self._listeners:
            self._listeners.remove(callback)

    def clear(self) -> None:
        """Clear all events (call before test action)"""
        self._events.clear()

    def find(
        self,
        event_type: Type[T],
        **filters
    ) -> Optional[T]:
        """Find first event matching type and filters"""
        for event in self._events:
            if isinstance(event, event_type):
                if self._matches_filters(event, filters):
                    return event
        return None

    def find_all(
        self,
        event_type: Type[T],
        **filters
    ) -> List[T]:
        """Find all events matching type and filters"""
        results = []
        for event in self._events:
            if isinstance(event, event_type):
                if self._matches_filters(event, filters):
                    results.append(event)
        return results

    def find_latest(
        self,
        event_type: Type[T],
        **filters
    ) -> Optional[T]:
        """Find the most recent event matching type and filters"""
        matches = self.find_all(event_type, **filters)
        return matches[-1] if matches else None

    def count(self, event_type: Type[T], **filters) -> int:
        """Count events matching type and filters"""
        return len(self.find_all(event_type, **filters))

    async def wait_for(
        self,
        event_type: Type[T],
        timeout: float = 10.0,
        poll_interval: float = 0.1,
        **filters
    ) -> T:
        """
        Wait for an event to appear.

        Args:
            event_type: Type of event to wait for
            timeout: Maximum time to wait in seconds
            poll_interval: How often to check for the event
            **filters: Additional filters to match

        Returns:
            The matching event

        Raises:
            TimeoutError: If event doesn't appear within timeout
        """
        start = datetime.utcnow()
        initial_count = len(self._events)

        while True:
            # Check events added since we started waiting
            for event in self._events[initial_count:]:
                if isinstance(event, event_type):
                    if self._matches_filters(event, filters):
                        return event

            # Also check all events (in case it was already there)
            found = self.find(event_type, **filters)
            if found:
                return found

            # Check timeout
            elapsed = (datetime.utcnow() - start).total_seconds()
            if elapsed >= timeout:
                raise TimeoutError(
                    f"Timeout waiting for {event_type.__name__} with {filters}"
                )

            await asyncio.sleep(poll_interval)

    def _matches_filters(self, event: Event, filters: dict) -> bool:
        """Check if event matches all filters"""
        for key, value in filters.items():
            actual = getattr(event, key, None)
            if actual != value:
                return False
        return True

    @property
    def all(self) -> List[Event]:
        """Get all events (for debugging)"""
        return list(self._events)

    @property
    def count_all(self) -> int:
        """Get total event count"""
        return len(self._events)

    def dump(self, filter_type: Optional[Type[Event]] = None) -> str:
        """Dump all events as string (for debugging)"""
        lines = []
        for event in self._events:
            if filter_type is None or isinstance(event, filter_type):
                lines.append(str(event))
        return "\n".join(lines)

    def print_all(self, filter_type: Optional[Type[Event]] = None) -> None:
        """Print all events to console"""
        print(self.dump(filter_type))

    def to_dict_list(self) -> List[dict]:
        """Convert all events to list of dicts (for JSON export)"""
        from dataclasses import asdict
        return [asdict(e) for e in self._events]
