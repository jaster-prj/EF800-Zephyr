import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))

from protocol import (FRAME_SIZE, FrameParser, LOG_GAP, LOG_REPLAY,
                       MSG_ACK, MSG_CAPABILITIES, MSG_KEY_DOWN, MSG_LCD_FRAME,
                       MSG_LOG_RECORD, ProtocolError, decode_lcd_frame,
                        decode_log_record, encode_log_record, encode_message,
                        lcd_payload, MSG_COMPOSITION_CONTROL,
                        encode_composition_control, decode_composition_control,
                        decode_composition_result, COMPOSITION_RESULT,
                        decode_composition_state)


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
        payload = lcd_payload(7, bytes(FRAME_SIZE))
        self.assertEqual(len(payload), 4 + FRAME_SIZE)
        decoded = decode_lcd_frame(payload)
        self.assertEqual(decoded["sequence"], 7)
        self.assertEqual(decoded["pixels"], bytes(FRAME_SIZE))
        self.assertNotIn("text", decoded)

    def test_lcd_decoder_rejects_legacy_text_row_payload(self):
        legacy_payload = lcd_payload(7, bytes(FRAME_SIZE)) + bytes(8 * 41)
        with self.assertRaises(ProtocolError):
            decode_lcd_frame(legacy_payload)

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

    def test_composition_control_is_bounded_and_round_trips(self):
        payload = encode_composition_control(19, "LCDCONTR", "LCDCONTR1")
        self.assertEqual(decode_composition_control(payload), {
            "requestId": 19, "group": "LCDCONTR", "token": "LCDCONTR1"})
        frame = FrameParser().feed(encode_message(MSG_COMPOSITION_CONTROL, payload))
        self.assertEqual(frame[0][0], MSG_COMPOSITION_CONTROL)
        with self.assertRaises(ProtocolError):
            encode_composition_control(0, "LCDCONTR", "LCDCONTR1")
        with self.assertRaises(ProtocolError):
            encode_composition_control(1, "LCDCONTR", "lowercase")
        with self.assertRaises(ProtocolError):
            decode_composition_control(payload[:-1])

    def test_composition_result_and_state_validation(self):
        result = decode_composition_result(COMPOSITION_RESULT.pack(4, 1, -114))
        self.assertEqual(result, {"requestId": 4, "accepted": False, "error": -114})
        state = decode_composition_state(b'{"type":"composition","version":1,"available":true,"restartRequired":false,"groups":[{"id":"BATSOC","active":"BATSOC2","selected":"BATSOC1","options":[{"token":"BATSOC0","default":false},{"token":"BATSOC1","default":false},{"token":"BATSOC2","default":true}]},{"id":"LCDCONTR","active":"LCDCONTR0","selected":"LCDCONTR1","options":[{"token":"LCDCONTR0","default":true},{"token":"LCDCONTR1","default":false}]}]}')
        self.assertTrue(state["available"])
        self.assertEqual(state["groups"][0]["id"], "BATSOC")
        self.assertEqual(state["groups"][0]["active"], "BATSOC2")
        self.assertEqual(state["groups"][0]["selected"], "BATSOC1")
        self.assertEqual(state["groups"][1]["selected"], "LCDCONTR1")
        with self.assertRaises(ProtocolError):
            decode_composition_result(COMPOSITION_RESULT.pack(0, 0, 0))
        with self.assertRaises(ProtocolError):
            decode_composition_state(b'{"type":"composition","version":1,"available":true,"restartRequired":false,"groups":[{"id":"LCDCONTR","active":"LCDCONTR9","selected":"LCDCONTR1","options":[{"token":"LCDCONTR0","default":true},{"token":"LCDCONTR1","default":false}]}]}')


if __name__ == "__main__":
    unittest.main()
