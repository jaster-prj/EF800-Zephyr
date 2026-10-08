import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))

from protocol import (FRAME_SIZE, FrameParser, LOG_GAP, LOG_REPLAY,
                       MSG_ACK, MSG_CAPABILITIES, MSG_KEY_DOWN, MSG_LCD_FRAME,
                       MSG_LOG_RECORD, ProtocolError, decode_lcd_frame,
                       decode_log_record, encode_log_record, encode_message,
                       lcd_payload)


class VirtualFrontPanelTests(unittest.TestCase):
    def test_parser_accepts_fragmented_frame(self):
        parser = FrameParser()
        frame = encode_message(MSG_KEY_DOWN, b"right")
        self.assertEqual(parser.feed(frame[:5]), [])
        self.assertEqual(parser.feed(frame[5:]), [(MSG_KEY_DOWN, b"right")])

    def test_parser_accepts_multiple_frames(self):
        parser = FrameParser()
        data = encode_message(MSG_KEY_DOWN, b"up") + encode_message(
            MSG_KEY_DOWN, b"down")
        self.assertEqual(parser.feed(data), [
            (MSG_KEY_DOWN, b"up"), (MSG_KEY_DOWN, b"down")])

    def test_parser_rejects_invalid_header(self):
        parser = FrameParser()
        with self.assertRaises(ProtocolError):
            parser.feed(b"bad-header!!")

    def test_frame_size(self):
        self.assertEqual(FRAME_SIZE, 1920)

    def test_parser_rejects_invalid_version_and_length(self):
        parser = FrameParser()
        with self.assertRaises(ProtocolError):
            parser.feed(encode_message(MSG_ACK)[:4] + bytes([99]) + encode_message(MSG_ACK)[5:])
        with self.assertRaises(ProtocolError):
            parser.feed(encode_message(MSG_ACK)[:6] + b"\xff\xff" + encode_message(MSG_ACK)[8:])

    def test_lcd_payload_round_trip(self):
        payload = lcd_payload(7, bytes(FRAME_SIZE), ["Main menu"] + [""] * 7)
        decoded = decode_lcd_frame(payload)
        self.assertEqual(decoded["sequence"], 7)
        self.assertEqual(decoded["pixels"], bytes(FRAME_SIZE))
        self.assertEqual(decoded["text"][0], "Main menu")

    def test_protocol_message_types(self):
        data = encode_message(MSG_CAPABILITIES, b"lcd=virtual") + encode_message(MSG_LCD_FRAME, lcd_payload(1, bytes(FRAME_SIZE)))
        messages = FrameParser().feed(data)
        self.assertEqual(messages[0], (MSG_CAPABILITIES, b"lcd=virtual"))
        self.assertEqual(decode_lcd_frame(messages[1][1])["sequence"], 1)

    def test_log_record_round_trip_and_flags(self):
        payload = encode_log_record(17, 2, "history warning", LOG_REPLAY | LOG_GAP)
        frame = encode_message(MSG_LOG_RECORD, payload)
        message = FrameParser().feed(frame)[0]
        decoded = decode_log_record(message[1])
        self.assertEqual(message[0], MSG_LOG_RECORD)
        self.assertEqual(decoded["sequence"], 17)
        self.assertEqual(decoded["severity"], "WARNING")
        self.assertEqual(decoded["flags"], LOG_REPLAY | LOG_GAP)
        self.assertEqual(decoded["text"], "history warning")

    def test_log_record_rejects_invalid_bounds(self):
        with self.assertRaises(ProtocolError):
            encode_log_record(1, 2, "x" * 121)
        payload = bytearray(encode_log_record(1, 1, "ok"))
        payload[0] = 9
        with self.assertRaises(ProtocolError):
            decode_log_record(payload)


if __name__ == "__main__":
    unittest.main()
