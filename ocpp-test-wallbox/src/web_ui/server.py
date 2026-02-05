from __future__ import annotations

import asyncio
import contextlib
import json
from typing import Set

from aiohttp import web, WSMsgType

from ..service import WallboxService


HTML = """<!doctype html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\" />
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
  <title>OCPP Wallbox Tester</title>
  <style>
    :root { --bg: #0f172a; --fg: #e2e8f0; --accent: #38bdf8; --muted: #64748b; }
    body { margin: 0; font-family: "IBM Plex Sans", sans-serif; background: linear-gradient(135deg, #0f172a, #1e293b); color: var(--fg); }
    .wrap { max-width: 980px; margin: 0 auto; padding: 24px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit,minmax(220px,1fr)); gap: 16px; }
    .card { background: rgba(15,23,42,0.8); border: 1px solid rgba(148,163,184,0.2); border-radius: 12px; padding: 16px; }
    h1 { font-weight: 600; margin: 0 0 16px; }
    button { background: var(--accent); color: #0f172a; border: none; padding: 10px 12px; border-radius: 10px; cursor: pointer; font-weight: 600; }
    button.secondary { background: transparent; border: 1px solid rgba(148,163,184,0.4); color: var(--fg); }
    .row { display: flex; gap: 8px; flex-wrap: wrap; }
    .muted { color: var(--muted); font-size: 0.9rem; }
    input, select { background: #0b1220; color: var(--fg); border: 1px solid rgba(148,163,184,0.4); border-radius: 8px; padding: 8px; }
  </style>
</head>
<body>
  <div class=\"wrap\">
    <h1>OCPP Wallbox Tester</h1>
    <div class=\"grid\">
      <div class=\"card\">
        <div class=\"muted\">Connection</div>
        <div id=\"connection\">-</div>
      </div>
      <div class=\"card\">
        <div class=\"muted\">Status</div>
        <div id=\"status\">-</div>
      </div>
      <div class=\"card\">
        <div class=\"muted\">Power</div>
        <div id=\"power\">-</div>
        <div class=\"muted\" id=\"energy\"></div>
      </div>
      <div class=\"card\">
        <div class=\"muted\">Phase Mode</div>
        <div id=\"phase\">-</div>
      </div>
    </div>

    <div class=\"card\" style=\"margin-top:16px;\">
      <div class=\"row\">
        <button onclick=\"action('plug')\">Plug In</button>
        <button class=\"secondary\" onclick=\"action('unplug')\">Unplug</button>
        <button onclick=\"startTx()\">Start Tx</button>
        <button class=\"secondary\" onclick=\"action('stop')\">Stop Tx</button>
      </div>
      <div class=\"row\" style=\"margin-top:12px;\">
        <select id=\"phaseSelect\">
          <option value=\"1-phase\">1-phase</option>
          <option value=\"3-phase\" selected>3-phase</option>
        </select>
        <button class=\"secondary\" onclick=\"setPhase()\">Set Phase</button>
        <label class=\"muted\" style=\"align-self:center;\">
          <input type=\"checkbox\" id=\"authRequired\" checked /> Authorize required
        </label>
        <button class=\"secondary\" onclick=\"setAuthorize()\">Apply</button>
      </div>
    </div>

    <div class=\"card\" style=\"margin-top:16px;\">
      <div class=\"muted\">OCPP Message Trace</div>
      <div id=\"logPane\" style=\"margin-top:8px; max-height:280px; overflow:auto; font-family: 'IBM Plex Mono', monospace; font-size: 0.85rem;\"></div>
    </div>
  </div>
<script>
const state = {};
const ws = new WebSocket(`ws://${location.host}/ws`);
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  document.getElementById('connection').textContent = data.connected ? 'Connected' : 'Disconnected';
  document.getElementById('status').textContent = data.connector_status;
  document.getElementById('power').textContent = `${data.power_w} W (limit ${data.current_limit_a} A)`;
  document.getElementById('energy').textContent = `${data.energy_wh} Wh`;
  document.getElementById('phase').textContent = data.phase_mode;
  document.getElementById('phaseSelect').value = data.phase_mode;
  document.getElementById('authRequired').checked = data.authorize_required;
  const logPane = document.getElementById('logPane');
  logPane.innerHTML = '';
  (data.logs || []).slice(-100).reverse().forEach((entry) => {
    const row = document.createElement('div');
    row.textContent = `[${entry.ts}] ${entry.dir} ${entry.action} ${entry.detail}`;
    logPane.appendChild(row);
  });
};

function action(cmd) {
  fetch(`/api/${cmd}`, { method: 'POST' });
}
function startTx() {
  const idTag = prompt('idTag', 'evcc') || 'evcc';
  fetch('/api/start', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ id_tag: idTag }) });
}
function setPhase() {
  const mode = document.getElementById('phaseSelect').value;
  fetch('/api/phase', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ mode }) });
}
function setAuthorize() {
  const enabled = document.getElementById('authRequired').checked;
  fetch('/api/authorize', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ enabled }) });
}
</script>
</body>
</html>
"""


