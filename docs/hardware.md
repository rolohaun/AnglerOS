# Hardware & wiring

## Boards

- **ESP32-CAM** (AI-Thinker / Aideepen) - plain ESP32-LX6, 4MB flash, PSRAM,
  OV3660 camera, onboard white flash LED. Programmed via the bundled **CH340**
  USB-serial adapter (Micro-USB), which also powers it during setup.
- **BigTreeTech SKR Pico** - RP2040 mainboard running Marlin. Flashed via
  BOOTSEL (drag-and-drop `.uf2`).

## ESP32-CAM pin plan (camera ON, SD + printer UART mode)

The camera occupies most GPIOs. AnglerOS mounts the microSD card in one-bit
SD_MMC mode and keeps a hardware UART available for the printer.

| Function | ESP32-CAM GPIO | Notes |
|----------|----------------|-------|
| Camera (OV3660) | standard AI-Thinker camera bus | unchanged |
| microSD | GPIO 14 / 15 / 2 | SD_MMC one-bit mode |
| UART TX to printer RX | **GPIO 4** | ESP32 `Serial1` TX; shared with onboard flash LED |
| UART RX from printer TX | **GPIO 13** | ESP32 `Serial1` RX |
| GND | GND | common ground with the mainboard |

Default baud is **250000** (Marlin's default `BAUDRATE`). GPIO4 is also the
ESP32-CAM onboard flash LED, so UART traffic may flicker the LED and AnglerOS
should not use the flash LED while printer UART is enabled.

## ESP32-CAM microSD storage mode

AnglerOS attempts to mount the ESP32-CAM microSD slot in one-bit SD_MMC mode at
boot. If a card is mounted, the System tab reports its capacity and usage.

On the AI-Thinker-style ESP32-CAM, SD_MMC one-bit mode uses GPIO14 as the SD
clock. Earlier AnglerOS builds used GPIO14 for UART TX; current builds use GPIO4
for UART TX so SD storage and the printer serial bridge can run together.

Logic levels are 3.3V on both sides - no level shifter needed. Do **not**
back-power the mainboard from the ESP32; power each board from its own supply
and share only GND + the two UART lines.

> **Printer side:** the ESP32 must connect to a UART that Marlin exposes as a
> serial port at the matching baud. On the SKR Pico use the board's serial/TFT
> header; the Marlin config's `SERIAL_PORT` (or a `SERIAL_PORT_2`) must map to
> it. A future AnglerOS config option will set this up automatically.

## Flashing AnglerOS onto the ESP32

Use the [browser flasher](../pages/) (Chrome/Edge). The Aideepen CAM-MB adapter
supports auto-download (DTR/RTS auto-reset). If your adapter lacks auto-reset,
hold **IO0 to GND** while connecting to enter the ROM bootloader.

## Flashing Marlin onto the SKR Pico

1. AnglerOS generates the firmware and provides a `firmware.uf2`.
2. Hold **BOOT** on the SKR Pico, plug in USB (or press RESET while holding
   BOOT). It mounts as a drive named `RPI-RP2`.
3. Copy `firmware.uf2` onto that drive. The board reboots into Marlin.
