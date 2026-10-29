import asyncio
import json
import pathlib
import sys
import unittest
from unittest.mock import AsyncMock

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from protocol import (LOG_GAP, encode_log_record, FrameParser, encode_message,
                      MSG_MTP_LIST, MSG_MTP_DOWNLOAD, MSG_MTP_UPLOAD_BEGIN,
                      MSG_MTP_UPLOAD_CHUNK, MSG_MTP_UPLOAD_COMMIT,
                      MSG_MTP_CANCEL,
                      MSG_MTP_RESULT, MSG_MTP_PROGRESS,
                       MSG_MTP_DOWNLOAD_CHUNK, MSG_MTP_LIST_RESULT,
                       MSG_MTP_DOWNLOAD_NEXT,
                        MSG_LCD_CONTRAST, decode_lcd_contrast,
                        encode_lcd_contrast, ProtocolError,
                        decode_composition_state, decode_composition_control,
                        MSG_COMPOSITION_CONTROL,
                        MTP_RESULT, MTP_PROGRESS, MTP_LIST_RESULT,
                      MTP_LIST_ENTRY, MTP_DOWNLOAD_CHUNK)

try:
    from gateway import CANONICAL_KEYS, Gateway, create_app
except ModuleNotFoundError:
    Gateway = None


@unittest.skipIf(Gateway is None, "aiohttp is installed in the gateway image")
class GatewayStateTests(unittest.IsolatedAsyncioTestCase):
    class Writer:
        def __init__(self):
            self.messages = []

        def write(self, message):
            self.messages.append(message)

        async def drain(self):
            return None

    async def test_canonical_key_inventory_and_compatibility_aliases(self):
        self.assertEqual(len(CANONICAL_KEYS), 59)
        self.assertEqual(len(set(CANONICAL_KEYS)), 59)
        gateway = Gateway()
        gateway.writer = self.Writer()
        controller = object()
        gateway.controller = controller
        for key in ("a", "backspace", "shift", "on", "up"):
            result = await gateway.handle_browser_message(
                controller, {"version": 1, "command": "key-down", "key": key})
            self.assertTrue(result["accepted"])
            self.assertEqual(result["key"], key)
        result = await gateway.handle_browser_message(
            controller, {"version": 1, "command": "key-down", "key": "esc"})
        self.assertTrue(result["accepted"])
        result = await gateway.handle_browser_message(
            controller, {"version": 1, "command": "key-down", "key": "not-a-key"})
        self.assertEqual(result["error"], "invalid key")

    async def test_lcd_contrast_payload_and_browser_state(self):
        gateway = Gateway()
        self.assertEqual(gateway.lcd_contrast_message(), {
            "type": "lcd-contrast", "version": 1,
            "enabled": False, "duty": 78})

        for enabled, duty in ((True, 78), (True, 25), (True, 95), (False, 78)):
            payload = encode_lcd_contrast(enabled, duty)
            self.assertEqual(decode_lcd_contrast(payload),
                             {"enabled": enabled, "duty": duty})
            self.assertEqual(gateway.lcd_contrast_message(payload), {
                "type": "lcd-contrast", "version": 1,
                "enabled": enabled, "duty": duty})
        with self.assertRaises(ProtocolError):
            decode_lcd_contrast(bytes((2, 55)))
        with self.assertRaises(ProtocolError):
            decode_lcd_contrast(bytes((1,)))
        with self.assertRaises(ProtocolError):
            encode_lcd_contrast(True, 101)
        self.assertIsNone(gateway.lcd_contrast_message(bytes((1, 101))))
        self.assertEqual(gateway.lcd_contrast,
                         {"enabled": False, "duty": 78})

    async def test_lcd_contrast_resets_to_static_reference_after_reconnect(self):
        gateway = Gateway()
        gateway.lcd_contrast_message(encode_lcd_contrast(True, 44))
        reset = gateway.reset_lcd_contrast()
        self.assertEqual(reset, {"type": "lcd-contrast", "version": 1,
                                 "enabled": False, "duty": 78})
        initial = await gateway.initial_messages()
        self.assertEqual(initial[1], reset)

    async def test_composition_selection_requires_controller_and_catalog_choice(self):
        gateway = Gateway()
        gateway.simulator_connected = True
        gateway.composition = decode_composition_state(json.dumps({
            "type": "composition", "version": 1, "available": True,
            "restartRequired": False,
            "groups": [{"id": "LCDCONTR", "active": "LCDCONTR0",
                        "selected": "LCDCONTR0", "options": [
                            {"token": "LCDCONTR0", "default": True},
                            {"token": "LCDCONTR1", "default": False}]}]
        }).encode())
        gateway.composition_request = AsyncMock(return_value={
            "requestId": 1, "accepted": True, "error": 0})
        browser = object()

        denied = await gateway.handle_browser_message(browser, {
            "version": 1, "command": "composition-set",
            "group": "LCDCONTR", "token": "LCDCONTR1"})
        self.assertEqual(denied["error"], "controller required")
        gateway.controller = browser
        invalid = await gateway.handle_browser_message(browser, {
            "version": 1, "command": "composition-set",
            "group": "LCDCONTR", "token": "UNKNOWN0"})
        self.assertEqual(invalid["error"], "selection is not in the active catalog")
        gateway.composition_request.assert_not_awaited()

        accepted = await gateway.handle_browser_message(browser, {
            "version": 1, "command": "composition-set",
            "group": "LCDCONTR", "token": "LCDCONTR1"})
        self.assertTrue(accepted["accepted"])
        self.assertTrue(accepted["restartRequired"])
        gateway.composition_request.assert_awaited_once_with("LCDCONTR", "LCDCONTR1")

    async def test_composition_changes_are_blocked_after_commit_until_restart(self):
        gateway = Gateway()
        browser = object()
        gateway.controller = browser
        gateway.simulator_connected = True
        gateway.composition = {"available": True, "restartRequired": True,
                               "groups": []}
        result = await gateway.handle_browser_message(browser, {
            "version": 1, "command": "composition-set",
            "group": "LCDCONTR", "token": "LCDCONTR1"})
        self.assertEqual(result["error"], "restart required before another change")

    async def test_composition_request_uses_bounded_simulator_protocol(self):
        gateway = Gateway()
        gateway.writer = self.Writer()
        request = asyncio.create_task(
            gateway.composition_request("LCDCONTR", "LCDCONTR1"))
        await asyncio.sleep(0)
        message_type, payload = FrameParser().feed(gateway.writer.messages[0])[0]
        self.assertEqual(message_type, MSG_COMPOSITION_CONTROL)
        control = decode_composition_control(payload)
        self.assertEqual(control["group"], "LCDCONTR")
        self.assertEqual(control["token"], "LCDCONTR1")
        gateway.composition_waiters[control["requestId"]].set_result({
            "requestId": control["requestId"], "accepted": True, "error": 0})
        result = await request
        self.assertTrue(result["accepted"])
        self.assertFalse(gateway.composition_waiters)

    async def test_controller_is_exclusive_and_release_clears_keys(self):
        gateway = Gateway()
        first = object()
        second = object()
        self.assertTrue((await gateway.handle_browser_message(first, {"version": 1, "command": "acquire"}))["accepted"])
        self.assertEqual((await gateway.handle_browser_message(second, {"version": 1, "command": "acquire"}))["error"], "controller occupied")
        self.assertFalse((await gateway.handle_browser_message(first, {"version": 1, "command": "key-down", "key": "right"}))["accepted"])
        await gateway.handle_browser_message(first, {"version": 1, "command": "release"})
        self.assertFalse(gateway.held_keys)

    async def test_release_all_clears_held_keys(self):
        gateway = Gateway()
        gateway.writer = self.Writer()
        controller = object()
        gateway.controller = controller
        await gateway.handle_browser_message(
            controller, {"version": 1, "command": "key-down", "key": "shift"})
        await gateway.handle_browser_message(
            controller, {"version": 1, "command": "key-down", "key": "a"})
        self.assertEqual(gateway.held_keys, {"shift", "a"})
        result = await gateway.handle_browser_message(
            controller, {"version": 1, "command": "release-all"})
        self.assertTrue(result["accepted"])
        self.assertFalse(gateway.held_keys)

    async def test_key_release_updates_held_key_tracking(self):
        gateway = Gateway()
        gateway.writer = self.Writer()
        controller = object()
        gateway.controller = controller
        await gateway.handle_browser_message(
            controller, {"version": 1, "command": "key-down", "key": "ctrl"})
        result = await gateway.handle_browser_message(
            controller, {"version": 1, "command": "key-up", "key": "ctrl"})
        self.assertTrue(result["accepted"])
        self.assertFalse(gateway.held_keys)

    async def test_browser_message_is_bounded(self):
        gateway = Gateway()
        result = await gateway.handle_browser_message(object(), {"version": 2, "command": "acquire"})
        self.assertEqual(result["type"], "error")

    async def test_plugin_upload_rejects_unsafe_filename(self):
        gateway = Gateway()
        with self.assertRaises(ValueError):
            await gateway.upload_plugin(b"package", "../escape.pmp")

    async def test_restart_requires_connected_simulator(self):
        gateway = Gateway()
        with self.assertRaises(ConnectionError):
            await gateway.restart_simulator()

    async def test_initial_messages_include_bounded_log_history(self):
        gateway = Gateway()
        gateway.log_history.append({"type": "log", "sequence": 4,
                                    "severity": "INFO", "flags": 1,
                                    "replay": True, "text": "startup"})
        messages = await gateway.initial_messages()
        self.assertEqual(messages[-1]["type"], "log")
        self.assertEqual(messages[-1]["sequence"], 4)

    async def test_initial_lcd_message_contains_pixels_without_text_rows(self):
        gateway = Gateway()
        gateway.display = {"sequence": 7, "pixels": bytes(1920)}
        messages = await gateway.initial_messages()
        lcd_message = next(message for message in messages
                           if message["type"] == "lcd")
        self.assertEqual(set(lcd_message),
                         {"type", "version", "sequence", "pixels"})
        self.assertEqual(len(bytes.fromhex(lcd_message["pixels"])), 1920)

    async def test_status_exposes_diagnostic_epoch(self):
        gateway = Gateway()
        gateway.log_epoch = 3
        self.assertEqual(gateway.status()["logEpoch"], 3)

    async def test_log_history_is_bounded(self):
        gateway = Gateway()
        for sequence in range(100):
            gateway.remember_log({"type": "log", "sequence": sequence,
                                  "severity": "INFO", "flags": 0,
                                  "replay": False, "text": "record"})
        self.assertLessEqual(len(gateway.log_history), 64)
        self.assertEqual(gateway.log_history[-1]["sequence"], 99)

    async def test_log_translation_rejects_malformed_and_preserves_flags(self):
        gateway = Gateway()
        self.assertIsNone(gateway.browser_log_record(b"bad"))
        record = gateway.browser_log_record(
            encode_log_record(12, 3, "dropped", LOG_GAP))
        self.assertEqual(record["sequence"], 12)
        self.assertEqual(record["severity"], "ERROR")
        self.assertEqual(record["flags"], LOG_GAP)

    async def test_log_translation_strips_ansi_controls_and_keeps_color_segments(self):
        record = Gateway.browser_log_record(encode_log_record(
            13, 1, "\x1b[32mKeyboard key: 0x0d\x1b[0m"))
        self.assertEqual(record["text"], "Keyboard key: 0x0d")
        self.assertNotIn("[0m", record["text"])
        self.assertNotIn("\x1b", record["text"])
        self.assertEqual(record["segments"], [{
            "text": "Keyboard key: 0x0d", "foreground": "#00aa00",
            "background": None, "bold": False}])

    async def test_broadcast_removes_failed_viewers(self):
        gateway = Gateway()

        class DeadViewer:
            async def send_str(self, _message):
                raise ConnectionError("closed")

        viewer = DeadViewer()
        gateway.clients.add(viewer)
        await gateway.broadcast({"type": "log", "text": "x"})
        self.assertNotIn(viewer, gateway.clients)

    async def test_mtp_gateway_operations_use_bounded_simulator_usb_messages(self):
        class MtpSimulatorWriter(self.Writer):
            def __init__(self, gateway):
                super().__init__()
                self.gateway = gateway
                self.parser = FrameParser()
                self.file_data = b"file received through virtual MTP"
                self.upload = bytearray()
                self.upload_size = 0
                self.upload_crc = 0
                self.events = []

            def write(self, frame):
                self.messages.append(frame)
                for message_type, payload in self.parser.feed(frame):
                    self.events.append(message_type)
                    request_id = int.from_bytes(payload[:4], "little")
                    if message_type == MSG_MTP_LIST:
                        response = (MTP_LIST_RESULT.pack(request_id, 0, 1) +
                                    MTP_LIST_ENTRY.pack(7, 34, 0, 8) + b"file.txt")
                        self.reply(MSG_MTP_LIST_RESULT, response)
                    elif message_type == MSG_MTP_DOWNLOAD:
                        chunk = MTP_DOWNLOAD_CHUNK.pack(request_id, 0,
                                                        len(self.file_data)) + self.file_data
                        self.reply(MSG_MTP_DOWNLOAD_CHUNK, chunk)
                        self.reply(MSG_MTP_PROGRESS, MTP_PROGRESS.pack(
                            request_id, len(self.file_data), len(self.file_data)))
                    elif message_type == MSG_MTP_DOWNLOAD_NEXT:
                        self.reply(MSG_MTP_RESULT, MTP_RESULT.pack(
                            request_id, 0, len(self.file_data), len(self.file_data),
                            __import__("zlib").crc32(self.file_data) & 0xFFFFFFFF))
                    elif message_type == MSG_MTP_UPLOAD_BEGIN:
                        _, _storage, _path_len, self.upload_size, self.upload_crc = \
                            __import__("struct").unpack_from("<IBBII", payload)
                        self.upload.clear()
                        self.reply(MSG_MTP_RESULT, MTP_RESULT.pack(
                            request_id, 0, 0, self.upload_size, 0))
                    elif message_type == MSG_MTP_UPLOAD_CHUNK:
                        offset = int.from_bytes(payload[4:8], "little")
                        if offset != len(self.upload):
                            raise AssertionError("gateway sent an out-of-order upload chunk")
                        self.upload.extend(payload[8:])
                        self.reply(MSG_MTP_PROGRESS, MTP_PROGRESS.pack(
                            request_id, len(self.upload), self.upload_size))
                    elif message_type == MSG_MTP_UPLOAD_COMMIT:
                        if (__import__("zlib").crc32(self.upload) & 0xFFFFFFFF) != self.upload_crc:
                            raise AssertionError("gateway upload checksum did not match bytes")
                        self.reply(MSG_MTP_RESULT, MTP_RESULT.pack(
                            request_id, 0, len(self.upload), len(self.upload),
                            self.upload_crc))

            def reply(self, message_type, payload):
                queue = self.gateway.mtp_waiters[__import__("struct").unpack_from("<I", payload)[0]]
                queue.put_nowait((message_type, payload))

        import struct
        import zlib

        gateway = Gateway()
        writer = MtpSimulatorWriter(gateway)
        gateway.writer = writer
        listed = await gateway.mtp_list("Data", "nested")
        self.assertEqual(listed["entries"][0]["name"], "file.txt")
        downloaded = await gateway.mtp_download("Plugins", "file.txt")
        self.assertEqual(downloaded, writer.file_data)
        uploaded = await gateway.mtp_upload("FWU", "", "image.bin", b"u" * 5001)
        self.assertTrue(uploaded["accepted"])
        self.assertEqual(writer.upload, b"u" * 5001)
        self.assertEqual(writer.events, [
            MSG_MTP_LIST, MSG_MTP_DOWNLOAD, MSG_MTP_DOWNLOAD_NEXT,
            MSG_MTP_UPLOAD_BEGIN,
            MSG_MTP_UPLOAD_CHUNK, MSG_MTP_UPLOAD_CHUNK,
            MSG_MTP_UPLOAD_CHUNK, MSG_MTP_UPLOAD_COMMIT])
        self.assertFalse(gateway.mtp_waiters)

    async def test_mtp_gateway_rejects_traversal_and_oversized_results(self):
        gateway = Gateway()
        with self.assertRaises(ValueError):
            await gateway.mtp_list("Data", "../plugins")
        with self.assertRaises(ValueError):
            await gateway.mtp_download("Configuration", "secret")
        with self.assertRaises(ValueError):
            await gateway.mtp_upload("Data", "", "../escape", b"x")
        with self.assertRaises(ValueError):
            gateway.decode_mtp_result(MTP_RESULT.pack(1, 0, 1, 1, 0), 2)

    async def test_browser_can_cancel_only_an_active_bounded_mtp_request(self):
        gateway = Gateway()
        gateway.writer = self.Writer()
        request_id = 42
        gateway.mtp_waiters[request_id] = asyncio.Queue(maxsize=4)
        browser = object()
        result = await gateway.handle_browser_message(browser, {
            "version": 1, "command": "mtp-cancel", "requestId": request_id})
        self.assertTrue(result["accepted"])
        parser = FrameParser()
        messages = [message for frame in gateway.writer.messages for message in parser.feed(frame)]
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0][0], MSG_MTP_CANCEL)
        self.assertEqual(messages[0][1], request_id.to_bytes(4, "little"))
        rejected = await gateway.handle_browser_message(browser, {
            "version": 1, "command": "mtp-cancel", "requestId": 99})
        self.assertEqual(rejected["type"], "error")

    async def test_mtp_http_routes_are_gateway_owned(self):
        app = create_app(Gateway())
        routes = {(route.method, route.resource.canonical)
                  for route in app.router.routes()}
        self.assertIn(("GET", "/api/mtp/list"), routes)
        self.assertIn(("GET", "/api/mtp/download"), routes)
        self.assertIn(("POST", "/api/mtp/upload"), routes)


if __name__ == "__main__":
    unittest.main()
