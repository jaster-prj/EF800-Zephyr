#!/usr/bin/env python3
"""Localhost web gateway for the Virtual EF800 front-panel protocol."""
import argparse
import asyncio
from collections import deque
import json
import os
import re
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
                        LOG_HISTORY_COUNT, LOG_REPLAY, MSG_MTP_LIST,
                        MSG_MTP_DOWNLOAD, MSG_MTP_UPLOAD_BEGIN,
                        MSG_MTP_UPLOAD_CHUNK, MSG_MTP_UPLOAD_COMMIT,
                        MSG_MTP_CANCEL, MSG_MTP_RESULT, MSG_MTP_PROGRESS,
                        MSG_MTP_DOWNLOAD_CHUNK, MSG_MTP_LIST_RESULT,
                        MTP_MAX_FILE_SIZE, MTP_MAX_PATH, MTP_MAX_NAME,
                        MTP_MAX_OBJECTS, MTP_CHUNK_SIZE, MTP_STORAGES,
                        MTP_RESULT, MTP_PROGRESS, MTP_LIST_RESULT,
                        MTP_LIST_ENTRY, MTP_DOWNLOAD_CHUNK,
                        MSG_MTP_DOWNLOAD_NEXT, MSG_LCD_CONTRAST,
                        decode_lcd_contrast, MSG_COMPOSITION_STATE,
                        MSG_COMPOSITION_CONTROL, MSG_COMPOSITION_RESULT,
                        decode_composition_control, decode_composition_result,
                        decode_composition_state, encode_composition_control)

MAX_BROWSER_MESSAGE = 1024
MAX_PLUGIN_PACKAGE = 32 * 1024
MAX_MTP_REQUEST_TIMEOUT = 30
MAX_HTTP_BODY = MTP_MAX_FILE_SIZE + 64 * 1024

# One canonical browser name per occupied EF800 keyboard position.  Aliases
# accepted by the native resolver (return, esc, back, control) are deliberately
# not exposed as additional browser controls.
CANONICAL_KEYS = (
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",
    "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
    "y", "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "comma", "period", "minus", "plus", "less", "caret", "space", "tab",
    "enter", "escape", "backspace", "delete", "insert", "off", "up",
    "down", "left", "right", "shift", "ctrl", "alt", "caps", "on",
)
VALID_KEYS = frozenset(CANONICAL_KEYS)
COMPATIBILITY_ALIASES = frozenset(("esc", "return", "back", "control"))
VALID_KEYS = VALID_KEYS | COMPATIBILITY_ALIASES

ANSI_SGR = re.compile(r"\x1b\[([0-9;]*)m")
ANSI_COLORS = {
    30: "#000000", 31: "#aa0000", 32: "#00aa00", 33: "#aa5500",
    34: "#0000aa", 35: "#aa00aa", 36: "#00aaaa", 37: "#aaaaaa",
    90: "#555555", 91: "#ff5555", 92: "#55ff55", 93: "#ffff55",
    94: "#5555ff", 95: "#ff55ff", 96: "#55ffff", 97: "#ffffff",
}
ANSI_BACKGROUND_COLORS = {
    code + 10: color for code, color in ANSI_COLORS.items()
}


