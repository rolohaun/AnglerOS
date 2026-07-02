# Hardware & wiring

## Boards

- **ESP32-CAM** (AI-Thinker / Aideepen) - plain ESP32-LX6, 4MB flash, PSRAM,
  OV3660 camera, onboard white flash LED. Programmed via the bundled **CH340**
  USB-serial adapter (Micro-USB), which also powers it during setup. Printer
  link: **UART** (no USB host hardware on this chip).
- **ESP32-S3-CAM** (dual USB-C) - ESP32-S3-WROOM-1 **N16R8** (16MB flash, 8MB
  PSRAM), OV3660 camera, microSD slot. Two USB-C ports: one **CH340C UART**
  (programming, logs, Improv) and one **native USB-OTG**. Printer link:
  **USB-OTG host** - AnglerOS plugs straight into the printer's USB port and
  talks CDC-ACM serial, the same way OctoPrint does.
- **BigTreeTech SKR Pico** - RP2040 mainboard running Marlin. Flashed via
  BOOTSEL (drag-and-drop `.uf2`).

## ESP32-CAM pin plan (camera ON, SD + printer UART mode)

The camera occupies most GPIOs. AnglerOS mounts the microSD card in one-bit
SD_MMC mode and keeps a hardware UART available for the printer.

| Function | ESP32-CAM GPIO | Notes |
|----------|----------------|-------|
| Camera (OV3660) | standard AI-Thinker camera bus | unchanged |
| microSD | GPIO 14 / 15 / 2 | SD_MMC one-bit mode |
| Flash LED | GPIO 4 | PWM brightness controlled from the dashboard |
| UART TX to printer RX | **GPIO 12** | ESP32 `Serial1` TX |
| UART RX from printer TX | **GPIO 13** | ESP32 `Serial1` RX |
| GND | GND | common ground with the mainboard |

Default baud is **250000** (Marlin's default `BAUDRATE`). GPIO12 is an ESP32
boot strapping pin, so the printer RX input should not pull it high or low
during reset. Most printer UART RX pins are high-impedance inputs, which is the
expected wiring here.

## ESP32-CAM microSD storage mode

AnglerOS attempts to mount the ESP32-CAM microSD slot in one-bit SD_MMC mode at
boot. If a card is mounted, the System tab reports its capacity and usage.

On the AI-Thinker-style ESP32-CAM, SD_MMC one-bit mode uses GPIO14 as the SD
clock. Earlier AnglerOS builds used GPIO14, then GPIO4, for UART TX. Current
builds use GPIO12 for UART TX so SD storage and the printer serial bridge can
run together while the flash LED stays off.

Logic levels are 3.3V on both sides - no level shifter needed. Do **not**
back-power the mainboard from the ESP32; power each board from its own supply
and share only GND + the two UART lines.

> **Printer side:** the ESP32 must connect to a UART that Marlin exposes as a
> serial port at the matching baud. On the SKR Pico use the board's serial/TFT
> header; the Marlin config's `SERIAL_PORT` (or a `SERIAL_PORT_2`) must map to
> it. A future AnglerOS config option will set this up automatically.

## ESP32-S3-CAM: USB-OTG printer link (OctoPrint-style)

On the S3-CAM the printer connection is a USB cable, no wiring:

1. Flash AnglerOS via the **UART/CH340 USB-C port** and set up Wi-Fi as usual.
2. Connect the **other USB-C port (OTG)** to the printer mainboard's USB port.
3. Power the S3-CAM through the CH340 port or its 5V pin (this also supplies
   VBUS to the OTG port so the printer detects a host).
4. Marlin must use its **native USB serial**: `SERIAL_PORT = -1`. AnglerOS's
   generated SKR Pico config already sets this.

The dashboard's link indicator shows `usb` once the printer enumerates. If a
USB-C-to-USB-C cable does not enumerate (some boards omit the CC resistors for
host role), use a **USB-C OTG adapter + USB-A cable** instead.

Board pin map (for reference): camera XCLK=15 SIOD=4 SIOC=5 VSYNC=6 HREF=7
PCLK=13 Y2..Y9=11,9,8,10,12,18,17,16; microSD (1-bit) CLK=39 CMD=38 D0=40.
The printer UART is disabled on this board - USB-OTG is the link.

## Flashing AnglerOS onto the ESP32

Use the [browser flasher](../pages/) (Chrome/Edge). The Aideepen CAM-MB adapter
supports auto-download (DTR/RTS auto-reset). If your adapter lacks auto-reset,
hold **IO0 to GND** while connecting to enter the ROM bootloader.

**ESP32-S3-CAM:** connect the **UART/CH340 USB-C port** to your PC (not the OTG
port) and pick "ESP32-S3-CAM" in the board list. If flashing doesn't start,
hold the **BOOT** button while plugging in.

## Flashing Marlin onto the SKR Pico

1. AnglerOS generates the firmware and provides a `firmware.uf2`.
2. Hold **BOOT** on the SKR Pico, plug in USB (or press RESET while holding
   BOOT). It mounts as a drive named `RPI-RP2`.
3. Copy `firmware.uf2` onto that drive. The board reboots into Marlin.
