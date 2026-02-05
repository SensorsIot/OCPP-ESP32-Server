"""
Web UI for test runner.

Provides HTML interface to view and run tests with live progress.
"""

import asyncio
import contextlib
import json
import logging
from typing import Any, Dict, Optional, Set

from aiohttp import web, WSMsgType

from .test_runner import TestRunner, TestSuite


TEST_UI_HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>OCPP Test Runner</title>
  <style>
    :root {
      --bg: #0f172a;
      --bg-card: rgba(15,23,42,0.9);
      --fg: #e2e8f0;
      --accent: #38bdf8;
      --success: #22c55e;
      --error: #ef4444;
      --warning: #f59e0b;
      --muted: #64748b;
      --border: rgba(148,163,184,0.2);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Inter", "Segoe UI", sans-serif;
      background: linear-gradient(135deg, #0f172a, #1e293b);
      color: var(--fg);
      min-height: 100vh;
    }
    .container { max-width: 1200px; margin: 0 auto; padding: 24px; }

    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 24px;
    }
    h1 { font-weight: 600; margin: 0; font-size: 1.5rem; }

    .controls {
      display: flex;
      gap: 12px;
      align-items: center;
    }
    input[type="text"] {
      background: var(--bg);
      color: var(--fg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 10px 14px;
      font-size: 0.9rem;
      width: 240px;
    }
    input[type="text"]:focus {
      outline: none;
      border-color: var(--accent);
    }
    button {
      background: var(--accent);
      color: #0f172a;
      border: none;
      padding: 10px 20px;
      border-radius: 8px;
      cursor: pointer;
      font-weight: 600;
      font-size: 0.9rem;
      transition: opacity 0.2s;
    }
    button:hover { opacity: 0.9; }
    button:disabled { opacity: 0.5; cursor: not-allowed; }
    button.secondary {
      background: transparent;
      border: 1px solid var(--border);
      color: var(--fg);
    }
    button.danger { background: var(--error); color: white; }

    .summary {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 16px;
      margin-bottom: 24px;
    }
    .stat-card {
      background: var(--bg-card);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 16px;
      text-align: center;
    }
    .stat-value {
      font-size: 2rem;
      font-weight: 700;
      line-height: 1;
    }
    .stat-label {
      color: var(--muted);
      font-size: 0.85rem;
      margin-top: 4px;
    }
    .stat-value.passed { color: var(--success); }
    .stat-value.failed { color: var(--error); }
    .stat-value.running { color: var(--accent); }
    .stat-value.skipped { color: var(--warning); }

    .progress-bar {
      height: 8px;
      background: var(--bg);
      border-radius: 4px;
      overflow: hidden;
      margin-bottom: 24px;
    }
    .progress-fill {
      height: 100%;
      display: flex;
      transition: width 0.3s ease;
    }
    .progress-passed { background: var(--success); }
    .progress-failed { background: var(--error); }
    .progress-skipped { background: var(--warning); }
    .progress-running { background: var(--accent); animation: pulse 1s infinite; }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.6; } }

    .test-list {
      background: var(--bg-card);
      border: 1px solid var(--border);
      border-radius: 12px;
      overflow: hidden;
    }
    .test-item {
      display: flex;
      align-items: center;
      padding: 12px 16px;
      border-bottom: 1px solid var(--border);
      gap: 12px;
    }
    .test-item:last-child { border-bottom: none; }
    .test-status {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      flex-shrink: 0;
    }
    .test-status.pending { background: var(--muted); }
    .test-status.running { background: var(--accent); animation: pulse 1s infinite; }
    .test-status.passed { background: var(--success); }
    .test-status.failed { background: var(--error); }
    .test-status.skipped { background: var(--warning); }
    .test-status.error { background: var(--error); }
    .test-name {
      flex: 1;
      font-family: "Fira Code", "Consolas", monospace;
      font-size: 0.85rem;
      word-break: break-all;
    }
    .test-duration {
      color: var(--muted);
      font-size: 0.8rem;
      min-width: 60px;
      text-align: right;
    }
    .test-error {
      background: rgba(239, 68, 68, 0.1);
      color: var(--error);
      padding: 8px 12px;
      margin: 0 16px 12px 38px;
      border-radius: 6px;
      font-family: monospace;
      font-size: 0.8rem;
      white-space: pre-wrap;
    }

    .empty-state {
      text-align: center;
      padding: 48px;
      color: var(--muted);
    }
    .empty-state h2 { color: var(--fg); margin-bottom: 8px; }

    .log-panel {
      background: var(--bg-card);
      border: 1px solid var(--border);
      border-radius: 12px;
      margin-top: 24px;
      padding: 16px;
    }
    .log-title {
      color: var(--muted);
      font-size: 0.85rem;
      margin-bottom: 12px;
    }
    .log-output {
      font-family: "Fira Code", "Consolas", monospace;
      font-size: 0.8rem;
      max-height: 200px;
      overflow-y: auto;
      white-space: pre-wrap;
      color: var(--muted);
    }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>OCPP Test Runner</h1>
      <div class="controls">
        <input type="text" id="pattern" placeholder="Filter tests (e.g., TC-100)" />
        <button id="runBtn" onclick="runTests()">Run Tests</button>
        <button id="stopBtn" class="danger" onclick="stopTests()" style="display:none;">Stop</button>
      </div>
    </header>

    <div class="summary">
      <div class="stat-card">
        <div class="stat-value" id="totalCount">0</div>
        <div class="stat-label">Total</div>
      </div>
      <div class="stat-card">
        <div class="stat-value passed" id="passedCount">0</div>
        <div class="stat-label">Passed</div>
      </div>
      <div class="stat-card">
        <div class="stat-value failed" id="failedCount">0</div>
        <div class="stat-label">Failed</div>
      </div>
      <div class="stat-card">
        <div class="stat-value running" id="runningCount">0</div>
        <div class="stat-label">Running</div>
      </div>
      <div class="stat-card">
        <div class="stat-value skipped" id="skippedCount">0</div>
        <div class="stat-label">Skipped</div>
      </div>
      <div class="stat-card">
        <div class="stat-value" id="durationValue">0s</div>
        <div class="stat-label">Duration</div>
      </div>
    </div>

    <div class="progress-bar">
      <div class="progress-fill" id="progressFill"></div>
    </div>

    <div class="test-list" id="testList">
      <div class="empty-state">
        <h2>No tests running</h2>
        <p>Enter a filter pattern and click "Run Tests" to start</p>
      </div>
    </div>
  </div>

