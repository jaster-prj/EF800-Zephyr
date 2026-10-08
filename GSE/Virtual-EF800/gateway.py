#!/usr/bin/env python3
"""Localhost web gateway for the Virtual EF800 front-panel protocol."""
import argparse
import asyncio
from collections import deque
import json
import os
import struct
import zlib
from contextlib import suppress

from aiohttp import WSMsgType, web

from protocol import (FRAME_SIZE, MSG_ACK, MSG_CAPABILITIES, MSG_KEY_DOWN,
                       MSG_KEY_RELEASE_ALL, MSG_KEY_UP, MSG_LCD_FRAME,
                       MSG_PLUGIN_STATUS, MSG_PLUGIN_UPLOAD_BEGIN,
                       MSG_PLUGIN_UPLOAD_CHUNK, MSG_PLUGIN_UPLOAD_COMMIT,
                       MSG_SIMULATOR_RESTART, MSG_PROFILE_CONTROL,
                       MSG_PLUGIN_ASSIGN,
                       MSG_PLUGIN_EXECUTE, MSG_LOG_RECORD,
                       ProtocolError, FrameParser, decode_lcd_frame,
                       decode_log_record, encode_message, LOG_HISTORY_BYTES,
                       LOG_HISTORY_COUNT, LOG_REPLAY)

MAX_BROWSER_MESSAGE = 1024
MAX_PLUGIN_PACKAGE = 32 * 1024
VALID_KEYS = {"up", "down", "left", "right", "enter", "esc"}


