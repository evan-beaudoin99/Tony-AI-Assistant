"""python -m tony.usb_bridge [COM5]: connects Tony's body to the server over the USB cable instead of WiFi.

Use this when the firmware is built with TONY_USE_USB true (firmware/include/config.h). Run `python -m tony` as
usual in one terminal and this in another. It reads framed messages from the board's USB-UART port and relays
them to the server's /ws/device WebSocket, so the server can't tell the difference. The board's log lines
are printed here too (the serial monitor can't have the port at the same time).

Frame: A5 5A, kind ('T' JSON text, 'B' binary, 'H' heartbeat), u32 little-endian payload length, payload, then
the low byte of the payload's sum. The bridge's heartbeat payload is one byte: 1 when it's connected to the server.
"""

from __future__ import annotations

import argparse
import asyncio
import queue
import struct
import sys
import threading
import time

SYNC = b"\xa5\x5a"
TEXT, BINARY, BEAT = ord("T"), ord("B"), ord("H")
KINDS = {TEXT, BINARY, BEAT}
HEADER = 7  # sync, kind, length
MAX_FRAME = 256 * 1024  # a VGA camera JPEG is ~30 KB
BEAT_EVERY = 1.0
DEVICE_TIMEOUT = 3.5
USB_UART_VIDS = {0x1A86, 0x10C4, 0x0403}  # WCH CH34x, Silicon Labs CP210x, FTDI


def frame(kind: int, payload: bytes) -> bytes:
    return SYNC + bytes([kind]) + struct.pack("<I", len(payload)) + payload + bytes([sum(payload) & 0xFF])


class FrameReader:
    """Splits the board's serial output into frames and log lines."""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.line = bytearray()

    def feed(self, data: bytes) -> list[tuple]:
        """Returns ("log", text) and ("frame", kind, payload) events, in order."""
        out: list[tuple] = []
        self.buf += data
        while self.buf:
            i = self.buf.find(SYNC)
            if i < 0:
                keep = 1 if self.buf.endswith(SYNC[:1]) else 0  # maybe the first half of a sync
                self._text(self.buf[: len(self.buf) - keep], out)
                del self.buf[: len(self.buf) - keep]
                break
            if i:
                self._text(self.buf[:i], out)
                del self.buf[:i]
            if len(self.buf) < HEADER:
                break
            kind = self.buf[2]
            (n,) = struct.unpack_from("<I", self.buf, 3)
            if kind not in KINDS or n > MAX_FRAME:
                del self.buf[:2]  # not a real frame start
                continue
            if len(self.buf) < HEADER + n + 1:
                break
            payload = bytes(self.buf[HEADER:HEADER + n])
            ok = self.buf[HEADER + n] == sum(payload) & 0xFF
            if not ok:
                del self.buf[:2]
                out.append(("log", "[bridge] dropped a damaged frame from the board"))
                continue
            del self.buf[:HEADER + n + 1]
            out.append(("frame", kind, payload))
        return out

    def _text(self, data: bytes, out: list[tuple]) -> None:
        self.line += data
        while (j := self.line.find(b"\n")) >= 0:
            out.append(("log", self.line[:j].decode("utf-8", "replace").rstrip("\r")))
            del self.line[: j + 1]


