"""HTTP and WebSocket routes: /ws/device for Tony's body, / and /ws/dashboard for the browser."""

from __future__ import annotations

import base64
import hmac
import json
import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, PlainTextResponse

from .config import Settings
from .device import DashboardLink, DeviceLink
from .hub import Tony
from .protocol import CAMERA_FRAME, MIC_AUDIO

log = logging.getLogger(__name__)

STATIC = Path(__file__).parent / "static"
LOCAL = {"127.0.0.1", "::1", "localhost"}


def create_app(settings: Settings) -> FastAPI:
    tony = Tony(settings)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        await tony.startup()
        yield
        await tony.shutdown()

    app = FastAPI(title="Tony", lifespan=lifespan)
    app.state.tony = tony

    def dashboard_allowed(host: str | None) -> bool:
        return settings.server.dashboard_lan_access or (host or "") in LOCAL

    @app.get("/")
    async def dashboard(request: Request):
        if not dashboard_allowed(request.client.host if request.client else None):
            return PlainTextResponse("The dashboard only opens on the server's own computer "
                                     "(set server.dashboard_lan_access in config.yaml).", status_code=403)
        return FileResponse(STATIC / "dashboard.html")

    @app.get("/face.js")
    async def face_script(request: Request):
        if not dashboard_allowed(request.client.host if request.client else None):
            return PlainTextResponse("", status_code=403)
        return FileResponse(STATIC / "face.js", media_type="text/javascript")

    @app.websocket("/ws/device")
    async def device_socket(ws: WebSocket, token: str = ""):
        # TODO: token check disabled for now. Re-enable before using Tony on a shared network.
        # expected = settings.secrets.device_token
        # if expected and not hmac.compare_digest(token.encode(), expected.encode()):
        #     log.warning("Turned away a device with the wrong token from %s", ws.client.host if ws.client else "?")
        #     await ws.close(code=1008)
        #     return
        await ws.accept()
        dev = DeviceLink(ws)
        await tony.attach_device(dev)
        try:
            while True:
                msg = await ws.receive()
                if msg["type"] == "websocket.disconnect":
                    break
                if msg.get("bytes"):
                    data = msg["bytes"]
                    if data[0] == MIC_AUDIO:
                        await tony.on_mic(data[1:])
                    elif data[0] == CAMERA_FRAME:
                        dev.got_frame(data[1:])
                elif msg.get("text"):
                    try:
                        obj = json.loads(msg["text"])
                    except json.JSONDecodeError:
                        continue
                    await tony.on_device_message(dev, obj)
        except WebSocketDisconnect:
            pass
        finally:
            await tony.detach_device(dev)

    @app.websocket("/ws/dashboard")
    async def dashboard_socket(ws: WebSocket):
        if not dashboard_allowed(ws.client.host if ws.client else None):
            await ws.close(code=1008)
            return
        await ws.accept()
        dash = DashboardLink(ws)
        tony.dashboards.add(dash)
        try:
            await dash.send_json(tony.status())
            for entry in tony.history:
                await dash.send_json(entry)
            if tony.last_card:
                await dash.send_json({"type": "card", "jpeg": base64.b64encode(tony.last_card).decode(), "replay": True})
            while True:
                msg = await ws.receive_json()
                await handle_dashboard(tony, dash, msg)
        except (WebSocketDisconnect, RuntimeError):
            pass
        finally:
            tony.dashboards.discard(dash)

    return app


async def handle_dashboard(tony: Tony, dash: DashboardLink, msg: dict) -> None:
    kind = msg.get("type")
    if kind == "say" and str(msg.get("text", "")).strip():
        await tony.typed(str(msg["text"]).strip())
    elif kind == "camera":
        if await tony.set_camera(bool(msg.get("on"))) is None:
            await tony.note("system", "No body connected, so there's no camera.")
    elif kind == "sleep":
        await tony.set_sleep(bool(msg.get("on")))
    elif kind == "peek":
        if not await tony.snapshot():
            await tony.note("system", "No photo: the camera's off or Tony's not connected.")
    elif kind == "hear":
        dash.hear = bool(msg.get("on"))
    elif kind == "stop":
        await tony.interrupt()
    elif kind == "volume":
        tony.s.tts.volume = max(0, min(100, int(msg.get("value", 70))))
        if tony.device:
            await tony.device.send_json({"type": "volume", "value": tony.s.tts.volume})
        await tony.broadcast_status()
    elif kind == "forget":
        tony.brain.forget()
        await tony.note("system", "Tony forgot the conversation so far.")
