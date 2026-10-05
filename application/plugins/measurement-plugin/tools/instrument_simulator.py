#!/usr/bin/env python3
"""Minimal pyserial ELTA3/FTS3XX instrument simulator."""
import argparse
import time

import serial

parser = argparse.ArgumentParser()
parser.add_argument("port")
parser.add_argument("--instrument", choices=("elta3", "fts3xx"), required=True)
args = parser.parse_args()
settings = ({"baudrate": 1200, "bytesize": serial.SEVENBITS,
             "parity": serial.PARITY_ODD, "stopbits": serial.STOPBITS_TWO}
            if args.instrument == "elta3" else
            {"baudrate": 9600, "bytesize": serial.EIGHTBITS,
             "parity": serial.PARITY_NONE, "stopbits": serial.STOPBITS_ONE})
with serial.Serial(args.port, timeout=0.1, **settings) as port:
    print(f"simulating {args.instrument} on {args.port}")
    pending_measurement = False
    while True:
        command = port.read_until(b"\n")
        if not command:
            continue
        print("rx", command.hex(" "))
        if args.instrument == "fts3xx" and command == b"\x11\r\n":
            port.write(b"1234 0452700 0900000\r\n")
        elif args.instrument == "elta3" and command == b"S14\r\n":
            port.write(b"A\r\n")
            pending_measurement = True
        elif args.instrument == "elta3" and command == b"B\r\n" and pending_measurement:
            port.write(b"#48 0 1.234; 100.0000; 50.5000\r\n")
        elif args.instrument == "elta3" and command == b"Q\r\n":
            pending_measurement = False
        time.sleep(0.01)
