# Hardware and wiring

AnglerOS supports one controller: the **LILYGO T-Dongle-S3**. It has an
ESP32-S3, 16MB QSPI flash, no PSRAM, a 160x80 ST7735 display, an APA102 status
LED, native USB-OTG, and a TF card slot connected by 4-bit SDMMC.

The printer mainboard is separate. The first generated Marlin profile targets
the **BigTreeTech SKR Pico**, but other printer-board profiles can still be
added without changing the AnglerOS hardware target.

## TF card: 4-bit SDMMC

AnglerOS mounts the T-Dongle TF card at SDMMC high speed with all four data
lines. G-code uploads stream directly to the card, so files are not limited by
the dongle's 16MB flash.

| Signal | GPIO |
|--------|------|
| SDMMC CLK | 12 |
| SDMMC CMD | 16 |
| SDMMC D0 | 14 |
| SDMMC D1 | 17 |
| SDMMC D2 | 21 |
| SDMMC D3 | 18 |

The System tab reports card type, mount mode, capacity, and used space.

## Printer links

AnglerOS starts both links and prefers USB whenever a CDC-ACM printer is
attached. The dashboard reports `usb`, `uart`, or `none` for the active link.

### QWIIC UART

UART is the simplest setup because it leaves the dongle's USB-A plug available
for power. Logic is 3.3V. Cross TX and RX, share ground, and do not connect a 5V
pin between boards.

| T-Dongle-S3 | Printer UART |
|-------------|--------------|
| GPIO 43 (TX) | RX |
| GPIO 44 (RX) | TX |
| GND | GND |

The default baud rate is **250000**. For SKR Pico, the generated Marlin config
uses `SERIAL_PORT_2 = 0`, which exposes UART0 on the TFT header alongside native
USB serial.

### USB-OTG host

The T-Dongle has only one USB connector. In normal AnglerOS operation it is
configured as a USB host and can talk to a Marlin USB CDC port. Unlike the old
dual-port controller, it cannot simultaneously use that connector for PC
serial setup.

The USB-A connector is male, so connecting it to a printer normally requires a
USB-A female OTG adapter or coupler plus the printer's cable. The dongle must
also receive 5V power while acting as host. Verify the adapter/power arrangement
does not back-feed the printer before connecting it.

## Onboard light

The dashboard Printer Light card drives the onboard APA102 as white with
adjustable brightness. Data is GPIO 40, clock is GPIO 39, and the light boots
off.

## Flashing AnglerOS

Use the [browser flasher](../pages/) in Chrome or Edge. Hold **BOOT** while
plugging the T-Dongle into the PC if it does not enter download mode. After
flashing, power-cycle without holding BOOT.

For first-time Wi-Fi setup, join **AnglerOS-Setup** and open
`http://192.168.4.1`. USB serial provisioning is intentionally disabled because
the same USB controller is reserved for the printer host.

## Flashing Marlin onto SKR Pico

1. Generate or download `firmware.uf2` from AnglerOS.
2. Hold **BOOT** on the SKR Pico while connecting USB. It mounts as `RPI-RP2`.
3. Copy `firmware.uf2` to that drive. The board reboots into Marlin.
