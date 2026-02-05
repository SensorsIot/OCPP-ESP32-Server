#!/usr/bin/env python3
"""Launch the Test Runner Web UI."""

import asyncio
import logging

from src.testing import TestRunner, TestRunnerUI

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s"
)


async def main():
    runner = TestRunner(test_path="tests")
    ui = TestRunnerUI(runner, host="0.0.0.0", port=8081)

    await ui.start()
    print("Test Runner UI available at http://localhost:8081")

    # Keep running
    try:
        while True:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        await ui.stop()


if __name__ == "__main__":
    asyncio.run(main())
