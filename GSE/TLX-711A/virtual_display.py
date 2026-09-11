#!/usr/bin/env python3
"""Display TLX-711A framebuffer packets from the Zephyr emulator."""
import argparse
import tkinter as tk

WIDTH, HEIGHT, ROW_BYTES = 240, 64, 30
FRAME_SIZE = HEIGHT * ROW_BYTES
FRAME_PREFIX = b"FB: ["
FRAME_SUFFIX = b"]\r\n"

def crc16_ccitt(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc

class FrameReader:
    def __init__(self, stream):
        self.stream = stream
        self.buffer = bytearray()

    def read_frame(self):
        while True:
            data = self.stream.read(4096)
            if data:
                self.buffer.extend(data)
            start = self.buffer.find(FRAME_PREFIX)
            if start < 0:
                keep = len(FRAME_PREFIX) - 1
                if len(self.buffer) > keep:
                    del self.buffer[:-keep]
                continue
            if start:
                del self.buffer[:start]
            packet_length = len(FRAME_PREFIX) + FRAME_SIZE + len(FRAME_SUFFIX)
            if len(self.buffer) < packet_length:
                continue
            payload_start = len(FRAME_PREFIX)
            payload_end = payload_start + FRAME_SIZE
            if self.buffer[payload_end:packet_length] != FRAME_SUFFIX:
                del self.buffer[0]
                continue
            payload = bytes(self.buffer[payload_start:payload_end])
            del self.buffer[:packet_length]
            return payload

def run(port, baud, scale):
    import serial
    serial_port = serial.Serial(port, baudrate=baud, timeout=0.05)
    reader = FrameReader(serial_port)
    root = tk.Tk()
    root.title("TLX-711A virtual display")
    canvas = tk.Canvas(root, width=WIDTH * scale, height=HEIGHT * scale,
                       bg="white", highlightthickness=0)
    canvas.pack()
    image = tk.PhotoImage(width=WIDTH, height=HEIGHT)
    scaled_image = image.zoom(scale, scale)
    image_id = canvas.create_image(0, 0, image=scaled_image, anchor="nw")

    def poll():
        if serial_port.in_waiting:
            frame = reader.read_frame()
            if frame:
                for y in range(HEIGHT):
                    row = frame[y * ROW_BYTES:(y + 1) * ROW_BYTES]
                    colors = ["#111111" if row[x // 8] & (1 << (x % 8)) else "white"
                              for x in range(WIDTH)]
                    image.put("{" + " ".join(colors) + "}", to=(0, y))
                scaled_image = image.zoom(scale, scale)
                canvas.itemconfigure(image_id, image=scaled_image)
        root.after(10, poll)

    poll()
    root.mainloop()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="serial port, e.g. COM7 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=256000)
    parser.add_argument("--scale", type=int, default=4)
    args = parser.parse_args()
    run(args.port, args.baud, args.scale)

if __name__ == "__main__":
    main()