<script>
let ws;
let isRunning = false;

function connect() {
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    updateUI(data);
  };
  ws.onclose = () => {
    setTimeout(connect, 1000);
  };
}

function updateUI(data) {
  if (!data.summary) return;

  const s = data.summary;
  document.getElementById('totalCount').textContent = s.total;
  document.getElementById('passedCount').textContent = s.passed;
  document.getElementById('failedCount').textContent = s.failed;
  document.getElementById('runningCount').textContent = s.running;
  document.getElementById('skippedCount').textContent = s.skipped;
  document.getElementById('durationValue').textContent = formatDuration(s.duration_ms);

  // Update progress bar
  const total = s.total || 1;
  const progressFill = document.getElementById('progressFill');
  progressFill.innerHTML = '';

  if (s.passed > 0) {
    const el = document.createElement('div');
    el.className = 'progress-passed';
    el.style.width = (s.passed / total * 100) + '%';
    progressFill.appendChild(el);
  }
  if (s.failed > 0) {
    const el = document.createElement('div');
    el.className = 'progress-failed';
    el.style.width = (s.failed / total * 100) + '%';
    progressFill.appendChild(el);
  }
  if (s.skipped > 0) {
    const el = document.createElement('div');
    el.className = 'progress-skipped';
    el.style.width = (s.skipped / total * 100) + '%';
    progressFill.appendChild(el);
  }
  if (s.running > 0) {
    const el = document.createElement('div');
    el.className = 'progress-running';
    el.style.width = (s.running / total * 100) + '%';
    progressFill.appendChild(el);
  }

  // Update test list
  const testList = document.getElementById('testList');
  if (!data.tests || data.tests.length === 0) {
    testList.innerHTML = '<div class="empty-state"><h2>No tests found</h2></div>';
    return;
  }

  let html = '';
  for (const test of data.tests) {
    const shortName = test.name.split('::').pop();
    html += `
      <div class="test-item">
        <div class="test-status ${test.status}"></div>
        <div class="test-name" title="${test.name}">${shortName}</div>
        <div class="test-duration">${test.duration_ms > 0 ? formatDuration(test.duration_ms) : ''}</div>
      </div>
    `;
    if (test.error_message) {
      html += `<div class="test-error">${escapeHtml(test.error_message)}</div>`;
    }
  }
  testList.innerHTML = html;

  // Update buttons
  isRunning = s.running > 0;
  document.getElementById('runBtn').style.display = isRunning ? 'none' : 'inline-block';
  document.getElementById('stopBtn').style.display = isRunning ? 'inline-block' : 'none';
}

