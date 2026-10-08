import asyncio
import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from protocol import LOG_GAP, encode_log_record

try:
    from gateway import Gateway
except ModuleNotFoundError:
    Gateway = None


@unittest.skipIf(Gateway is None, "aiohttp is installed in the gateway image")
class GatewayStateTests(unittest.IsolatedAsyncioTestCase):
    async def test_controller_is_exclusive_and_release_clears_keys(self):
        gateway = Gateway()
        first = object()
        second = object()
        self.assertTrue((await gateway.handle_browser_message(first, {"version": 1, "command": "acquire"}))["accepted"])
        self.assertEqual((await gateway.handle_browser_message(second, {"version": 1, "command": "acquire"}))["error"], "controller occupied")
        self.assertFalse((await gateway.handle_browser_message(first, {"version": 1, "command": "key-down", "key": "right"}))["accepted"])
        await gateway.handle_browser_message(first, {"version": 1, "command": "release"})
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
