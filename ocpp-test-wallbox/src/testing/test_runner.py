"""
Test runner with live progress reporting.

Executes pytest tests and broadcasts progress via WebSocket.
"""

import asyncio
import json
import logging
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Any, Callable, Dict, List, Optional


class TestStatus(Enum):
    PENDING = "pending"
    RUNNING = "running"
    PASSED = "passed"
    FAILED = "failed"
    SKIPPED = "skipped"
    ERROR = "error"


@dataclass
class TestResult:
    """Result of a single test"""
    name: str
    status: TestStatus = TestStatus.PENDING
    duration_ms: float = 0.0
    error_message: Optional[str] = None
    started_at: Optional[datetime] = None
    finished_at: Optional[datetime] = None

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "status": self.status.value,
            "duration_ms": self.duration_ms,
            "error_message": self.error_message,
            "started_at": self.started_at.isoformat() if self.started_at else None,
            "finished_at": self.finished_at.isoformat() if self.finished_at else None,
        }


@dataclass
class TestSuite:
    """Collection of test results"""
    name: str
    tests: List[TestResult] = field(default_factory=list)
    started_at: Optional[datetime] = None
    finished_at: Optional[datetime] = None

    @property
    def total(self) -> int:
        return len(self.tests)

    @property
    def passed(self) -> int:
        return sum(1 for t in self.tests if t.status == TestStatus.PASSED)

    @property
    def failed(self) -> int:
        return sum(1 for t in self.tests if t.status == TestStatus.FAILED)

    @property
    def skipped(self) -> int:
        return sum(1 for t in self.tests if t.status == TestStatus.SKIPPED)

    @property
    def running(self) -> int:
        return sum(1 for t in self.tests if t.status == TestStatus.RUNNING)

    @property
    def pending(self) -> int:
        return sum(1 for t in self.tests if t.status == TestStatus.PENDING)

    @property
    def duration_ms(self) -> float:
        return sum(t.duration_ms for t in self.tests)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "tests": [t.to_dict() for t in self.tests],
            "summary": {
                "total": self.total,
                "passed": self.passed,
                "failed": self.failed,
                "skipped": self.skipped,
                "running": self.running,
                "pending": self.pending,
                "duration_ms": self.duration_ms,
            },
            "started_at": self.started_at.isoformat() if self.started_at else None,
            "finished_at": self.finished_at.isoformat() if self.finished_at else None,
        }


ProgressCallback = Callable[[Dict[str, Any]], None]


class TestRunner:
    """Runs pytest tests with progress reporting"""

    def __init__(self, test_path: str = "tests"):
        self.test_path = test_path
        self.logger = logging.getLogger("testing.runner")
        self._callbacks: List[ProgressCallback] = []
        self._suite: Optional[TestSuite] = None
        self._running = False
        self._process: Optional[subprocess.Popen] = None

    def add_progress_callback(self, callback: ProgressCallback) -> None:
        """Add a callback to be notified of progress updates"""
        self._callbacks.append(callback)

    def remove_progress_callback(self, callback: ProgressCallback) -> None:
        """Remove a progress callback"""
        if callback in self._callbacks:
            self._callbacks.remove(callback)

    def _notify_progress(self) -> None:
        """Notify all callbacks of current progress"""
        if self._suite is None:
            return
        data = self._suite.to_dict()
        for callback in self._callbacks:
            try:
                callback(data)
            except Exception as e:
                self.logger.error("Progress callback error: %s", e)

    async def discover_tests(self, pattern: Optional[str] = None) -> List[str]:
        """Discover available tests"""
        cmd = [
            sys.executable, "-m", "pytest",
            self.test_path,
            "--collect-only", "-q"
        ]
        if pattern:
            cmd.extend(["-k", pattern])

        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, _ = await proc.communicate()

        tests = []
        for line in stdout.decode().strip().split("\n"):
            line = line.strip()
            if line and "::" in line and not line.startswith(("=", "-", " ")):
                tests.append(line)
        return tests

    async def run_tests(
        self,
        pattern: Optional[str] = None,
        markers: Optional[List[str]] = None,
    ) -> TestSuite:
        """Run tests and return results"""
        if self._running:
            raise RuntimeError("Tests already running")

        self._running = True
        self._suite = TestSuite(name=pattern or "all")
        self._suite.started_at = datetime.utcnow()

        try:
            # Discover tests first
            test_names = await self.discover_tests(pattern)
            for name in test_names:
                self._suite.tests.append(TestResult(name=name))

            self._notify_progress()

            # Build pytest command
            cmd = [
                sys.executable, "-m", "pytest",
                self.test_path,
                "-v",
                "--tb=short",
            ]
            if pattern:
                cmd.extend(["-k", pattern])
            if markers:
                for marker in markers:
                    cmd.extend(["-m", marker])

            # Run pytest with streaming output
            proc = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self._process = proc

            # Parse output line by line
            current_test = None
            while True:
                line = await proc.stdout.readline()
                if not line:
                    break
                line_str = line.decode().strip()
                self._parse_output_line(line_str)
                self._notify_progress()

            await proc.wait()

            # Mark any still-pending tests based on final state
            for test in self._suite.tests:
                if test.status == TestStatus.RUNNING:
                    test.status = TestStatus.ERROR
                    test.error_message = "Test did not complete"
                    test.finished_at = datetime.utcnow()

            self._suite.finished_at = datetime.utcnow()
            self._notify_progress()

            return self._suite

        finally:
            self._running = False
            self._process = None

    def _parse_output_line(self, line: str) -> None:
        """Parse pytest output line and update test status"""
        if not self._suite:
            return

        # Look for test result lines like:
        # tests/test_foo.py::test_something PASSED
        # tests/test_foo.py::test_something FAILED
        for status_text, status in [
            (" PASSED", TestStatus.PASSED),
            (" FAILED", TestStatus.FAILED),
            (" SKIPPED", TestStatus.SKIPPED),
            (" ERROR", TestStatus.ERROR),
        ]:
            if status_text in line:
                # Extract test name
                parts = line.split(status_text)
                if parts:
                    test_name = parts[0].strip()
                    for test in self._suite.tests:
                        if test.name == test_name or test_name.endswith(test.name.split("::")[-1]):
                            test.status = status
                            test.finished_at = datetime.utcnow()
                            if test.started_at:
                                test.duration_ms = (test.finished_at - test.started_at).total_seconds() * 1000
                            break
                return

        # Look for running test
        if "::" in line and " " not in line.split("::")[-1]:
            test_name = line.strip()
            for test in self._suite.tests:
                if test.name == test_name or test_name.endswith(test.name.split("::")[-1]):
                    test.status = TestStatus.RUNNING
                    test.started_at = datetime.utcnow()
                    break

    async def stop(self) -> None:
        """Stop running tests"""
        if self._process:
            self._process.terminate()
            try:
                await asyncio.wait_for(
                    asyncio.create_task(self._process.wait()),
                    timeout=5.0
                )
            except asyncio.TimeoutError:
                self._process.kill()

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def current_suite(self) -> Optional[TestSuite]:
        return self._suite
