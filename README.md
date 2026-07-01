# AnglerOS

**A Mainsail-style web interface, self-hosted on an ESP32, for configuring and
flashing [Marlin](https://marlinfw.org/) firmware.**

AnglerOS aims to make setting up Marlin on RP2040-class mainboards as painless as
Klipper's single config file. It runs on an inexpensive **ESP32-CAM**, serves a
dark-themed web UI (green accents, inspired by Mainsail), and acts as the bridge
between the user and Marlin's sprawling `Configuration.h` / `Configuration_adv.h`.

> Status: **early development.** First target board: **BigTreeTech SKR Pico**
> (RP2040) on a custom Delta printer.

## What it does

- **Dashboard** — live temperatures, jog controls, G-code select & print, and a
  print-monitoring camera feed (OV3660 + onboard flash LED).
- **Configuration** — a Klipper-like "one page" config that exposes only the
  settings a printer actually needs (steppers, TMC2209-over-UART, hotend, bed,
  kinematics geometry), then drives an automated build + flash pipeline.

## How it works

The ESP32 can't compile Marlin itself, so AnglerOS offloads the heavy lifting:

1. You fill in the Configuration tab. AnglerOS generates a Marlin `config.ini`.
2. It triggers a **GitHub Actions** build that compiles Marlin at the latest
   stable release and produces a `firmware.uf2`.
3. You flash the `.uf2` to the SKR Pico via BOOTSEL (drag-and-drop).

Installing **AnglerOS itself** onto the ESP32 needs no toolchain: a
[browser flasher](pages/) (ESP Web Tools / Web Serial) hosted on GitHub Pages
writes the firmware over the CH340 USB-serial adapter straight from Chrome/Edge.

## Hardware

| Part | Notes |
|------|-------|
| ESP32-CAM (AI-Thinker/Aideepen) | OV3660 camera, CH340 programmer adapter |
| BigTreeTech SKR Pico | RP2040 mainboard running Marlin |
| UART link | ESP32 ↔ SKR Pico host serial (3.3V, see [docs/hardware.md](docs/hardware.md)) |

## Repository layout

```
firmware/          PlatformIO project for the ESP32-CAM
  data/            LittleFS web UI (SPA) + config schema + board profiles
.github/workflows/ CI: build AnglerOS firmware + build Marlin from config
pages/             GitHub Pages browser flasher (ESP Web Tools)
scripts/           build helpers (config prep, bin merge)
docs/              wiring, flashing, setup guides
```

See the [project plan](.claude/plans/i-have-this-esp32-gentle-shore.md) for the
full design and roadmap.

## Roadmap

- ESP32-S3 target (USB-OTG) → potential direct-over-USB mainboard flashing.
- SWD auto-flash from the ESP32.
- Additional mainboards via JSON board profiles.

## License

TBD.
