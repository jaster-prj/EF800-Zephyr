"""Versioned wire protocol shared by Virtual EF800 host clients."""
import struct

WIDTH, HEIGHT = 240, 64
FRAME_SIZE = WIDTH * HEIGHT // 8
TEXT_ROWS, TEXT_COLUMNS = 8, 40
MAGIC = 0x56454638
VERSION = 1
MAX_PAYLOAD = 2400
HEADER = struct.Struct("<IBBHI")
MSG_HELLO = 1
MSG_CAPABILITIES = 2
MSG_LCD_FRAME = 3
MSG_KEY_DOWN = 4
MSG_KEY_UP = 5
MSG_KEY_RELEASE_ALL = 6
MSG_ACK = 7
MSG_STATE = 8
MSG_PROFILE_CONTROL = 9
MSG_PROFILE_STATE = 10
MSG_PLUGIN_UPLOAD_BEGIN = 11
MSG_PLUGIN_UPLOAD_CHUNK = 12
MSG_PLUGIN_UPLOAD_COMMIT = 13
MSG_PLUGIN_STATUS = 14
MSG_SIMULATOR_RESTART = 15
MSG_DEVICE_DOCK = 16
MSG_DEVICE_STATE = 17
MSG_PLUGIN_ASSIGN = 18
MSG_PLUGIN_EXECUTE = 19
MSG_LOG_RECORD = 20

LOG_MAX_TEXT = 120
LOG_HISTORY_COUNT = 64
LOG_HISTORY_BYTES = 8192
LOG_QUEUE_COUNT = 32
LOG_PAYLOAD_HEADER = struct.Struct("<BBIH")
LOG_REPLAY = 1 << 0
LOG_TRUNCATED = 1 << 1
LOG_GAP = 1 << 2
LOG_EPOCH = 1 << 3
LOG_SEVERITIES = {0: "DEBUG", 1: "INFO", 2: "WARNING", 3: "ERROR", 4: "PANIC"}


class ProtocolError(ValueError):
    pass


def encode_message(message_type, payload=b""):
    payload = bytes(payload)
    if not 0 <= message_type <= 255 or len(payload) > MAX_PAYLOAD:
        raise ProtocolError("invalid message type or payload size")
    return HEADER.pack(MAGIC, VERSION, message_type, len(payload), 0) + payload


class FrameParser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        messages = []
        while len(self.buffer) >= HEADER.size:
            magic, version, message_type, length, reserved = HEADER.unpack(
                self.buffer[:HEADER.size])
            if magic != MAGIC or version != VERSION or reserved != 0:
                raise ProtocolError("invalid virtual EF800 frame header")
            if length > MAX_PAYLOAD:
                raise ProtocolError("payload too large")
            total = HEADER.size + length
            if len(self.buffer) < total:
                break
            payload = bytes(self.buffer[HEADER.size:total])
            del self.buffer[:total]
            messages.append((message_type, payload))
        return messages


def decode_lcd_frame(payload):
    expected = 4 + FRAME_SIZE + TEXT_ROWS * (TEXT_COLUMNS + 1)
    if len(payload) != expected:
        raise ProtocolError("invalid LCD frame size")
    sequence = struct.unpack_from("<I", payload)[0]
    pixels = payload[4:4 + FRAME_SIZE]
    text_start = 4 + FRAME_SIZE
    text = []
    for index in range(TEXT_ROWS):
        start = text_start + index * (TEXT_COLUMNS + 1)
        text.append(payload[start:start + TEXT_COLUMNS].decode("ascii", errors="replace").rstrip("\x00 "))
    return {"sequence": sequence, "pixels": pixels, "text": text}


def lcd_payload(sequence, pixels, text=None):
    if len(pixels) != FRAME_SIZE:
        raise ProtocolError("invalid framebuffer size")
    rows = list(text or [""] * TEXT_ROWS)
    if len(rows) != TEXT_ROWS:
        raise ProtocolError("invalid text row count")
    encoded = bytearray(struct.pack("<I", sequence))
    encoded.extend(pixels)
    for row in rows:
        encoded.extend(row[:TEXT_COLUMNS].ljust(TEXT_COLUMNS).encode("ascii", errors="replace"))
        encoded.append(0)
    return bytes(encoded)


def encode_log_record(sequence, severity, text, flags=0):
    if severity not in LOG_SEVERITIES or flags & ~(LOG_REPLAY | LOG_TRUNCATED | LOG_GAP | LOG_EPOCH):
        raise ProtocolError("invalid log severity or flags")
    encoded = str(text).encode("ascii", errors="replace")
    if len(encoded) > LOG_MAX_TEXT:
        raise ProtocolError("log text too large")
    return LOG_PAYLOAD_HEADER.pack(severity, flags, sequence & 0xFFFFFFFF, len(encoded)) + encoded


def decode_log_record(payload):
    if len(payload) < LOG_PAYLOAD_HEADER.size:
        raise ProtocolError("invalid log payload size")
    severity, flags, sequence, text_length = LOG_PAYLOAD_HEADER.unpack_from(payload)
    if severity not in LOG_SEVERITIES or flags & ~(LOG_REPLAY | LOG_TRUNCATED | LOG_GAP | LOG_EPOCH):
        raise ProtocolError("invalid log severity or flags")
    if text_length > LOG_MAX_TEXT or len(payload) != LOG_PAYLOAD_HEADER.size + text_length:
        raise ProtocolError("invalid log text size")
    return {"sequence": sequence, "severity": LOG_SEVERITIES[severity],
            "severityCode": severity, "flags": flags,
            "text": payload[LOG_PAYLOAD_HEADER.size:].decode("ascii")}