class Gateway:
    def __init__(self, simulator_host="virtual-ef800", simulator_port=8800):
        self.simulator_host = simulator_host
        self.simulator_port = simulator_port
        self.writer = None
        self.simulator_connected = False
        self.display = None
        self.capabilities = ""
        self.log_history = deque()
        self.log_history_bytes = 0
        self.log_epoch = 0
        self.clients = set()
        self.controller = None
        self.held_keys = set()
        self.running = True
        self.control_lock = asyncio.Lock()
        self.control_waiter = None

    def status(self):
        return {"type": "status", "version": 1,
                "simulatorConnected": self.simulator_connected,
                "controller": self.controller is not None,
                "stale": self.display is None or not self.simulator_connected,
                "logEpoch": self.log_epoch,
                "capabilities": self.capabilities}

    async def broadcast(self, message):
        if not self.clients:
            return
        dead = set()
        encoded = json.dumps(message, separators=(",", ":"))
        async def send_one(ws):
            try:
                await asyncio.wait_for(ws.send_str(encoded), timeout=1)
            except (asyncio.TimeoutError, ConnectionError, RuntimeError):
                return ws
            return None
        results = await asyncio.gather(*(send_one(ws) for ws in tuple(self.clients)))
        dead.update(ws for ws in results if ws is not None)
        for ws in dead:
            self.clients.discard(ws)

    async def broadcast_status(self):
        await self.broadcast(self.status())

    def remember_log(self, record):
        self.log_history.append(record)
        self.log_history_bytes += len(record["text"].encode("ascii"))
        while (len(self.log_history) > LOG_HISTORY_COUNT or
               self.log_history_bytes > LOG_HISTORY_BYTES):
            removed = self.log_history.popleft()
            self.log_history_bytes -= len(removed["text"].encode("ascii"))

    @staticmethod
    def browser_log_record(payload):
        try:
            record = decode_log_record(payload)
        except ProtocolError:
            return None
        return {
            "type": "log", "version": 1,
            "sequence": record["sequence"],
            "severity": record["severity"],
            "severityCode": record["severityCode"],
            "flags": record["flags"],
            "replay": bool(record["flags"] & LOG_REPLAY),
            "text": record["text"],
        }

    async def send_simulator(self, message_type, payload=b""):
        if self.writer is None:
            return False
        try:
            self.writer.write(encode_message(message_type, payload))
            await self.writer.drain()
            return True
        except (ConnectionError, asyncio.CancelledError):
            return False

    async def release_all(self):
        self.held_keys.clear()
        await self.send_simulator(MSG_KEY_RELEASE_ALL)

    async def simulator_loop(self):
        delay = 0.25
        while self.running:
            try:
                reader, writer = await asyncio.open_connection(self.simulator_host, self.simulator_port)
                self.writer = writer
                self.simulator_connected = True
                self.log_epoch += 1
                self.log_history.clear()
                self.log_history_bytes = 0
                delay = 0.25
                await self.broadcast({"type": "log-epoch", "version": 1,
                                      "epoch": self.log_epoch})
                await self.broadcast_status()
                parser = FrameParser()
                while self.running:
                    data = await reader.read(4096)
                    if not data:
                        break
                    for message_type, payload in parser.feed(data):
                        if message_type == MSG_CAPABILITIES:
                            self.capabilities = payload.decode("ascii", errors="replace")
                            await self.broadcast_status()
                        elif message_type == MSG_LCD_FRAME:
                            try:
                                frame = decode_lcd_frame(payload)
                            except ProtocolError:
                                continue
                            self.display = frame
                            await self.broadcast({"type": "lcd", "version": 1,
                                                  "sequence": frame["sequence"],
                                                  "pixels": frame["pixels"].hex(),
                                                  "text": frame["text"]})
                        elif message_type == MSG_PLUGIN_STATUS:
                            waiter = self.control_waiter
                            if waiter is not None and not waiter.done():
                                waiter.set_result(payload)
                            elif len(payload) == 9:
                                status, error, value = struct.unpack("<BII", payload)
                                await self.broadcast({"type": "plugin-status",
                                                       "accepted": status == 0,
                                                       "error": error,
                                                       "count": value})
                        elif message_type == MSG_LOG_RECORD:
                            browser_record = self.browser_log_record(payload)
                            if browser_record is None:
                                continue
                            self.remember_log(browser_record)
                            await self.broadcast(browser_record)
            except (OSError, ProtocolError):
                pass
            finally:
                if self.writer is not None:
                    self.writer.close()
                    with suppress(Exception):
                        await self.writer.wait_closed()
                self.writer = None
                if self.simulator_connected:
                    self.simulator_connected = False
                    self.controller = None
                    await self.release_all()
                    await self.broadcast_status()
            await asyncio.sleep(delay)
            delay = min(delay * 2, 5.0)

    async def simulator_request(self, message_type, payload=b"", timeout=10):
        async with self.control_lock:
            if self.writer is None:
                raise ConnectionError("simulator disconnected")
            loop = asyncio.get_running_loop()
            self.control_waiter = loop.create_future()
            try:
                if not await self.send_simulator(message_type, payload):
                    raise ConnectionError("simulator write failed")
                response = await asyncio.wait_for(self.control_waiter, timeout)
                if len(response) != 9:
                    raise ProtocolError("invalid simulator status")
                status, error, value = struct.unpack("<BII", response)
                return {"accepted": status == 0, "error": error,
                        "value": value}
            finally:
                self.control_waiter = None

    async def upload_plugin(self, package, filename):
        safe_name = os.path.basename(filename)
        if safe_name != filename or not safe_name.lower().endswith(".pmp"):
            raise ProtocolError("invalid plugin filename")
        if not package or len(package) > MAX_PLUGIN_PACKAGE:
            raise ProtocolError("plugin package size out of range")
        name = safe_name.encode("ascii")
        if len(name) >= 64:
            raise ProtocolError("plugin filename too long")
        package_id = 0x504C5547
        begin = struct.pack("<IIIB", package_id, len(package),
                            zlib.crc32(package) & 0xFFFFFFFF, len(name)) + name
        result = await self.simulator_request(MSG_PLUGIN_UPLOAD_BEGIN, begin)
        if not result["accepted"]:
            return result
        chunk_size = 2000
        for offset in range(0, len(package), chunk_size):
            chunk = struct.pack("<II", package_id, offset) + package[offset:offset + chunk_size]
            result = await self.simulator_request(MSG_PLUGIN_UPLOAD_CHUNK, chunk)
            if not result["accepted"]:
                return result
        return await self.simulator_request(MSG_PLUGIN_UPLOAD_COMMIT,
                                            struct.pack("<I", package_id))

    async def restart_simulator(self):
        return await self.simulator_request(MSG_SIMULATOR_RESTART)

    async def http_upload_plugin(self, request):
        try:
            await self.broadcast({"type": "plugin", "state": "uploading"})
            reader = await request.multipart()
            field = await reader.next()
            if field is None or field.filename is None:
                raise ProtocolError("multipart plugin file is required")
            package = await field.read(decode=False)
            result = await self.upload_plugin(package, field.filename)
            await self.broadcast({"type": "plugin", "state":
                                  "installed" if result["accepted"] else "rejected",
                                  "filename": field.filename, "result": result})
            status = 200 if result["accepted"] else 400
            return web.json_response(result, status=status)
        except (ProtocolError, ValueError, UnicodeError, ConnectionError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=400)

    async def http_restart_simulator(self, request):
        try:
            result = await self.restart_simulator()
            return web.json_response(result, status=200 if result["accepted"] else 400)
        except (ConnectionError, asyncio.TimeoutError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=503)

    async def http_dock_device(self, request):
        try:
            port = int(request.match_info["port"])
            if port not in (1, 2):
                raise ProtocolError("only virtual serial ports 1 and 2 are supported")
            body = await request.json()
            response = bytes.fromhex(str(body.get("response_hex", "")))
            if len(response) > 128:
                raise ProtocolError("response script is too large")
            available = 1 if bool(body.get("available", True)) else 0
            delay = max(0, min(60000, int(body.get("delay_ms", 0))))
            error = int(body.get("error", 0))
            for operation, payload in (
                    (1, bytes([available])),
                    (2, struct.pack("<I", delay)),
                    (3, struct.pack("<i", error)),
                    (4, response)):
                if not await self.send_simulator(MSG_PROFILE_CONTROL,
                                                  bytes([port, operation]) + payload):
                    raise ConnectionError("simulator disconnected")
            state = {"port": port, "available": bool(available),
                     "delay_ms": delay, "error": error,
                     "response_hex": response.hex()}
            return web.json_response({"accepted": True, "state": state})
        except (ValueError, TypeError, ProtocolError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=400)
        except ConnectionError as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=503)

    async def http_assign_plugin(self, request):
        try:
            plugin_id = int(request.match_info["plugin_id"])
            body = await request.json()
            port = int(body.get("port", 1))
            connection = int(body.get("connection", 1))
            if not 1 <= port <= 2 or connection not in (1, 2):
                raise ProtocolError("invalid virtual plugin assignment")
            result = await self.simulator_request(MSG_PLUGIN_ASSIGN,
                struct.pack("<BII", port, plugin_id, connection))
            return web.json_response(result, status=200 if result["accepted"] else 400)
        except (ValueError, TypeError, ProtocolError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=400)
        except (ConnectionError, asyncio.TimeoutError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=503)

    async def http_execute_plugin(self, request):
        try:
            port = int(request.match_info["port"])
            if not 1 <= port <= 2:
                raise ProtocolError("invalid virtual plugin port")
            result = await self.simulator_request(MSG_PLUGIN_EXECUTE,
                                                  bytes((port,)))
            return web.json_response(result,
                status=200 if result["accepted"] else 400)
        except (ValueError, TypeError, ProtocolError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=400)
        except (ConnectionError, asyncio.TimeoutError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=503)

    async def initial_messages(self):
        messages = [self.status()]
        if self.display is not None:
            frame = self.display
            messages.append({"type": "lcd", "version": 1,
                             "sequence": frame["sequence"],
                             "pixels": frame["pixels"].hex(),
                             "text": frame["text"]})
        messages.extend(self.log_history)
        return messages

    async def websocket(self, request):
        ws = web.WebSocketResponse(heartbeat=20)
        await ws.prepare(request)
        self.clients.add(ws)
        for message in await self.initial_messages():
            await ws.send_json(message)
        try:
            async for incoming in ws:
                if incoming.type != WSMsgType.TEXT or len(incoming.data) > MAX_BROWSER_MESSAGE:
                    await ws.send_json({"type": "error", "error": "invalid message"})
                    continue
                try:
                    message = json.loads(incoming.data)
                    result = await self.handle_browser_message(ws, message)
                except (ValueError, TypeError, json.JSONDecodeError):
                    result = {"type": "error", "error": "invalid message"}
                await ws.send_json(result)
        finally:
            self.clients.discard(ws)
            if self.controller is ws:
                self.controller = None
                await self.release_all()
                await self.broadcast_status()
        return ws

    async def handle_browser_message(self, ws, message):
        if not isinstance(message, dict) or message.get("version") != 1:
            return {"type": "error", "error": "unsupported browser protocol"}
        command = message.get("command")
        if command == "acquire":
            if self.controller is not None and self.controller is not ws:
                return {"type": "error", "error": "controller occupied"}
            self.controller = ws
            await self.broadcast_status()
            return {"type": "ack", "command": command, "accepted": True}
        if command == "release":
            if self.controller is ws:
                self.controller = None
                await self.release_all()
                await self.broadcast_status()
            return {"type": "ack", "command": command, "accepted": True}
        if command in {"key-down", "key-up"}:
            key = message.get("key")
            if self.controller is not ws:
                return {"type": "error", "error": "controller required"}
            if key not in VALID_KEYS:
                return {"type": "error", "error": "invalid key"}
            message_type = MSG_KEY_DOWN if command == "key-down" else MSG_KEY_UP
            accepted = await self.send_simulator(message_type, key.encode("ascii"))
            if accepted:
                if command == "key-down":
                    self.held_keys.add(key)
                else:
                    self.held_keys.discard(key)
            return {"type": "ack", "command": command, "accepted": accepted}
        if command == "release-all" and self.controller is ws:
            await self.release_all()
            return {"type": "ack", "command": command, "accepted": True}
        return {"type": "error", "error": "unknown command"}


