"""Capture a bounded USB Serial/JTAG session; never write device flash."""

import argparse
from datetime import datetime, timezone
from pathlib import Path
import sys
import time

import serial
from esptool.reset import HardReset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 0 < args.seconds <= 120:
        parser.error("--seconds must be between 0 and 120")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        # Set control lines before opening, avoiding pySerial's default pulse.
        with serial.Serial(port=None, baudrate=115200, timeout=0.2) as port:
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            port.reset_input_buffer()
            with args.output.open("wb") as output:
                stamp = datetime.now(timezone.utc).isoformat()
                header = f"# UTC {stamp}; {args.port}; reset={args.reset}\n"
                output.write(header.encode("utf-8"))
                print(header, end="", flush=True)
                if args.reset:
                    # esptool's hard reset starts the application, not download mode.
                    HardReset(port, uses_usb=True)()
                deadline = time.monotonic() + args.seconds
                while time.monotonic() < deadline:
                    data = port.read(max(1, min(port.in_waiting, 4096)))
                    if data:
                        output.write(data)
                        output.flush()
                        print(data.decode("utf-8", errors="replace"), end="", flush=True)
        print(f"\nCapture complete; {args.port} closed. Log: {args.output}")
        return 0
    except (serial.SerialException, OSError) as error:
        print(f"Serial capture failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
