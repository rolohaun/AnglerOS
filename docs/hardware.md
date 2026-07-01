# Hardware & wiring

## Boards

- **ESP32-CAM** (AI-Thinker / Aideepen) — plain ESP32-LX6, 4MB flash, PSRAM,
  OV3660 camera, onboard white flash LED. Programmed via the bundled **CH340**
  USB-serial adapter (Micro-USB), which also powers it during setup.
- **BigTreeTech SKR Pico** — RP2040 mainboard running Marlin. Flashed via
  BOOTSEL (drag-and-drop `.uf2`).

## ESP32-CAM pin plan (camera ON, SD card unused)

The camera occupies most GPIOs. We **do not use the microSD card**, which frees
the SD-data pins for the UART link to the SKR Pico.

| Function | ESP32-CAM GPIO | Notes |
|----------|----------------|-------|
| Camera (OV3660) | standard AI-Thinker camera bus | unchanged |
| Flash LED | GPIO 4 | onboard white LED, PWM brightness |
| UART TX → SKR Pico RX | *TBD* | pick from freed SD pins |
| UART RX ← SKR Pico TX | *TBD* | pick from freed SD pins |
| GND | GND | common ground with SKR Pico |

> ⚠️ **Strapping-pin caution:** GPIO 12, 15, and 2 are boot strapping pins.
> Holding one at the wrong level at reset can prevent boot or select the wrong
> flash voltage. The final TX/RX pair will be chosen and documented here during
> Phase 3 (UART bridge) to avoid this.

Logic levels are 3.3V on both sides — no level shifter needed. Do **not**
back-power the SKR Pico from the ESP32; power each board from its own supply and
share only GND + the two UART lines.

## Flashing AnglerOS onto the ESP32

Use the [browser flasher](../pages/) (Chrome/Edge). The Aideepen CAM-MB adapter
supports auto-download (DTR/RTS auto-reset). If your adapter lacks auto-reset,
hold **IO0 → GND** while connecting to enter the ROM bootloader.

## Flashing Marlin onto the SKR Pico

1. AnglerOS generates the firmware and provides a `firmware.uf2`.
2. Hold **BOOT** on the SKR Pico, plug in USB (or press RESET while holding
   BOOT). It mounts as a drive named `RPI-RP2`.
3. Copy `firmware.uf2` onto that drive. The board reboots into Marlin.