def ansi_log_segments(text):
    """Strip ANSI SGR controls and return bounded safe text/color segments."""
    segments = []
    foreground = None
    background = None
    bold = False
    cursor = 0

    def append(value):
        value = value.replace("\x1b", "")
        if not value:
            return
        style = {"foreground": foreground, "background": background,
                 "bold": bold}
        if segments and all(segments[-1][key] == style[key]
                            for key in style):
            segments[-1]["text"] += value
        else:
            segments.append({"text": value, **style})

    for match in ANSI_SGR.finditer(text):
        append(text[cursor:match.start()])
        codes = [int(value) if value else 0
                 for value in match.group(1).split(";")]
        index = 0
        while index < len(codes):
            code = codes[index]
            if code == 0:
                foreground = background = None
                bold = False
            elif code == 1:
                bold = True
            elif code == 22:
                bold = False
            elif code == 39:
                foreground = None
            elif code == 49:
                background = None
            elif code in ANSI_COLORS:
                foreground = ANSI_COLORS[code]
            elif code in ANSI_BACKGROUND_COLORS:
                background = ANSI_BACKGROUND_COLORS[code]
            elif code in (38, 48) and index + 2 < len(codes) and codes[index + 1] == 5:
                color_index = codes[index + 2]
                color = _ansi_256_color(color_index) if color_index <= 255 else None
                if color is not None:
                    if code == 38:
                        foreground = color
                    else:
                        background = color
                index += 2
            elif code in (38, 48) and index + 4 < len(codes) and codes[index + 1] == 2:
                rgb = codes[index + 2:index + 5]
                if all(component <= 255 for component in rgb):
                    color = "#{:02x}{:02x}{:02x}".format(*rgb)
                    if code == 38:
                        foreground = color
                    else:
                        background = color
                index += 4
            index += 1
        cursor = match.end()
    append(text[cursor:])
    return segments


