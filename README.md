# AnglerOS

**A Mainsail-style web interface, self-hosted on a LILYGO T-Dongle-S3, for
controlling a printer and configuring and building Marlin firmware.**

AnglerOS serves a movable three-column dashboard, stores and streams G-code,
bridges commands to Marlin over USB-OTG or UART, and turns Marlin's large
configuration surface into a focused browser workflow.

> Status: **early development.** The AnglerOS controller target is the
> **LILYGO T-Dongle-S3**. The first printer-board profile is BigTreeTech SKR
> Pico (RP2040).

## Current features

- Mainsail-inspired dashboard with movable cards, temperatures, jog controls,
  macros, console, and print controls.
- Fast direct-to-card G-code uploads through the T-Dongle's 4-bit SDMMC slot.
- Direct upload and print from OrcaSlicer through its Octo/Klipper host type.
- USB-OTG CDC host and QWIIC UART printer links.
- Onboard APA102 printer-light brightness control.
- Onboard 160x80 status screen with IP address, temperatures, active command,
  filename, printer link, and print progress.
- System view with ESP32-S3 CPU, memory, network, and storage metrics.
- Browser-based AnglerOS installer and GitHub Actions Marlin builds.

## Hardware

| Part | Notes |
|------|-------|
| LILYGO T-Dongle-S3 | ESP32-S3, 16MB flash, no PSRAM, TF slot, USB-OTG, APA102 |
| Printer mainboard | Marlin controller; SKR Pico is the first included profile |
| Printer link | USB-OTG CDC or 3.3V QWIIC UART |

See [hardware and wiring](docs/hardware.md) for the exact SDMMC, UART, USB, and
LED details.

See [OrcaSlicer setup](docs/orcaslicer.md) to send sliced G-code straight to
AnglerOS over the local network.

## Repository layout

```text
firmware/          PlatformIO project for the LILYGO T-Dongle-S3
  data/            LittleFS web UI and printer-board profiles
.github/workflows/ AnglerOS firmware and Marlin build automation
pages/             GitHub Pages browser flasher
scripts/           Build helpers and flash-image merging
docs/              Wiring, flashing, and setup guides
```

See the [project plan](.claude/plans/i-have-this-esp32-gentle-shore.md) for the
full design and roadmap.

## License

TBD.
