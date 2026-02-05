"""
Testing framework for OCPP Wallbox Emulator.

Provides event capture, assertions, and test utilities.
"""

from .events import (
    Event,
    MessageReceived,
    MessageSent,
    StateChange,
    MeterUpdate,
    ProfileApplied,
    TransactionStarted,
    TransactionStopped,
    MqttMessage,
    ConnectionEvent,
    AuthorizationEvent,
)
from .event_log import EventLog
from .mqtt_observer import MqttObserver, MqttObserverContext
from .test_runner import TestRunner, TestResult, TestSuite, TestStatus
from .test_ui import TestRunnerUI

__all__ = [
    # Events
    "Event",
    "MessageReceived",
    "MessageSent",
    "StateChange",
    "MeterUpdate",
    "ProfileApplied",
    "TransactionStarted",
    "TransactionStopped",
    "MqttMessage",
    "ConnectionEvent",
    "AuthorizationEvent",
    # Event Log
    "EventLog",
    # MQTT Observer
    "MqttObserver",
    "MqttObserverContext",
    # Test Runner
    "TestRunner",
    "TestResult",
    "TestSuite",
    "TestStatus",
    "TestRunnerUI",
]
