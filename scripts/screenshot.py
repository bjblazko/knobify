#!/usr/bin/env python3
"""Captures the current display contents from a running knobify device.

Sends "SCREENSHOT\n" over the board's serial port, reads back the header
line LvglGlue::writeScreenshotToSerial() prints (width, height, bits per
pixel) followed by that many raw RGB565 bytes, and writes an uncompressed
24bpp BMP -- no extra image library needed on the host.

This exists so UI bugs on the round display can be diagnosed from an
actual capture instead of a written description or a phone photo (see
lib/ui/LvglGlue.h). Requires pyserial; PlatformIO already bundles it, so
if the system Python lacks it, run this via PlatformIO's own Python, e.g.:
  $(dirname $(which pio))/../Cellar/platformio/*/libexec/bin/python \\
      scripts/screenshot.py
(scripts/screenshot.sh finds a working interpreter automatically.)

Usage: screenshot.py [--port /dev/cu.XXXX] [-o output.bmp]
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    print(
        "pyserial not found in this Python. Try scripts/screenshot.sh, "
        "which locates PlatformIO's bundled interpreter automatically.",
        file=sys.stderr,
    )
    sys.exit(1)

BAUD = 115200


def find_port():
    from serial.tools import list_ports

    candidates = ("1A86:7523", "303A:1001")  # CH340, native USB-Serial-JTAG
    for p in list_ports.comports():
        hwid = (p.hwid or "").upper()
        if any(c in hwid for c in candidates):
            return p.device
    return None


def read_exact(ser, n):
    data = bytearray()
    while len(data) < n:
        chunk = ser.read(n - len(data))
        if not chunk:
            raise TimeoutError(
                f"expected {n} bytes, got {len(data)} before timeout"
            )
        data.extend(chunk)
    return bytes(data)


def capture(ser):
    ser.reset_input_buffer()
    ser.write(b"SCREENSHOT\n")
    ser.flush()

    deadline = time.time() + 5
    while time.time() < deadline:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            continue
        if line.startswith("SCREENSHOT_ERROR"):
            raise RuntimeError(line)
        if line.startswith("SCREENSHOT "):
            _, w, h, bpp = line.split()
            w, h, bpp = int(w), int(h), int(bpp)
            if bpp != 16:
                raise RuntimeError(f"unsupported bpp {bpp} (only RGB565/16 handled)")
            pixel_bytes = read_exact(ser, w * h * 2)
            return w, h, pixel_bytes
        # Anything else (boot/debug logging) is printed through and skipped.
        print(f"[device] {line}", file=sys.stderr)
    raise TimeoutError("no SCREENSHOT response within 5s")


def rgb565_to_bmp(w, h, pixel_bytes, path):
    # Bottom-up row order, 24bpp, no padding needed since w*3 (1080) is
    # already a multiple of 4 for this display's 360px width.
    row_bytes = w * 3
    assert row_bytes % 4 == 0, "row padding not implemented for this width"

    pixels = bytearray(row_bytes * h)
    for y in range(h):
        src_row = y * w * 2
        dst_row = (h - 1 - y) * row_bytes  # BMP rows are bottom-up.
        for x in range(w):
            lo = pixel_bytes[src_row + x * 2]
            hi = pixel_bytes[src_row + x * 2 + 1]
            value = lo | (hi << 8)  # little-endian uint16, LV_COLOR_16_SWAP=0
            r5 = (value >> 11) & 0x1F
            g6 = (value >> 5) & 0x3F
            b5 = value & 0x1F
            r8 = (r5 * 255) // 31
            g8 = (g6 * 255) // 63
            b8 = (b5 * 255) // 31
            dst = dst_row + x * 3
            pixels[dst] = b8  # BMP stores BGR.
            pixels[dst + 1] = g8
            pixels[dst + 2] = r8

    file_header = struct.pack(
        "<2sIHHI", b"BM", 14 + 40 + len(pixels), 0, 0, 14 + 40
    )
    dib_header = struct.pack(
        "<IiiHHIIiiII",
        40, w, h, 1, 24, 0, len(pixels), 2835, 2835, 0, 0,
    )
    with open(path, "wb") as f:
        f.write(file_header)
        f.write(dib_header)
        f.write(pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (auto-detected if omitted)")
    parser.add_argument(
        "-o", "--output", default="screenshot.bmp", help="output BMP path"
    )
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        print(
            "Could not auto-detect the board's serial port. Pass --port.",
            file=sys.stderr,
        )
        sys.exit(1)

    with serial.Serial(port, BAUD, timeout=1) as ser:
        w, h, pixel_bytes = capture(ser)

    rgb565_to_bmp(w, h, pixel_bytes, args.output)
    print(f"Saved {w}x{h} screenshot to {args.output}")


if __name__ == "__main__":
    main()
