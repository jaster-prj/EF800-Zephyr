import asyncio
import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from protocol import LOG_GAP, encode_log_record

try:
    from gateway import CANONICAL_KEYS, Gateway
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

    async def test_broadcast_removes_failed_viewers(self):
        gateway = Gateway()

        class DeadViewer:
            async def send_str(self, _message):
                raise ConnectionError("closed")

        viewer = DeadViewer()
        gateway.clients.add(viewer)
        await gateway.broadcast({"type": "log", "text": "x"})
        self.assertNotIn(viewer, gateway.clients)


if __name__ == "__main__":
    unittest.main()