class WebUiServer:
    def __init__(self, service: WallboxService, host: str, port: int) -> None:
        self.service = service
        self.host = host
        self.port = port
        self._sockets: Set[web.WebSocketResponse] = set()
        self._runner: web.AppRunner | None = None
        self._task: asyncio.Task[None] | None = None

    async def _broadcast_loop(self) -> None:
        while True:
            payload = json.dumps(self.service.get_state())
            for ws in set(self._sockets):
                if ws.closed:
                    self._sockets.discard(ws)
                    continue
                await ws.send_str(payload)
            await asyncio.sleep(1)

    async def _handle_index(self, request: web.Request) -> web.Response:
        return web.Response(text=HTML, content_type="text/html")

    async def _handle_state(self, request: web.Request) -> web.Response:
        return web.json_response(self.service.get_state())

    async def _handle_ws(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self._sockets.add(ws)
        await ws.send_str(json.dumps(self.service.get_state()))
        async for msg in ws:
            if msg.type == WSMsgType.ERROR:
                break
        self._sockets.discard(ws)
        return ws

    async def _handle_simple_action(self, request: web.Request, action: str) -> web.Response:
        if action == "plug":
            await self.service.plug_in()
        elif action == "unplug":
            await self.service.unplug()
        elif action == "stop":
            await self.service.stop_transaction()
        return web.json_response({"ok": True})

    async def _handle_start(self, request: web.Request) -> web.Response:
        data = await request.json()
        await self.service.start_transaction(data.get("id_tag", "evcc"))
        return web.json_response({"ok": True})

    async def _handle_phase(self, request: web.Request) -> web.Response:
        data = await request.json()
        await self.service.set_phase_mode(data.get("mode", "3-phase"))
        return web.json_response({"ok": True})

    async def _handle_authorize(self, request: web.Request) -> web.Response:
        data = await request.json()
        await self.service.set_authorize_required(bool(data.get("enabled", True)))
        return web.json_response({"ok": True})

    async def start(self) -> None:
        app = web.Application()
        app.router.add_get("/", self._handle_index)
        app.router.add_get("/api/state", self._handle_state)
        app.router.add_get("/ws", self._handle_ws)
        app.router.add_post("/api/plug", lambda r: self._handle_simple_action(r, "plug"))
        app.router.add_post("/api/unplug", lambda r: self._handle_simple_action(r, "unplug"))
        app.router.add_post("/api/stop", lambda r: self._handle_simple_action(r, "stop"))
        app.router.add_post("/api/start", self._handle_start)
        app.router.add_post("/api/phase", self._handle_phase)
        app.router.add_post("/api/authorize", self._handle_authorize)

        self._runner = web.AppRunner(app)
        await self._runner.setup()
        site = web.TCPSite(self._runner, host=self.host, port=self.port)
        await site.start()
        self._task = asyncio.create_task(self._broadcast_loop())

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._task
        if self._runner:
            await self._runner.cleanup()
