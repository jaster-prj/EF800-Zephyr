import importlib.util
import pathlib
import struct
import unittest

MODULE = pathlib.Path(__file__).with_name("scenario_client.py")
SPEC = importlib.util.spec_from_file_location("scenario_client", MODULE)
scenario_client = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scenario_client)


class ScenarioClientTests(unittest.TestCase):
    def test_frame_contains_protocol_header(self):
        encoded = scenario_client.frame(scenario_client.MSG_KEY_DOWN, b"up")
        magic, version, message_type, length, _ = scenario_client.HEADER.unpack(
            encoded[:scenario_client.HEADER.size])
        self.assertEqual(magic, scenario_client.MAGIC)
        self.assertEqual(version, scenario_client.VERSION)
        self.assertEqual(message_type, scenario_client.MSG_KEY_DOWN)
        self.assertEqual(length, 2)

    def test_receive_preserves_multiple_messages_from_one_read(self):
        class FakeSocket:
            def __init__(self, data):
                self.data = data

            def settimeout(self, _timeout):
                pass

            def recv(self, _size):
                data, self.data = self.data, b""
                return data

        first = scenario_client.frame(scenario_client.MSG_KEY_DOWN, b"up")
        second = scenario_client.frame(scenario_client.MSG_KEY_UP, b"up")
        parser = scenario_client.FrameParser()
        pending = []
        sock = FakeSocket(first + second)
        self.assertEqual(scenario_client.receive(sock, parser=parser, pending=pending),
                         (scenario_client.MSG_KEY_DOWN, b"up"))
        self.assertEqual(scenario_client.receive(sock, parser=parser, pending=pending),
                         (scenario_client.MSG_KEY_UP, b"up"))


if __name__ == "__main__":
    unittest.main()