class Bridge:
    def __init__(self, ser, url: str) -> None:
        self.ser = ser
        self.url = url
        self.loop = asyncio.get_running_loop()
        self.to_server: asyncio.Queue = asyncio.Queue()
        self.writes: queue.Queue = queue.Queue()
        self.heard_at = 0.0
        self.link_up = False
        self.ws = None
        self.port_failed = asyncio.Event()

    # ── serial side (threads, so the blocking port never stalls the event loop) ──

    def read_port(self) -> None:
        reader = FrameReader()
        try:
            while True:
                data = self.ser.read(self.ser.in_waiting or 1)
                if data:
                    for event in reader.feed(data):
                        self.loop.call_soon_threadsafe(self.on_event, event)
        except Exception as e:  # unplugged, or another program grabbed the port
            print(f"[bridge] lost the USB port: {e}", flush=True)
            self.loop.call_soon_threadsafe(self.port_failed.set)

    def write_port(self) -> None:
        try:
            while True:
                self.ser.write(self.writes.get())
        except Exception:
            self.loop.call_soon_threadsafe(self.port_failed.set)

    def send(self, kind: int, payload: bytes) -> None:
        self.writes.put(frame(kind, payload))

    def on_event(self, event: tuple) -> None:
        if event[0] == "log":
            print(f"tony | {event[1]}", flush=True)
            return
        _, kind, payload = event
        self.heard_at = time.monotonic()
        if kind != BEAT and self.ws is not None:
            self.to_server.put_nowait((kind, payload))

    def device_alive(self) -> bool:
        return time.monotonic() - self.heard_at < DEVICE_TIMEOUT

    def set_link(self, up: bool) -> None:
        if up != self.link_up:
            self.link_up = up
            self.send(BEAT, b"\x01" if up else b"\x00")

    # ── server side ──

    async def heartbeat(self) -> None:
        while True:
            self.send(BEAT, b"\x01" if self.link_up else b"\x00")
            await asyncio.sleep(BEAT_EVERY)

    async def run(self) -> None:
        import websockets

        tasks = [asyncio.create_task(self.heartbeat()), asyncio.create_task(self.relay(websockets))]
        await self.port_failed.wait()
        for t in tasks:
            t.cancel()

    async def relay(self, websockets) -> None:
        waiting_said = False
        while True:
            if not self.device_alive():
                if not waiting_said:
                    print("[bridge] waiting for the board (is the firmware built with TONY_USE_USB true?)",
                          flush=True)
                    waiting_said = True
                await asyncio.sleep(0.2)
                continue
            waiting_said = False
            try:
                async with websockets.connect(self.url, max_size=None) as ws:
                    while not self.to_server.empty():
                        self.to_server.get_nowait()
                    self.ws = ws
                    self.set_link(True)
                    print("[bridge] board connected to the server", flush=True)
                    reason = await self.pump(ws)
                    print(f"[bridge] {reason}", flush=True)
            except (OSError, websockets.exceptions.WebSocketException) as e:
                print(f"[bridge] can't reach the server at {self.url.split('?')[0]} ({e}). "
                      f"Is `python -m tony` running?", flush=True)
                await asyncio.sleep(2)
            finally:
                self.ws = None
                self.set_link(False)

    async def pump(self, ws) -> str:
        async def server_to_board() -> str:
            async for msg in ws:
                if isinstance(msg, str):
                    self.send(TEXT, msg.encode())
                else:
                    self.send(BINARY, msg)
            return "the server closed the connection"

        async def board_to_server() -> str:
            while True:
                kind, payload = await self.to_server.get()
                await ws.send(payload.decode("utf-8", "replace") if kind == TEXT else payload)

        async def watchdog() -> str:
            while self.device_alive():
                await asyncio.sleep(0.5)
            return "the board went quiet, so I hung up on the server"

        tasks = [asyncio.create_task(c()) for c in (server_to_board, board_to_server, watchdog)]
        try:
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            return done.pop().result()
        finally:
            for t in tasks:
                t.cancel()


def pick_port(ports) -> str | None:
    """The only serial port, or the only one that looks like a USB-UART chip."""
    if len(ports) == 1:
        return ports[0].device
    likely = [p for p in ports if p.vid in USB_UART_VIDS]
    return likely[0].device if len(likely) == 1 else None


def server_url(port: int | None) -> str:
    token = ""
    try:
        from .config import load_settings

        settings = load_settings()
        port = port or settings.server.port
        token = settings.secrets.device_token
    except Exception:
        port = port or 8765
    return f"ws://127.0.0.1:{port}/ws/device?token={token}"


def main() -> None:
    parser = argparse.ArgumentParser(prog="python -m tony.usb_bridge", description=__doc__.split("\n")[0])
    parser.add_argument("port", nargs="?", help="the board's serial port, e.g. COM5 (found automatically if only one)")
    parser.add_argument("--baud", type=int, default=921600, help="must match TONY_USB_BAUD (default 921600)")
    parser.add_argument("--server-port", type=int, help="the server's port (default: server.port in config.yaml)")
    args = parser.parse_args()

    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        raise SystemExit("The USB bridge needs pyserial: pip install -r requirements.txt")

    port = args.port
    if not port:
        ports = list(list_ports.comports())
        port = pick_port(ports)
        if not port:
            listing = "\n".join(f"  {p.device}  {p.description}" for p in ports) or "  (none: is the board plugged in?)"
            raise SystemExit(f"Which port is Tony on? Pass it, e.g. `python -m tony.usb_bridge COM5`.\n{listing}")

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = args.baud
    ser.timeout = 0.2
    ser.dtr = False  # leave the reset and boot lines alone, or opening the port reboots the board
    ser.rts = False
    try:
        ser.open()
    except serial.SerialException as e:
        raise SystemExit(f"Can't open {port}: {e}\nClose any serial monitor (PlatformIO, Arduino) that has it open.")

    url = server_url(args.server_port)
    print(f"[bridge] {port} at {args.baud} baud  <->  {url.split('?')[0]}   (Ctrl+C to stop)", flush=True)

    async def go() -> None:
        bridge = Bridge(ser, url)
        threading.Thread(target=bridge.read_port, daemon=True).start()
        threading.Thread(target=bridge.write_port, daemon=True).start()
        await bridge.run()

    try:
        asyncio.run(go())
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    sys.exit(0)


if __name__ == "__main__":
    main()
