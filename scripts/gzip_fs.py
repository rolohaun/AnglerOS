#!/usr/bin/env python3
"""Gzip the web assets in the LittleFS data dir (in place; originals removed).

ESPAsyncWebServer looks for `<file>.gz` first and serves it with
Content-Encoding: gzip, so shipping only the compressed variants cuts both the
flash reads and the Wi-Fi bytes ~4x. Run in CI before `pio run -t buildfs`.
"""

import gzip
import sys
from pathlib import Path

EXTS = {".html", ".css", ".js", ".json", ".svg", ".txt", ".md", ".ps1", ".sh"}


def main() -> None:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "firmware/data")
    total_in = total_out = 0
    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.suffix.lower() not in EXTS:
            continue
        data = p.read_bytes()
        gz = p.with_name(p.name + ".gz")
        with gzip.open(gz, "wb", compresslevel=9) as f:
            f.write(data)
        p.unlink()
        total_in += len(data)
        total_out += gz.stat().st_size
        print(f"gzip {p.relative_to(root)}: {len(data)} -> {gz.stat().st_size} bytes")
    print(f"total: {total_in} -> {total_out} bytes")


if __name__ == "__main__":
    main()