def _ansi_256_color(index):
    if index < 16:
        return ("#000000", "#aa0000", "#00aa00", "#aa5500",
                "#0000aa", "#aa00aa", "#00aaaa", "#aaaaaa",
                "#555555", "#ff5555", "#55ff55", "#ffff55",
                "#5555ff", "#ff55ff", "#55ffff", "#ffffff")[index]
    if index < 232:
        value = index - 16
        levels = (0, 95, 135, 175, 215, 255)
        red, green, blue = levels[value // 36], levels[(value // 6) % 6], levels[value % 6]
    else:
        red = green = blue = 8 + (index - 232) * 10
    return "#{:02x}{:02x}{:02x}".format(red, green, blue)


class Gateway:
    def __init__(self, simulator_host="virtual-ef800", simulator_port=8800):
        self.simulator_host = simulator_host
        self.simulator_port = simulator_port
        self.writer = None
        self.simulator_connected = False
        self.display = None
        self.lcd_contrast = {"enabled": False, "duty": 78}
        self.composition = {"available": False, "restartRequired": False,
                            "groups": []}
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
        self.mtp_lock = asyncio.Lock()
        self.mtp_waiters = {}
        self.mtp_request_id = 0
        self.composition_lock = asyncio.Lock()
        self.composition_waiters = {}
        self.composition_request_id = 0

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

    def lcd_contrast_message(self, payload=None):
        if payload is not None:
            try:
                self.lcd_contrast = decode_lcd_contrast(payload)
            except ProtocolError:
                return None
        return {"type": "lcd-contrast", "version": 1,
                **self.lcd_contrast}

    def reset_lcd_contrast(self):
        self.lcd_contrast = {"enabled": False, "duty": 78}
        return self.lcd_contrast_message()

    @staticmethod
    def composition_message(state=None):
        if state is None:
            state = {"available": False, "restartRequired": False, "groups": []}
        return {"type": "composition", "version": 1, **state}

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
        segments = ansi_log_segments(record["text"])
        return {
            "type": "log", "version": 1,
            "sequence": record["sequence"],
            "severity": record["severity"],
            "severityCode": record["severityCode"],
            "flags": record["flags"],
            "replay": bool(record["flags"] & LOG_REPLAY),
            "text": "".join(segment["text"] for segment in segments),
            "segments": segments,
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
                contrast_message = self.reset_lcd_contrast()
                self.composition = {"available": False,
                                    "restartRequired": False, "groups": []}
                self.log_epoch += 1
                self.log_history.clear()
                self.log_history_bytes = 0
                delay = 0.25
                await self.broadcast({"type": "log-epoch", "version": 1,
                                      "epoch": self.log_epoch})
                await self.broadcast_status()
                await self.broadcast(contrast_message)
                await self.broadcast(self.composition_message())
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
                                                  "pixels": frame["pixels"].hex()})
                        elif message_type == MSG_LCD_CONTRAST:
                            message = self.lcd_contrast_message(payload)
                            if message is not None:
                                await self.broadcast(message)
                        elif message_type == MSG_COMPOSITION_STATE:
                            try:
                                self.composition = decode_composition_state(payload)
                            except ProtocolError:
                                continue
                            await self.broadcast(self.composition_message())
                        elif message_type == MSG_COMPOSITION_RESULT:
                            try:
                                result = decode_composition_result(payload)
                            except ProtocolError:
                                continue
                            waiter = self.composition_waiters.get(result["requestId"])
                            if waiter is not None and not waiter.done():
                                waiter.set_result(result)
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
                        elif message_type in (MSG_MTP_RESULT, MSG_MTP_PROGRESS,
                                              MSG_MTP_DOWNLOAD_CHUNK,
                                              MSG_MTP_LIST_RESULT):
                            if len(payload) >= 4:
                                request_id = struct.unpack_from("<I", payload)[0]
                                waiter = self.mtp_waiters.get(request_id)
                                if waiter is not None and not waiter.full():
                                    waiter.put_nowait((message_type, payload))
                        elif message_type == MSG_LOG_RECORD:
                            browser_record = self.browser_log_record(payload)
                            if browser_record is None:
                                continue
                            self.remember_log(browser_record)
                            await self.broadcast(browser_record)
            except (OSError, ProtocolError):
                pass
            finally:
                for waiter in tuple(self.mtp_waiters.values()):
                    if not waiter.full():
                        waiter.put_nowait(None)
                for waiter in tuple(self.composition_waiters.values()):
                    if not waiter.done():
                        waiter.set_result(None)
                if self.writer is not None:
                    self.writer.close()
                    with suppress(Exception):
                        await self.writer.wait_closed()
                self.writer = None
                if self.simulator_connected:
                    self.simulator_connected = False
                    contrast_message = self.reset_lcd_contrast()
                    self.composition = {"available": False,
                                        "restartRequired": False, "groups": []}
                    self.controller = None
                    await self.release_all()
                    await self.broadcast_status()
                    await self.broadcast(contrast_message)
                    await self.broadcast(self.composition_message())
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

    async def composition_request(self, group, token, timeout=10):
        async with self.composition_lock:
            if self.writer is None:
                raise ConnectionError("simulator disconnected")
            self.composition_request_id = (self.composition_request_id + 1) & 0xFFFFFFFF
            if self.composition_request_id == 0:
                self.composition_request_id = 1
            while self.composition_request_id in self.composition_waiters:
                self.composition_request_id = (self.composition_request_id + 1) & 0xFFFFFFFF or 1
            request_id = self.composition_request_id
            waiter = asyncio.get_running_loop().create_future()
            self.composition_waiters[request_id] = waiter
            try:
                payload = encode_composition_control(request_id, group, token)
                if not await self.send_simulator(MSG_COMPOSITION_CONTROL, payload):
                    raise ConnectionError("simulator write failed")
                result = await asyncio.wait_for(waiter, timeout)
                if result is None:
                    raise ConnectionError("simulator disconnected")
                return result
            finally:
                self.composition_waiters.pop(request_id, None)

    @staticmethod
    def validate_mtp_path(path, allow_root=True):
        if not isinstance(path, str):
            raise ProtocolError("MTP path must be text")
        try:
            encoded = path.encode("utf-8", errors="strict")
        except UnicodeError as error:
            raise ProtocolError("MTP path is not valid UTF-8 text") from error
        if len(encoded) > MTP_MAX_PATH or "\\" in path or path.startswith("/"):
            raise ProtocolError("MTP path is outside approved relative-path bounds")
        if not path:
            if allow_root:
                return ""
            raise ProtocolError("MTP file path is required")
        parts = path.split("/")
        if any(not part or part in (".", "..") for part in parts):
            raise ProtocolError("MTP path contains an invalid component")
        if any(len(part.encode("utf-8")) > MTP_MAX_NAME or
               any(ord(char) < 0x20 for char in part) for part in parts):
            raise ProtocolError("MTP path component is too long or contains controls")
        return path

    @staticmethod
    def mtp_storage_id(name):
        if not isinstance(name, str) or name not in MTP_STORAGES:
            raise ProtocolError("MTP storage must be Data, Plugins, or FWU")
        return MTP_STORAGES[name]

    def next_mtp_request_id(self):
        self.mtp_request_id = (self.mtp_request_id + 1) & 0xFFFFFFFF
        if self.mtp_request_id == 0:
            self.mtp_request_id = 1
        while self.mtp_request_id in self.mtp_waiters:
            self.mtp_request_id = (self.mtp_request_id + 1) & 0xFFFFFFFF or 1
        return self.mtp_request_id

    async def mtp_register(self):
        if self.writer is None:
            raise ConnectionError("simulator disconnected")
        request_id = self.next_mtp_request_id()
        queue = asyncio.Queue(maxsize=64)
        self.mtp_waiters[request_id] = queue
        return request_id, queue

    async def mtp_send(self, message_type, payload):
        if len(payload) > 2400:
            raise ProtocolError("MTP control message exceeds the simulator frame bound")
        if not await self.send_simulator(message_type, payload):
            raise ConnectionError("simulator disconnected during MTP transfer")

    async def mtp_receive(self, request_id, queue, expected, timeout=MAX_MTP_REQUEST_TIMEOUT):
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise asyncio.TimeoutError("MTP operation timed out")
            try:
                event = await asyncio.wait_for(queue.get(), remaining)
            except asyncio.TimeoutError as error:
                raise asyncio.TimeoutError("MTP operation timed out") from error
            if event is None:
                raise ConnectionError("simulator disconnected during MTP transfer")
            message_type, payload = event
            if message_type == MSG_MTP_PROGRESS:
                if len(payload) != MTP_PROGRESS.size:
                    raise ProtocolError("invalid MTP progress message")
                event_id, transferred, total = MTP_PROGRESS.unpack(payload)
                if event_id != request_id or transferred > total or total > MTP_MAX_FILE_SIZE:
                    raise ProtocolError("MTP progress exceeds transfer bounds")
                await self.broadcast({"type": "mtp-progress", "version": 1,
                                     "requestId": request_id,
                                     "transferred": transferred, "total": total})
                if MSG_MTP_PROGRESS in expected:
                    return message_type, payload
                continue
            if message_type not in expected:
                raise ProtocolError("unexpected simulator MTP response")
            return message_type, payload

    async def mtp_list(self, storage, path=""):
        storage_id = self.mtp_storage_id(storage)
        path = self.validate_mtp_path(path)
        path_bytes = path.encode("utf-8")
        async with self.mtp_lock:
            request_id, queue = await self.mtp_register()
            try:
                await self.mtp_send(MSG_MTP_LIST,
                                    struct.pack("<IBB", request_id, storage_id,
                                                len(path_bytes)) + path_bytes)
                message_type, payload = await self.mtp_receive(
                    request_id, queue, {MSG_MTP_LIST_RESULT})
                if message_type != MSG_MTP_LIST_RESULT or len(payload) < MTP_LIST_RESULT.size:
                    raise ProtocolError("invalid MTP listing response")
                response_id, status, count = MTP_LIST_RESULT.unpack_from(payload)
                if response_id != request_id or count > MTP_MAX_OBJECTS:
                    raise ProtocolError("MTP listing response exceeds bounds")
                if status != 0:
                    raise ProtocolError(f"MTP listing failed ({status})")
                offset = MTP_LIST_RESULT.size
                entries = []
                for _ in range(count):
                    if len(payload) - offset < MTP_LIST_ENTRY.size:
                        raise ProtocolError("truncated MTP listing entry")
                    handle, size, flags, name_length = MTP_LIST_ENTRY.unpack_from(payload, offset)
                    offset += MTP_LIST_ENTRY.size
                    if (name_length == 0 or name_length > MTP_MAX_NAME or
                            name_length > len(payload) - offset or flags & ~1 or
                            size > MTP_MAX_FILE_SIZE):
                        raise ProtocolError("invalid MTP listing entry")
                    try:
                        name = payload[offset:offset + name_length].decode("utf-8", errors="strict")
                    except UnicodeError as error:
                        raise ProtocolError("invalid MTP filename encoding") from error
                    offset += name_length
                    if (name in (".", "..") or "/" in name or "\\" in name or
                            any(ord(char) < 0x20 for char in name)):
                        raise ProtocolError("unsafe MTP filename from simulator")
                    entries.append({"handle": handle, "name": name,
                                    "size": size, "directory": bool(flags & 1)})
                if offset != len(payload):
                    raise ProtocolError("unexpected data after MTP listing")
                return {"storage": storage, "path": path, "entries": entries}
            finally:
                self.mtp_waiters.pop(request_id, None)

    async def mtp_download(self, storage, path):
        storage_id = self.mtp_storage_id(storage)
        path = self.validate_mtp_path(path, allow_root=False)
        path_bytes = path.encode("utf-8")
        async with self.mtp_lock:
            request_id, queue = await self.mtp_register()
            data = bytearray()
            try:
                await self.broadcast({"type": "mtp-transfer", "version": 1,
                                      "requestId": request_id, "state": "started",
                                      "operation": "download"})
                await self.mtp_send(MSG_MTP_DOWNLOAD,
                                    struct.pack("<IBB", request_id, storage_id,
                                                len(path_bytes)) + path_bytes)
                while True:
                    message_type, payload = await self.mtp_receive(
                        request_id, queue,
                        {MSG_MTP_DOWNLOAD_CHUNK, MSG_MTP_RESULT})
                    if message_type == MSG_MTP_DOWNLOAD_CHUNK:
                        if len(payload) < MTP_DOWNLOAD_CHUNK.size:
                            raise ProtocolError("truncated MTP download chunk")
                        event_id, offset, chunk_length = MTP_DOWNLOAD_CHUNK.unpack_from(payload)
                        chunk = payload[MTP_DOWNLOAD_CHUNK.size:]
                        if (event_id != request_id or chunk_length == 0 or
                                chunk_length != len(chunk) or offset != len(data) or
                                len(data) + chunk_length > MTP_MAX_FILE_SIZE):
                            raise ProtocolError("invalid or out-of-order MTP download chunk")
                        data.extend(chunk)
                        await self.mtp_send(MSG_MTP_DOWNLOAD_NEXT,
                                            struct.pack("<II", request_id, len(data)))
                        continue
                    if len(payload) != MTP_RESULT.size:
                        raise ProtocolError("invalid MTP download completion")
                    event_id, status, transferred, total, checksum = MTP_RESULT.unpack(payload)
                    actual_crc = zlib.crc32(data) & 0xFFFFFFFF
                    if event_id != request_id:
                        raise ProtocolError("MTP download request identifier mismatch")
                    if status != 0:
                        raise ProtocolError(f"MTP download failed ({status})")
                    if (transferred != len(data) or total != len(data) or
                            checksum != actual_crc):
                        raise ProtocolError("MTP download incomplete or checksum mismatch")
                    result = bytes(data)
                    await self.broadcast({"type": "mtp-transfer", "version": 1,
                                          "requestId": request_id, "state": "complete",
                                          "operation": "download", "total": len(result)})
                    return result
            except asyncio.CancelledError:
                await self.best_effort_mtp_cancel(request_id)
                await self.broadcast({"type": "mtp-transfer", "version": 1,
                                      "requestId": request_id, "state": "cancelled",
                                      "operation": "download"})
                raise
            except Exception as error:
                await self.broadcast({"type": "mtp-transfer", "version": 1,
                                      "requestId": request_id, "state": "failed",
                                      "operation": "download", "error": str(error)[:160]})
                raise
            finally:
                self.mtp_waiters.pop(request_id, None)

    async def best_effort_mtp_cancel(self, request_id):
        with suppress(Exception):
            await asyncio.shield(self.mtp_send(MSG_MTP_CANCEL,
                                               struct.pack("<I", request_id)))

    async def mtp_upload(self, storage, directory, filename, data):
        storage_id = self.mtp_storage_id(storage)
        directory = self.validate_mtp_path(directory)
        try:
            filename_bytes = filename.encode("utf-8", errors="strict") if isinstance(filename, str) else b""
        except UnicodeError as error:
            raise ProtocolError("invalid MTP upload filename encoding") from error
        if (not isinstance(filename, str) or filename in ("", ".", "..") or
                "/" in filename or "\\" in filename or
                len(filename_bytes) > MTP_MAX_NAME or
                any(ord(char) < 0x20 for char in filename)):
            raise ProtocolError("invalid MTP upload filename")
        path = f"{directory}/{filename}" if directory else filename
        path = self.validate_mtp_path(path, allow_root=False)
        if not data or len(data) > MTP_MAX_FILE_SIZE:
            raise ProtocolError("MTP upload size is outside allowed bounds")
        path_bytes = path.encode("utf-8")
        checksum = zlib.crc32(data) & 0xFFFFFFFF
        async with self.mtp_lock:
            request_id, queue = await self.mtp_register()
            begun = False
            finished = False
            try:
                await self.broadcast({"type": "mtp-transfer", "version": 1,
                                      "requestId": request_id, "state": "started",
                                      "operation": "upload", "total": len(data)})
                begin = struct.pack("<IBBII", request_id, storage_id,
                                    len(path_bytes), len(data), checksum) + path_bytes
                await self.mtp_send(MSG_MTP_UPLOAD_BEGIN, begin)
                _, payload = await self.mtp_receive(request_id, queue, {MSG_MTP_RESULT})
                result = self.decode_mtp_result(payload, request_id)
                if result["status"] != 0:
                    raise ProtocolError(f"MTP upload rejected ({result['status']})")
                if result["total"] != len(data):
                    raise ProtocolError("simulator accepted a different MTP upload size")
                begun = True
                for offset in range(0, len(data), MTP_CHUNK_SIZE):
                    chunk = data[offset:offset + MTP_CHUNK_SIZE]
                    await self.mtp_send(MSG_MTP_UPLOAD_CHUNK,
                                        struct.pack("<II", request_id, offset) + chunk)
                    message_type, payload = await self.mtp_receive(
                        request_id, queue, {MSG_MTP_PROGRESS})
                    if message_type != MSG_MTP_PROGRESS or len(payload) != MTP_PROGRESS.size:
                        raise ProtocolError("invalid MTP upload progress acknowledgement")
                    event_id, transferred, total = MTP_PROGRESS.unpack(payload)
                    if (event_id != request_id or transferred != offset + len(chunk) or
                            total != len(data)):
                        raise ProtocolError("MTP upload chunk was not accepted")
                await self.mtp_send(MSG_MTP_UPLOAD_COMMIT, struct.pack("<I", request_id))
                _, payload = await self.mtp_receive(request_id, queue, {MSG_MTP_RESULT})
                completed = self.decode_mtp_result(payload, request_id)
                if (completed["status"] != 0 or completed["transferred"] != len(data) or
                        completed["total"] != len(data) or completed["crc32"] != checksum):
                    raise ProtocolError(
                        "MTP upload did not complete atomically "
                        f"(status={completed['status']}, transferred={completed['transferred']}, "
                        f"total={completed['total']}, checksum={completed['crc32']:08x})")
                finished = True
                await self.broadcast({"type": "mtp-transfer", "version": 1,
                                      "requestId": request_id, "state": "complete",
                                      "operation": "upload", "total": len(data)})
                return {"accepted": True, "requestId": request_id,
                        "transferred": len(data), "total": len(data)}
            except asyncio.CancelledError:
                await self.best_effort_mtp_cancel(request_id)
                await self.broadcast({"type": "mtp-transfer", "version": 1,
                                      "requestId": request_id, "state": "cancelled",
                                      "operation": "upload"})
                raise
            except Exception as error:
                await self.broadcast({"type": "mtp-transfer", "version": 1,
                                      "requestId": request_id, "state": "failed",
                                      "operation": "upload", "error": str(error)[:160]})
                raise
            finally:
                if begun and not finished:
                    await self.best_effort_mtp_cancel(request_id)
                self.mtp_waiters.pop(request_id, None)

    @staticmethod
    def decode_mtp_result(payload, request_id):
        if len(payload) != MTP_RESULT.size:
            raise ProtocolError("invalid MTP result message")
        event_id, status, transferred, total, checksum = MTP_RESULT.unpack(payload)
        if event_id != request_id or transferred > MTP_MAX_FILE_SIZE or total > MTP_MAX_FILE_SIZE:
            raise ProtocolError("MTP result exceeds transfer bounds")
        return {"status": status, "transferred": transferred,
                "total": total, "crc32": checksum}

    async def http_mtp_list(self, request):
        try:
            result = await self.mtp_list(request.query.get("storage", ""),
                                         request.query.get("path", ""))
            return web.json_response(result)
        except ProtocolError as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=400)
        except (ConnectionError, asyncio.TimeoutError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=503)

    async def http_mtp_download(self, request):
        try:
            storage = request.query.get("storage", "")
            path = self.validate_mtp_path(request.query.get("path", ""),
                                          allow_root=False)
            data = await self.mtp_download(storage, path)
            filename = path.rsplit("/", 1)[-1]
            from urllib.parse import quote
            return web.Response(body=data, content_type="application/octet-stream",
                                headers={"Content-Disposition":
                                         f"attachment; filename*=UTF-8''{quote(filename)}"})
        except ProtocolError as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=400)
        except (ConnectionError, asyncio.TimeoutError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=503)

    async def http_mtp_upload(self, request):
        try:
            reader = await request.multipart()
            fields = {}
            upload = None
            while True:
                field = await reader.next()
                if field is None:
                    break
                if field.name == "file":
                    if upload is not None or not field.filename:
                        raise ProtocolError("exactly one named MTP file is required")
                    if "/" in field.filename or "\\" in field.filename:
                        raise ProtocolError("upload filename must not contain a path")
                    upload = (field.filename, await field.read(decode=False))
                elif field.name in {"storage", "path"} and field.name not in fields:
                    fields[field.name] = await field.text()
                else:
                    raise ProtocolError("unexpected or duplicate MTP upload field")
            if upload is None or set(fields) != {"storage", "path"}:
                raise ProtocolError("storage, path, and file fields are required")
            filename, data = upload
            if len(data) > MTP_MAX_FILE_SIZE:
                raise ProtocolError("MTP upload exceeds the 32 KiB file-size limit")
            result = await self.mtp_upload(fields["storage"], fields["path"],
                                           filename, data)
            return web.json_response(result)
        except ProtocolError as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=400)
        except (ConnectionError, asyncio.TimeoutError) as error:
            return web.json_response({"accepted": False, "error": str(error)}, status=503)

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
            profile = int(body.get("profile", 0))
            if profile not in (0, 1, 2):
                raise ProtocolError("invalid virtual instrument profile")
            for operation, payload in (
                    (1, bytes([available])),
                    (2, struct.pack("<I", delay)),
                    (3, struct.pack("<i", error)),
                    (4, response),
                    (5, bytes([profile]))):
                if not await self.send_simulator(MSG_PROFILE_CONTROL,
                                                  bytes([port, operation]) + payload):
                    raise ConnectionError("simulator disconnected")
            state = {"port": port, "available": bool(available),
                     "delay_ms": delay, "error": error,
                     "response_hex": response.hex(), "profile": profile}
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
        messages = [self.status(), self.lcd_contrast_message(),
                    self.composition_message(self.composition)]
        if self.display is not None:
            frame = self.display
            messages.append({"type": "lcd", "version": 1,
                             "sequence": frame["sequence"],
                             "pixels": frame["pixels"].hex()})
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
        if command == "composition-set":
            if self.controller is not ws:
                return {"type": "error", "error": "controller required"}
            group = message.get("group")
            token = message.get("token")
            if not self.simulator_connected or not self.composition["available"]:
                return {"type": "error", "error": "composition unavailable"}
            if self.composition["restartRequired"]:
                return {"type": "error", "error": "restart required before another change"}
            if not isinstance(group, str) or not isinstance(token, str):
                return {"type": "error", "error": "invalid composition selection"}
            group_state = next((entry for entry in self.composition["groups"]
                                if entry["id"] == group), None)
            if (group_state is None or
                    token not in {option["token"] for option in group_state["options"]}):
                return {"type": "error", "error": "selection is not in the active catalog"}
            try:
                result = await self.composition_request(group, token)
            except (ConnectionError, asyncio.TimeoutError, ProtocolError) as error:
                return {"type": "ack", "command": command,
                        "accepted": False, "error": str(error)}
            if not result["accepted"]:
                return {"type": "ack", "command": command,
                        "accepted": False, "errorCode": result["error"],
                        "error": os.strerror(abs(result["error"]))
                        if result["error"] else "composition change rejected"}
            return {"type": "ack", "command": command, "accepted": True,
                    "restartRequired": True}
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
            return {"type": "ack", "command": command, "key": key,
                    "accepted": accepted}
        if command == "release-all" and self.controller is ws:
            await self.release_all()
            return {"type": "ack", "command": command, "accepted": True}
        if command == "mtp-cancel":
            request_id = message.get("requestId")
            if (not isinstance(request_id, int) or isinstance(request_id, bool) or
                    not 1 <= request_id <= 0xFFFFFFFF or
                    request_id not in self.mtp_waiters):
                return {"type": "error", "error": "unknown MTP transfer"}
            accepted = await self.send_simulator(MSG_MTP_CANCEL,
                                                 struct.pack("<I", request_id))
            return {"type": "ack", "command": command,
                    "requestId": request_id, "accepted": accepted}
        return {"type": "error", "error": "unknown command"}


def create_app(gateway):
    app = web.Application(client_max_size=MAX_HTTP_BODY)
    app["gateway"] = gateway

    def static_file(name):
        return web.FileResponse(
            os.path.join(os.path.dirname(__file__), "web", name),
            headers={"Cache-Control": "no-store, max-age=0"})

    app.add_routes([
        web.get("/", lambda _request: static_file("index.html")),
        web.get("/app.js", lambda _request: static_file("app.js")),
        web.get("/style.css", lambda _request: static_file("style.css")),
        web.get("/health", lambda request: web.json_response({
            "web": "ready", "simulator": "connected" if request.app["gateway"].simulator_connected else "disconnected"})),
        web.post("/api/plugins", gateway.http_upload_plugin),
        web.post("/api/simulator/restart", gateway.http_restart_simulator),
        web.post("/api/devices/{port}", gateway.http_dock_device),
        web.post("/api/plugins/{plugin_id}/assign", gateway.http_assign_plugin),
        web.post("/api/plugins/execute/{port}", gateway.http_execute_plugin),
        web.get("/api/mtp/list", gateway.http_mtp_list),
        web.get("/api/mtp/download", gateway.http_mtp_download),
        web.post("/api/mtp/upload", gateway.http_mtp_upload),
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