function formatDuration(ms) {
  if (ms < 1000) return Math.round(ms) + 'ms';
  return (ms / 1000).toFixed(1) + 's';
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

function runTests() {
  const pattern = document.getElementById('pattern').value;
  fetch('/api/run', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ pattern: pattern || null })
  });
}

function stopTests() {
  fetch('/api/stop', { method: 'POST' });
}

connect();
</script>
</body>
</html>
"""


class TestRunnerUI:
    """Web UI server for test runner"""

    def __init__(
        self,
        test_runner: TestRunner,
        host: str = "0.0.0.0",
        port: int = 8080,
    ):
        self.test_runner = test_runner
        self.host = host
        self.port = port
        self.logger = logging.getLogger("testing.ui")
        self._sockets: Set[web.WebSocketResponse] = set()
        self._runner: Optional[web.AppRunner] = None
        self._broadcast_task: Optional[asyncio.Task] = None

        # Register for progress updates
        self.test_runner.add_progress_callback(self._on_progress)

    def _on_progress(self, data: Dict[str, Any]) -> None:
        """Handle progress update from test runner"""
        # Schedule broadcast in the event loop
        asyncio.create_task(self._broadcast(data))

    async def _broadcast(self, data: Dict[str, Any]) -> None:
        """Broadcast data to all connected WebSocket clients"""
        payload = json.dumps(data)
        for ws in set(self._sockets):
            if ws.closed:
                self._sockets.discard(ws)
                continue
            try:
                await ws.send_str(payload)
            except Exception:
                self._sockets.discard(ws)

    async def _handle_index(self, request: web.Request) -> web.Response:
        """Serve the main HTML page"""
        return web.Response(text=TEST_UI_HTML, content_type="text/html")

    async def _handle_ws(self, request: web.Request) -> web.WebSocketResponse:
        """Handle WebSocket connection"""
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self._sockets.add(ws)

        # Send current state
        if self.test_runner.current_suite:
            await ws.send_str(json.dumps(self.test_runner.current_suite.to_dict()))
        else:
            await ws.send_str(json.dumps({"summary": {"total": 0, "passed": 0, "failed": 0, "running": 0, "skipped": 0, "pending": 0, "duration_ms": 0}, "tests": []}))

        async for msg in ws:
            if msg.type == WSMsgType.ERROR:
                break

        self._sockets.discard(ws)
        return ws

    async def _handle_run(self, request: web.Request) -> web.Response:
        """Start running tests"""
        if self.test_runner.is_running:
            return web.json_response({"error": "Tests already running"}, status=400)

        data = await request.json()
        pattern = data.get("pattern")
        markers = data.get("markers")

        # Run tests in background
        asyncio.create_task(self._run_tests(pattern, markers))
        return web.json_response({"ok": True, "message": "Tests started"})

    async def _run_tests(self, pattern: Optional[str], markers: Optional[list]) -> None:
        """Run tests in background"""
        try:
            await self.test_runner.run_tests(pattern=pattern, markers=markers)
        except Exception as e:
            self.logger.error("Test run failed: %s", e)

    async def _handle_stop(self, request: web.Request) -> web.Response:
        """Stop running tests"""
        await self.test_runner.stop()
        return web.json_response({"ok": True, "message": "Tests stopped"})

    async def _handle_status(self, request: web.Request) -> web.Response:
        """Get current test status"""
        if self.test_runner.current_suite:
            return web.json_response(self.test_runner.current_suite.to_dict())
        return web.json_response({"tests": [], "summary": {"total": 0}})

    async def start(self) -> None:
        """Start the web server"""
        app = web.Application()
        app.router.add_get("/", self._handle_index)
        app.router.add_get("/ws", self._handle_ws)
        app.router.add_post("/api/run", self._handle_run)
        app.router.add_post("/api/stop", self._handle_stop)
        app.router.add_get("/api/status", self._handle_status)

        self._runner = web.AppRunner(app)
        await self._runner.setup()
        site = web.TCPSite(self._runner, host=self.host, port=self.port)
        await site.start()
        self.logger.info("Test Runner UI started at http://%s:%d", self.host, self.port)

    async def stop(self) -> None:
        """Stop the web server"""
        if self._broadcast_task:
            self._broadcast_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._broadcast_task
        if self._runner:
            await self._runner.cleanup()
        self.logger.info("Test Runner UI stopped")
