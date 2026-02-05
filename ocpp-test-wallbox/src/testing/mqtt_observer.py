"""
MQTT Observer for capturing broker messages during tests.

Subscribes to MQTT topics and logs messages as events for test assertions.
"""

import asyncio
import json
import logging
from typing import Any, Dict, List, Optional

from aiomqtt import Client, MqttError

from .event_log import EventLog
from .events import MqttMessage


class MqttObserver:
    """Observes MQTT broker and captures messages for test assertions"""

    def __init__(
        self,
        event_log: EventLog,
        host: str = "localhost",
        port: int = 1883,
        topics: Optional[List[str]] = None,
    ):
        self.event_log = event_log
        self.host = host
        self.port = port
        self.topics = topics or ["ocpp/#"]
        self.logger = logging.getLogger("testing.mqtt_observer")
        self._client: Optional[Client] = None
        self._task: Optional[asyncio.Task] = None
        self._running = False

    async def start(self) -> None:
        """Start observing MQTT messages"""
        if self._running:
            return

        self._running = True
        self._task = asyncio.create_task(self._observe_loop())
        self.logger.info("MQTT observer started for %s:%d", self.host, self.port)

    async def stop(self) -> None:
        """Stop observing MQTT messages"""
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None
        self.logger.info("MQTT observer stopped")

    async def _observe_loop(self) -> None:
        """Main observation loop with reconnection handling"""
        while self._running:
            try:
                async with Client(self.host, self.port) as client:
                    self._client = client
                    # Subscribe to all configured topics
                    for topic in self.topics:
                        await client.subscribe(topic)
                        self.logger.debug("Subscribed to %s", topic)

                    # Process incoming messages
                    async for message in client.messages:
                        if not self._running:
                            break
                        await self._handle_message(
                            str(message.topic),
                            message.payload
                        )

            except MqttError as e:
                self.logger.warning("MQTT connection error: %s", e)
                if self._running:
                    await asyncio.sleep(1.0)  # Reconnect delay
            except asyncio.CancelledError:
                break
            except Exception as e:
                self.logger.error("Unexpected error in MQTT observer: %s", e)
                if self._running:
                    await asyncio.sleep(1.0)

    async def _handle_message(self, topic: str, payload: bytes) -> None:
        """Handle an incoming MQTT message"""
        try:
            # Try to decode as JSON
            payload_str = payload.decode("utf-8")
            try:
                payload_dict = json.loads(payload_str)
            except json.JSONDecodeError:
                payload_dict = {"raw": payload_str}

            # Create and log the event
            event = MqttMessage(
                topic=topic,
                payload=payload_dict,
            )
            await self.event_log.add(event)
            self.logger.debug("MQTT message captured: %s", topic)

        except Exception as e:
            self.logger.error("Error handling MQTT message: %s", e)

    async def publish(self, topic: str, payload: Dict[str, Any]) -> None:
        """Publish a message to MQTT (for test actions)"""
        if self._client is None:
            raise RuntimeError("MQTT observer not connected")

        payload_bytes = json.dumps(payload).encode("utf-8")
        await self._client.publish(topic, payload_bytes)
        self.logger.debug("Published to %s: %s", topic, payload)

    @property
    def connected(self) -> bool:
        """Check if connected to MQTT broker"""
        return self._client is not None and self._running


class MqttObserverContext:
    """Context manager for MQTT observer"""

    def __init__(
        self,
        event_log: EventLog,
        host: str = "localhost",
        port: int = 1883,
        topics: Optional[List[str]] = None,
    ):
        self.observer = MqttObserver(event_log, host, port, topics)

    async def __aenter__(self) -> MqttObserver:
        await self.observer.start()
        return self.observer

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        await self.observer.stop()
