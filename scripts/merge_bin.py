#!/usr/bin/env python3
"""Merge PlatformIO build artifacts into a single flashable image.

ESP Web Tools flashes one contiguous image at offset 0. This combines the
bootloader, partition table, boot_app0, the app, and the LittleFS filesystem
into one `.bin` using `esptool merge_bin`.

Usage:
    python scripts/merge_bin.py --env esp32cam --chip esp32 --flash-size 4MB \
        --fs-offset 0x310000 --out pages/firmware/angleros-esp32cam.bin
    python scripts/merge_bin.py --env esp32s3cam --chip esp32s3 --flash-size 16MB \
        --fs-offset 0xc90000 --out pages/firmware/angleros-esp32s3cam.bin
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# The second-stage bootloader lives at a chip-specific offset.
BOOTLOADER_OFFSET = {
    "esp32": "0x1000",
    "esp32s3": "0x0",
}


def pio_core_dir() -> Path:
    return Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))


def find(build_dir: Path, name: str) -> Path:
    # Prefer the env's build dir; boot_app0.bin lives in the framework package,
    # not the build dir, so fall back to the PlatformIO packages tree.
    hit = next(build_dir.rglob(name), None)
    if hit is None:
        packages = pio_core_dir() / "packages"
        if packages.exists():
            hit = next(packages.rglob(name), None)
    if hit is None:
        sys.exit(f"error: could not find {name} under {build_dir} or PlatformIO packages")
    return hit


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", required=True)
    ap.add_argument("--chip", required=True, choices=sorted(BOOTLOADER_OFFSET))
    ap.add_argument("--flash-size", required=True, help="e.g. 4MB, 16MB")
    ap.add_argument("--fs-offset", required=True,
                    help="LittleFS partition offset from the env's partition table")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    layout = [
        (BOOTLOADER_OFFSET[args.chip], "bootloader.bin"),
        ("0x8000", "partitions.bin"),
        ("0xe000", "boot_app0.bin"),
        ("0x10000", "firmware.bin"),
        (args.fs_offset, "littlefs.bin"),
    ]

    build_dir = Path("firmware/.pio/build") / args.env
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    merge_args = []
    for offset, name in layout:
        merge_args += [offset, str(find(build_dir, name))]

    cmd = [
        sys.executable, "-m", "esptool", "--chip", args.chip,
        "merge_bin", "-o", str(out), "--fill-flash-size", args.flash_size,
        *merge_args,
    ]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