def create_app(gateway):
    app = web.Application(client_max_size=MAX_BROWSER_MESSAGE)
    app["gateway"] = gateway
    app.add_routes([
        web.get("/", lambda _request: web.FileResponse(
            os.path.join(os.path.dirname(__file__), "web", "index.html"))),
        web.get("/app.js", lambda _request: web.FileResponse(
            os.path.join(os.path.dirname(__file__), "web", "app.js"))),
        web.get("/style.css", lambda _request: web.FileResponse(
            os.path.join(os.path.dirname(__file__), "web", "style.css"))),
        web.get("/health", lambda request: web.json_response({
            "web": "ready", "simulator": "connected" if request.app["gateway"].simulator_connected else "disconnected"})),
        web.post("/api/plugins", gateway.http_upload_plugin),
        web.post("/api/simulator/restart", gateway.http_restart_simulator),
        web.post("/api/devices/{port}", gateway.http_dock_device),
        web.post("/api/plugins/{plugin_id}/assign", gateway.http_assign_plugin),
        web.post("/api/plugins/execute/{port}", gateway.http_execute_plugin),
        web.get("/ws", gateway.websocket),
    ])
    async def startup(app):
        app["simulator_task"] = asyncio.create_task(gateway.simulator_loop())
    async def cleanup(app):
        gateway.running = False
        await gateway.release_all()
        app["simulator_task"].cancel()
        with suppress(asyncio.CancelledError):
            await app["simulator_task"]
    app.on_startup.append(startup)
    app.on_cleanup.append(cleanup)
    return app


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.getenv("WEB_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.getenv("WEB_PORT", "8080")))
    parser.add_argument("--simulator-host", default=os.getenv("SIMULATOR_HOST", "virtual-ef800"))
    parser.add_argument("--simulator-port", type=int, default=int(os.getenv("SIMULATOR_PORT", "8800")))
    args = parser.parse_args()
    web.run_app(create_app(Gateway(args.simulator_host, args.simulator_port)), host=args.host, port=args.port)


if __name__ == "__main__":
    main()
