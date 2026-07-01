#!/usr/bin/env python3
"""Merge PlatformIO build artifacts into a single flashable image.

ESP Web Tools flashes one contiguous image at offset 0. This combines the
bootloader, partition table, boot_app0, the app, and the LittleFS filesystem
into one `.bin` using `esptool merge_bin`.

Usage:
    python scripts/merge_bin.py --env esp32cam --out pages/firmware/angleros-esp32cam.bin
"""

import argparse
import subprocess
import sys
from pathlib import Path

# Flash offsets for a 4MB ESP32 with the huge_app partition scheme.
# NOTE: verify the fs offset against the generated partition table for the env.
LAYOUT = [
    ("0x1000", "bootloader.bin"),
    ("0x8000", "partitions.bin"),
    ("0xe000", "boot_app0.bin"),
    ("0x10000", "firmware.bin"),
    ("0x310000", "littlefs.bin"),
]


def find(build_dir: Path, name: str) -> Path:
    hit = next(build_dir.rglob(name), None)
    if hit is None:
        sys.exit(f"error: could not find {name} under {build_dir}")
    return hit


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    build_dir = Path("firmware/.pio/build") / args.env
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    merge_args = []
    for offset, name in LAYOUT:
        merge_args += [offset, str(find(build_dir, name))]

    cmd = [
        sys.executable, "-m", "esptool", "--chip", "esp32",
        "merge_bin", "-o", str(out), "--fill-flash-size", "4MB",
        *merge_args,
    ]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
