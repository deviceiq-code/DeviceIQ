#!/usr/bin/env python3
"""Packages firmware.bin and littlefs.bin into a single DeviceIQ OTA file.

Build both targets first, then run this script:
    pio run
    pio run --target buildfs
    python3 scripts/package_ota.py

Container format (32-byte header, little-endian - must match the
OTAPackageHeader struct in src/web/HTTPServer.cpp):
    8s   magic              b"DIQOTA01"
    B    header version     1
    B    software major
    B    software minor
    B    software revision
    I    firmware length (bytes)
    I    firmware CRC32     (zlib/IEEE 802.3, matches Python's zlib.crc32)
    I    filesystem length (bytes)
    I    filesystem CRC32
    I    reserved (0)
... followed by <firmware length> bytes of firmware.bin, then
    <filesystem length> bytes of littlefs.bin.
"""
import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"DIQOTA01"
HEADER_VERSION = 1
HEADER_FORMAT = "<8sBBBBIIIII"

ROOT = Path(__file__).resolve().parent.parent


def read_version(platformio_ini: Path):
    text = platformio_ini.read_text()

    def find(name):
        match = re.search(rf"DEVICEIQ_VERSION_{name}\s*=\s*(\d+)", text)
        if not match:
            raise SystemExit(f"Could not find DEVICEIQ_VERSION_{name} in {platformio_ini}")
        return int(match.group(1))

    return find("MAJOR"), find("MINOR"), find("REVISION")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--firmware", default=str(ROOT / ".pio/build/esp32dev/firmware.bin"), help="Path to the built firmware.bin")
    parser.add_argument("--filesystem", default=str(ROOT / ".pio/build/esp32dev/littlefs.bin"), help="Path to the built littlefs.bin")
    parser.add_argument("--platformio-ini", default=str(ROOT / "platformio.ini"), help="Where to read DEVICEIQ_VERSION_* from")
    parser.add_argument("-o", "--output", default=None, help="Output path (default: alongside firmware.bin, named by version)")
    args = parser.parse_args()

    firmware_path = Path(args.firmware)
    filesystem_path = Path(args.filesystem)

    if not firmware_path.is_file():
        raise SystemExit(f"Firmware image not found: {firmware_path}\nRun 'pio run' first.")
    if not filesystem_path.is_file():
        raise SystemExit(f"Filesystem image not found: {filesystem_path}\nRun 'pio run --target buildfs' first.")

    major, minor, revision = read_version(Path(args.platformio_ini))

    firmware = firmware_path.read_bytes()
    filesystem = filesystem_path.read_bytes()

    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        HEADER_VERSION,
        major, minor, revision,
        len(firmware), zlib.crc32(firmware) & 0xFFFFFFFF,
        len(filesystem), zlib.crc32(filesystem) & 0xFFFFFFFF,
        0,
    )
    if len(header) != 32:
        raise SystemExit(f"internal error: header is {len(header)} bytes, expected 32")

    output_path = Path(args.output) if args.output else firmware_path.parent / f"deviceiq-ota-{major}.{minor}.{revision}.bin"
    output_path.write_bytes(header + firmware + filesystem)

    print(f"DeviceIQ {major}.{minor}.{revision}")
    print(f"  Firmware:   {len(firmware):>10,} bytes")
    print(f"  Filesystem: {len(filesystem):>10,} bytes")
    print(f"  Package:    {output_path} ({output_path.stat().st_size:,} bytes)")


if __name__ == "__main__":
    sys.exit(main())
