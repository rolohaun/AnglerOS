# Verifying a generated `config.ini` (local build & flash)

This is the sanity check that AnglerOS produces a **real, buildable Marlin
configuration**: take the `config.ini` from the Configuration tab, build Marlin
for the SKR Pico on your desktop with PlatformIO, and flash it over USB. Real
`pio` output = real error logging while we refine the schema.

## How it works

Marlin's own `platformio.ini` runs a pre-build script
(`buildroot/share/PlatformIO/scripts/configuration.py`) that **auto-applies
`Marlin/config.ini`** on every build. Our file uses:

```ini
[config:base]
ini_use_config = example/delta/generic @ bugfix-2.1.x, base
MOTHERBOARD = BOARD_BTT_SKR_PICO
# ...overrides...
```

> **SKR Pico note:** RP2040 support isn't in Marlin's latest *stable* release —
> it lives in the **`bugfix-2.1.x`** branch (lands in 2.1.3). So for this board
> both the Marlin source **and** the base-example ref use `bugfix-2.1.x`. The
> generated `config.ini` header records the exact refs it targets.

`ini_use_config` pulls the delta base example from the MarlinFirmware
**Configurations** repo (each release is a branch named `release-<tag>`), then
applies our `[config:base]` overrides on top.

## Prerequisites

- [VS Code](https://code.visualstudio.com/) + the **PlatformIO IDE** extension
- `git` on your PATH
- Internet (the build fetches the base example)

## Get the config

Configuration tab → **Download config.ini**.

## Option A — one command (Windows PowerShell)

From the repo root:

```powershell
./tools/build-local.ps1 -Config "$HOME\Downloads\config.ini"
```

It reads the target Marlin release from the file's header, clones Marlin at that
ref into `./marlin-build`, drops the `config.ini` in place, and builds
`env:SKR_Pico`. On success it prints the path to `firmware.uf2`.

To force a ref (e.g. the dev branch): `-Tag bugfix-2.1.x`.

## Option B — manual (any OS, VS Code GUI)

1. Clone Marlin at the ref shown in the config header (`Marlin source tag:`).
   For the SKR Pico that's the dev branch:
   ```
   git clone --depth 1 --branch bugfix-2.1.x https://github.com/MarlinFirmware/Marlin.git
   ```
2. Copy your `config.ini` to **`Marlin/config.ini`** (the inner `Marlin/`
   folder, next to `Configuration.h`).
3. Open the cloned folder in VS Code. In the **PlatformIO** sidebar →
   *Project Tasks* → **SKR_Pico** → **Build**.
4. Watch the terminal. A clean build ends with the `.uf2` at
   `.pio/build/SKR_Pico/firmware.uf2`.

## Flash the SKR Pico

- **BOOTSEL (simplest):** hold **BOOT** on the SKR Pico, plug in USB (it mounts
  as a drive named `RPI-RP2`), and copy `firmware.uf2` onto it. It reboots into
  Marlin.
- **PlatformIO Upload:** with the board in BOOTSEL mode, run the **Upload** task
  (uses `picotool` over USB).

Confirm it's alive: connect a serial terminal at 115200 and send `M115` — it
should report the Marlin version and `BOARD_BTT_SKR_PICO`.

## If the build fails

That's the point — copy the `pio` error back and we fix the schema/generator.
Common first-pass culprits:

- A `#define` name mismatch (config.ini key not recognized) → we correct the
  mapping in `firmware/data/schema/printer.schema.json` or the generator.
- The base example ref didn't resolve → check the `ini_use_config` line's
  `@ release-...` branch exists for that release.
- A feature enabled without its dependencies (e.g. sensorless homing) → we gate
  or add the required companions.
