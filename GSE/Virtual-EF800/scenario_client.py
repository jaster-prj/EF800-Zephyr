#!/usr/bin/env python3
"""Headless deterministic client for the virtual EF800 front-panel protocol."""
import argparse
import socket
import time

from protocol import (FRAME_SIZE, HEADER, MSG_LCD_FRAME, MSG_KEY_DOWN,
                      MSG_KEY_RELEASE_ALL, MSG_KEY_UP, FrameParser,
                      ProtocolError, VERSION, MAGIC, decode_lcd_frame,
                      encode_message)

frame = encode_message


def receive(sock, timeout=2, parser=None, pending=None):
    sock.settimeout(timeout)
    parser = parser or FrameParser()
    pending = pending if pending is not None else []
    if pending:
        return pending.pop(0)
    while True:
        data = sock.recv(4096)
        if not data:
            raise RuntimeError("front-panel connection closed")
        try:
            messages = parser.feed(data)
        except ProtocolError as exc:
            raise RuntimeError(str(exc)) from exc
        if messages:
            pending.extend(messages[1:])
            return messages[0]


def wait_for_lcd(sock, timeout):
    deadline = time.monotonic() + timeout
    parser = FrameParser()
    pending = []
    while time.monotonic() < deadline:
        message_type, payload = receive(sock, max(0.01, deadline - time.monotonic()),
                                        parser, pending)
        if message_type == MSG_LCD_FRAME:
            decoded = decode_lcd_frame(payload)
            return decoded["sequence"], decoded["pixels"]
    raise TimeoutError("no LCD frame received")


def run(host, port, keys):
    with socket.create_connection((host, port), timeout=2) as sock:
        sequence, _frame = wait_for_lcd(sock, 2)
        print(f"initial LCD sequence={sequence}")
        for key in keys:
            encoded = key.encode("ascii")
            sock.sendall(frame(MSG_KEY_DOWN, encoded))
            sock.sendall(frame(MSG_KEY_UP, encoded))
            print(f"key={key}")
        sock.sendall(frame(MSG_KEY_RELEASE_ALL))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("keys", nargs="*", default=["down", "right"])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8800)
    args = parser.parse_args()
    run(args.host, args.port, args.keys)
