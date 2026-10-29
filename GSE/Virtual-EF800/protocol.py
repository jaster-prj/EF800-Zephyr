"""Versioned wire protocol shared by Virtual EF800 host clients."""
import struct
import json

WIDTH, HEIGHT = 240, 64
FRAME_SIZE = WIDTH * HEIGHT // 8
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
MSG_MTP_LIST = 21
MSG_MTP_DOWNLOAD = 22
MSG_MTP_UPLOAD_BEGIN = 23
MSG_MTP_UPLOAD_CHUNK = 24
MSG_MTP_UPLOAD_COMMIT = 25
MSG_MTP_CANCEL = 26
MSG_MTP_RESULT = 27
MSG_MTP_PROGRESS = 28
MSG_MTP_DOWNLOAD_CHUNK = 29
MSG_MTP_LIST_RESULT = 30
MSG_MTP_DOWNLOAD_NEXT = 31
MSG_LCD_CONTRAST = 32
LCD_CONTRAST_PAYLOAD = struct.Struct("<BB")
MSG_COMPOSITION_STATE = 33
MSG_COMPOSITION_CONTROL = 34
MSG_COMPOSITION_RESULT = 35
COMPOSITION_CONTROL_HEADER = struct.Struct("<IBB")
COMPOSITION_RESULT = struct.Struct("<IBi")
COMPOSITION_ID_MAX = 16

MTP_MAX_FILE_SIZE = 32 * 1024
MTP_MAX_PATH = 128
MTP_MAX_NAME = 96
MTP_MAX_OBJECTS = 12
MTP_CHUNK_SIZE = 2000
MTP_STORAGES = {"Data": 1, "Plugins": 2, "FWU": 3}
MTP_RESULT = struct.Struct("<IiIII")
MTP_PROGRESS = struct.Struct("<III")
MTP_LIST_RESULT = struct.Struct("<IiB")
MTP_LIST_ENTRY = struct.Struct("<IIBB")
MTP_DOWNLOAD_CHUNK = struct.Struct("<IIH")

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


def encode_lcd_contrast(enabled, duty):
    if not isinstance(enabled, bool) or type(duty) is not int or not 0 <= duty <= 100:
        raise ProtocolError("invalid LCD contrast state")
    return LCD_CONTRAST_PAYLOAD.pack(1 if enabled else 0, duty)


def decode_lcd_contrast(payload):
    if len(payload) != LCD_CONTRAST_PAYLOAD.size:
        raise ProtocolError("invalid LCD contrast payload size")
    enabled, duty = LCD_CONTRAST_PAYLOAD.unpack(payload)
    if enabled not in (0, 1) or duty > 100:
        raise ProtocolError("invalid LCD contrast value")
    return {"enabled": bool(enabled), "duty": duty}


def _valid_composition_id(value):
    return (isinstance(value, str) and 1 <= len(value) <= COMPOSITION_ID_MAX and
            "A" <= value[0] <= "Z" and
            all("A" <= char <= "Z" or "0" <= char <= "9"
                for char in value[1:]))


def encode_composition_control(request_id, group, token):
    if (type(request_id) is not int or not 1 <= request_id <= 0xFFFFFFFF or
            not _valid_composition_id(group) or
            not _valid_composition_id(token)):
        raise ProtocolError("invalid composition selection")
    group_bytes = group.encode("ascii")
    token_bytes = token.encode("ascii")
    return (COMPOSITION_CONTROL_HEADER.pack(request_id, len(group_bytes),
                                             len(token_bytes)) +
            group_bytes + token_bytes)


def decode_composition_control(payload):
    if len(payload) < COMPOSITION_CONTROL_HEADER.size:
        raise ProtocolError("truncated composition control")
    request_id, group_length, token_length = COMPOSITION_CONTROL_HEADER.unpack_from(payload)
    expected = COMPOSITION_CONTROL_HEADER.size + group_length + token_length
    if (request_id == 0 or not 1 <= group_length <= COMPOSITION_ID_MAX or
            not 1 <= token_length <= COMPOSITION_ID_MAX or len(payload) != expected):
        raise ProtocolError("invalid composition control bounds")
    try:
        group = payload[COMPOSITION_CONTROL_HEADER.size:
                        COMPOSITION_CONTROL_HEADER.size + group_length].decode("ascii")
        token = payload[-token_length:].decode("ascii")
    except UnicodeDecodeError as error:
        raise ProtocolError("composition identifiers must be ASCII") from error
    if not _valid_composition_id(group) or not _valid_composition_id(token):
        raise ProtocolError("invalid composition identifier")
    return {"requestId": request_id, "group": group, "token": token}


def decode_composition_result(payload):
    if len(payload) != COMPOSITION_RESULT.size:
        raise ProtocolError("invalid composition result size")
    request_id, status, error = COMPOSITION_RESULT.unpack(payload)
    if request_id == 0 or status not in (0, 1) or (status == 0 and error != 0):
        raise ProtocolError("invalid composition result")
    return {"requestId": request_id, "accepted": status == 0, "error": error}


def decode_composition_state(payload):
    if not 1 <= len(payload) <= MAX_PAYLOAD:
        raise ProtocolError("invalid composition state size")
    try:
        state = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProtocolError("invalid composition state JSON") from error
    if (not isinstance(state, dict) or state.get("type") != "composition" or
            type(state.get("version")) is not int or state.get("version") != 1 or
            type(state.get("available")) is not bool or
            type(state.get("restartRequired")) is not bool or
            not isinstance(state.get("groups"), list) or
            len(state["groups"]) > COMPOSITION_ID_MAX):
        raise ProtocolError("invalid composition state fields")
    group_ids = set()
    normalized_groups = []
    for group in state["groups"]:
        if (not isinstance(group, dict) or not _valid_composition_id(group.get("id")) or
                group["id"] in group_ids or
                not _valid_composition_id(group.get("active")) or
                not _valid_composition_id(group.get("selected")) or
                not isinstance(group.get("options"), list) or
                not 1 <= len(group["options"]) <= COMPOSITION_ID_MAX):
            raise ProtocolError("invalid composition group")
        group_ids.add(group["id"])
        options = []
        option_tokens = set()
        for option in group["options"]:
            if (not isinstance(option, dict) or
                    not _valid_composition_id(option.get("token")) or
                    option["token"] in option_tokens or
                    type(option.get("default")) is not bool):
                raise ProtocolError("invalid composition option")
            option_tokens.add(option["token"])
            options.append({"token": option["token"],
                            "default": option["default"]})
        if group["active"] not in option_tokens or group["selected"] not in option_tokens:
            raise ProtocolError("composition selection is not in its catalog")
        normalized_groups.append({"id": group["id"], "active": group["active"],
                                  "selected": group["selected"],
                                  "options": options})
    if not state["available"] and normalized_groups:
        raise ProtocolError("unavailable composition state has groups")
    return {"type": "composition", "version": 1,
            "available": state["available"],
            "restartRequired": state["restartRequired"],
            "groups": normalized_groups}


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
    expected = 4 + FRAME_SIZE
    if len(payload) != expected:
        raise ProtocolError("invalid LCD frame size")
    sequence = struct.unpack_from("<I", payload)[0]
    pixels = payload[4:4 + FRAME_SIZE]
    return {"sequence": sequence, "pixels": pixels}


def lcd_payload(sequence, pixels):
    if len(pixels) != FRAME_SIZE:
        raise ProtocolError("invalid framebuffer size")
    return struct.pack("<I", sequence) + bytes(pixels)


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
