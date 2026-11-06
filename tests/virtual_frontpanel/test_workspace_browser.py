#!/usr/bin/env python3
"""Exercise the production workspace browser through the native-sim front panel."""

import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PROTOCOL_DIR = ROOT / "GSE" / "Virtual-EF800"
sys.path.insert(0, str(PROTOCOL_DIR))

from protocol import (  # noqa: E402
    MTP_RESULT,
    MSG_LCD_FRAME,
    MSG_KEY_DOWN,
    MSG_KEY_UP,
    MSG_MTP_PROGRESS,
    MSG_MTP_RESULT,
    MSG_MTP_UPLOAD_BEGIN,
    MSG_MTP_UPLOAD_CHUNK,
    MSG_MTP_UPLOAD_COMMIT,
    FrameParser,
    decode_lcd_frame,
    encode_message,
)


SIMULATOR = ROOT / "build" / "ef800-main-native-sim" / "zephyr" / "zephyr.exe"


class WorkspaceBrowserNativeSimTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not SIMULATOR.is_file():
            raise unittest.SkipTest(f"native-sim executable not found: {SIMULATOR}")
        cls.flash_directory = tempfile.TemporaryDirectory(
            prefix="ef800-workspace-browser-", dir="/workspace"
        )
        cls.process = None
        cls.socket = None
        cls.addClassCleanup(cls._cleanup)
        flash_path = pathlib.Path(cls.flash_directory.name) / "flash.bin"
        cls.process = subprocess.Popen(
            [str(SIMULATOR), f"-flash={flash_path}"], cwd="/workspace",
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            try:
                cls.socket = socket.create_connection(("127.0.0.1", 8800), timeout=1.0)
                break
            except OSError:
                if cls.process.poll() is not None:
                    raise RuntimeError("native-sim exited before opening its front panel")
                time.sleep(0.1)
        if cls.socket is None:
            raise TimeoutError("native-sim front panel did not listen on port 8800")
        cls.socket.settimeout(0.25)
        cls.parser = FrameParser()
        cls.pending = []
        cls.latest_frame = None
        cls.request_id = 1
        try:
            cls.wait_for_stable_lcd(timeout=30.0)
        except Exception as error:
            cls.process.terminate()
            output, _ = cls.process.communicate(timeout=5.0)
            raise RuntimeError(
                "native-sim did not publish an LCD pixel frame:\n" +
                f"last LCD frame: {cls.latest_frame!r}\n" +
                output.decode("utf-8", errors="replace")
            ) from error

    @classmethod
    def tearDownClass(cls):
        cls._cleanup()

    @classmethod
    def _cleanup(cls):
        if cls.socket is not None:
            cls.socket.close()
            cls.socket = None
        if cls.process is not None and cls.process.poll() is None:
            cls.process.terminate()
            try:
                cls.process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                cls.process.kill()
                cls.process.wait(timeout=5.0)
        if cls.process is not None and cls.process.stdout is not None:
            cls.process.stdout.close()
        cls.process = None
        if getattr(cls, "flash_directory", None) is not None:
            cls.flash_directory.cleanup()
            cls.flash_directory = None

    @classmethod
    def _receive_message(cls, deadline):
        while time.monotonic() < deadline:
            if cls.pending:
                return cls.pending.pop(0)
            try:
                data = cls.socket.recv(4096)
            except socket.timeout:
                continue
            if not data:
                raise RuntimeError("native-sim front panel disconnected")
            cls.pending.extend(cls.parser.feed(data))
        raise TimeoutError("timed out waiting for a native-sim message")

    @classmethod
    def wait_for_lcd(cls, predicate=None, timeout=8.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            message_type, payload = cls._receive_message(deadline)
            if message_type != MSG_LCD_FRAME:
                continue
            frame = decode_lcd_frame(payload)
            cls.latest_frame = frame
            if predicate is None or predicate(frame):
                return frame
        raise TimeoutError("timed out waiting for the expected LCD pixel frame")

    @classmethod
    def wait_for_stable_lcd(cls, timeout=30.0, stable_for=0.75):
        deadline = time.monotonic() + timeout
        last_sequence = None
        last_change = None
        while time.monotonic() < deadline:
            receive_deadline = min(deadline, time.monotonic() + 0.2)
            try:
                message_type, payload = cls._receive_message(receive_deadline)
            except TimeoutError:
                if (cls.latest_frame is not None and last_change is not None and
                        time.monotonic() - last_change >= stable_for):
                    return cls.latest_frame
                continue
            if message_type != MSG_LCD_FRAME:
                continue
            frame = decode_lcd_frame(payload)
            cls.latest_frame = frame
            if frame["sequence"] != last_sequence:
                last_sequence = frame["sequence"]
                last_change = time.monotonic()
        raise TimeoutError("LCD framebuffer did not settle during MainApplication startup")

    @classmethod
    def send_key(cls, key):
        encoded = key.encode("ascii")
        cls.socket.sendall(encode_message(MSG_KEY_DOWN, encoded))
        cls.socket.sendall(encode_message(MSG_KEY_UP, encoded))

    @classmethod
    def press(cls, key):
        previous = 0 if cls.latest_frame is None else cls.latest_frame["sequence"]
        cls.send_key(key)
        return cls.wait_for_lcd(
            lambda frame: frame["sequence"] != previous, timeout=8.0
        )

    @classmethod
    def wait_for_mtp_result(cls, request_id, timeout=30.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            message_type, payload = cls._receive_message(deadline)
            if message_type == MSG_LCD_FRAME:
                cls.latest_frame = decode_lcd_frame(payload)
            elif message_type == MSG_MTP_PROGRESS:
                continue
            elif message_type == MSG_MTP_RESULT:
                response_id, status, *_ = MTP_RESULT.unpack(payload)
                if response_id == request_id:
                    if status != 0:
                        raise AssertionError(f"virtual MTP upload failed: {status}")
                    return
        raise TimeoutError("timed out waiting for virtual MTP completion")

    @classmethod
    def upload_data_file(cls, filename, content):
        request_id = cls.request_id
        cls.request_id += 1
        path = filename.encode("ascii")
        checksum = zlib.crc32(content) & 0xFFFFFFFF
        begin = struct.pack("<IBBII", request_id, 1, len(path), len(content), checksum)
        cls.socket.sendall(encode_message(MSG_MTP_UPLOAD_BEGIN, begin + path))
        cls.wait_for_mtp_result(request_id)
        chunk_size = 1800
        for offset in range(0, len(content), chunk_size):
            payload = struct.pack("<II", request_id, offset) + content[offset:offset + chunk_size]
            cls.socket.sendall(encode_message(MSG_MTP_UPLOAD_CHUNK, payload))
        cls.socket.sendall(encode_message(
            MSG_MTP_UPLOAD_COMMIT, struct.pack("<I", request_id)
        ))
        cls.wait_for_mtp_result(request_id)

    def assert_lcd_framebuffer(self):
        self.assertIsNotNone(self.latest_frame)
        self.assertEqual(len(self.latest_frame["pixels"]), 1920)
        self.assertNotIn("text", self.latest_frame)

    def test_select_edit_cancel_and_delete_workspace_rows(self):
        workspace_name = f"ui{time.time_ns() % 100000:05d}"

        # Enable the production MainApplication MTP personality on USB Port 4.
        self.press("e")
        self.assert_lcd_framebuffer()
        self.press("p")
        self.press("4")
        self.press("m")
        self.assert_lcd_framebuffer()
        self.press("enter")
        self.assert_lcd_framebuffer()
        self.upload_data_file(
            f"{workspace_name}.dat",
            b"L" * 300 + b"\r\n\r\nsecond\rthird\n-STP 1.000 P001 \r\n",
        )

        # Select the newly uploaded .dat file through the normal Workspace
        # management menu; no test helper writes directly to the filesystem.
        self.press("escape")
        self.press("escape")
        self.press("e")
        self.press("w")
        self.press("e")
        self.assert_lcd_framebuffer()
        self.press("enter")
        self.assert_lcd_framebuffer()
        self.press("escape")
        self.press("escape")
        self.press("w")
        self.press("m")
        self.assert_lcd_framebuffer()

        # Selection moves through rows and exposes Modify/Delete/Cancel.
        self.press("up")
        self.assert_lcd_framebuffer()
        self.press("up")
        self.assert_lcd_framebuffer()
        self.press("up")
        self.assert_lcd_framebuffer()
        self.press("up")
        self.assert_lcd_framebuffer()
        self.press("enter")
        self.assert_lcd_framebuffer()

        # A long row remains deletable, but Modify reports the bounded editor
        # limit. Canceling the delete prompt leaves the row and selection intact.
        self.press("m")
        self.assert_lcd_framebuffer()
        self.press("d")
        self.assert_lcd_framebuffer()
        self.press("escape")
        self.press("c")
        self.assert_lcd_framebuffer()

        # Move to a short row and exercise overwrite, Insert mode,
        # Escape cancellation, delete cancellation, and confirmed deletion.
        self.press("down")
        self.press("down")
        self.press("down")
        self.assert_lcd_framebuffer()
        self.press("enter")
        self.press("m")
        self.assert_lcd_framebuffer()
        self.press("insert")
        self.assert_lcd_framebuffer()
        self.press("left")
        self.press("x")
        self.press("enter")
        self.assert_lcd_framebuffer()

        self.press("enter")
        self.press("m")
        self.press("z")
        self.press("enter")
        self.assert_lcd_framebuffer()

        self.press("enter")
        self.press("m")
        self.press("left")
        self.press("delete")
        self.press("enter")
        self.assert_lcd_framebuffer()

        self.press("enter")
        self.press("m")
        self.press("backspace")
        self.press("q")
        self.press("escape")
        self.press("c")
        self.assert_lcd_framebuffer()

        self.press("enter")
        self.press("d")
        self.press("escape")
        self.press("c")
        self.assert_lcd_framebuffer()

        self.press("enter")
        self.press("d")
        self.assert_lcd_framebuffer()
        self.press("enter")
        self.assert_lcd_framebuffer()

        # The over-limit row still deletes through the same confirmed path.
        self.press("up")
        self.press("up")
        self.press("up")
        self.assert_lcd_framebuffer()
        self.press("enter")
        self.press("d")
        self.press("enter")
        self.assert_lcd_framebuffer()

        # The current-position summary remains read-only.
        self.press("escape")
        self.press("p")
        self.assert_lcd_framebuffer()
        previous = self.latest_frame["sequence"]
        self.send_key("enter")
        self.wait_for_lcd(
            lambda frame: frame["sequence"] != previous, timeout=8.0
        )
        self.assert_lcd_framebuffer()


if __name__ == "__main__":
    unittest.main()
