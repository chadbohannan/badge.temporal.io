# The Badge Device

The Replay 2026 Badge is a card-sized ESP32-S3 wearable: a two-board assembly (see the [hardware package](hardware-package.md) for the KiCad/fab side of that split) built around an OLED display, an 8x8 LED matrix, physical controls, an IMU, IR, haptics, and a rechargeable battery. `docs/hardware.html` is the public-facing version of this spec table; this page adds the capacity and connector detail that page omits.

## Compute and memory

The MCU is an ESP32-S3-WROOM-1 16N8 module: dual-core Xtensa at 240 MHz (`board_build.f_cpu` in `firmware/platformio.ini`), 16 MB of flash, and 8 MB of PSRAM. The embedded MicroPython runtime described on the [MicroPython bridge](micropython-bridge.md) page draws a 2 MB Python heap from that PSRAM, leaving the rest for the C++ application, the embedded [data bundle](data-bundle.md), and framebuffers. Core 0 and Core 1 split IR and application work as described on the [firmware system page](../systems/replay2026-firmware.md).

## Display and LED matrix

The primary display is a 128x64 monochrome OLED, SSD1309-compatible, driven through `oled_println()`/`oled_show()`. Below it sits an 8x8 red LED matrix on an IS31FL3731 driver with per-pixel PWM (`led_set_pixel()`, `led_show_image()`) — the matrix's single ambient owner and override contract are covered on the [LEDAppRuntime](led-app-runtime.md) page. An LIS2DH12 3-axis accelerometer drives "auto nametag mode": when the IMU detects the badge hanging upside down, firmware flips display, matrix, and input orientation together rather than leaving the wearer to read an inverted screen.

## Input

Four d-pad buttons and one analog joystick make up the physical controls. The joystick is read as raw ADC (`joy_x()`/`joy_y()`, 0-4095, center near 2048) rather than a debounced direction, so app code does its own deadzone handling. Buttons are addressable two ways: physical position (`0`-`3`) for games, or PlayStation-style aliases (`BTN_TRIANGLE`, `BTN_CROSS`, `BTN_SQUARE`, `BTN_CIRCLE`) and semantic roles (`BTN_CONFIRM`, `BTN_BACK`) for menus — the semantic pair is the one that respects a user's confirm/back swap preference, so menu code should prefer it over a hardcoded physical button. The `hardware/cad/` STEP references (`Temporal-Thumbstick-v3/v4/v5.step`) are successive mechanical iterations of the joystick's physical cap, not electrically distinct parts.

## IR and haptics

An NEC-protocol IR LED and TSOP receiver (`ir_start()`, `ir_send_words()`) give the badge line-of-sight badge-to-badge communication — the transport underneath [Boops contact exchange](badge-boops.md). A vibration motor with coil-tone support (`haptic_pulse()`, `tone()`) is the only other physical-output surface besides the display and matrix.

## Power and charging

`hardware/pcb/PCB-A` carries a BQ24079 USB-friendly Li-Ion charger with power-path management, fed from the board's USB-C 2.0 connector (`J1`) and a 2-pin JST PH connector (`J3`) to the battery pack — the same USB-C port used for flashing and serial console. Battery percentage isn't read as a naive resistor-divider voltage: `firmware/src/hardware/battery/BatteryAlgorithm.h` runs median-of-N ADC sampling through an integer IIR filter and an interpolated discharge curve, then gates the displayed percent so it never drops below what the instantaneous curve sample says (avoiding a false-0% flicker while still decaying quickly past true empty). Because the resistor divider sits on the charger-managed node while VBUS is present, the algorithm can't trust a continuous read for state-of-charge while charging — the port-side adapter (`Power.cpp`) briefly drives `CE_PIN` high to disable the BQ24079 for a probe burst, takes `BAT_PROBE_SAMPLES` open-circuit reads, then re-enables charging and classifies the probe before trusting it.

`J5` (UART) and `J6`/`J2` (I2C, `J2` populated DNP) round out PCB-A's debug and expansion headers; a 24-pin 0.5 mm FPC connector (`U5`) carries signal between PCB-A and the PCB-B backplate/art board.

## Storage capacity

The 16 MB flash is split by a PlatformIO partition table rather than a fixed OS filesystem, and which table applies depends on the build environment described on the [firmware system page](../systems/replay2026-firmware.md):

| Partition | `replay2026` (`partitions_replay_16MB_doom.csv`) | `replay2026-expanded` (`partitions_replay_16MB_ver2.csv`) |
|---|---|---|
| `nvs` | 20 KB | 20 KB |
| `otadata` | 8 KB | 8 KB |
| `app0` / `app1` (OTA slots) | ~3.9 MB each | 4.5 MB each |
| `ffat` (FATFS) | 6 MB | ~7.1 MB |
| `coredump` | 64 KB | 64 KB |

The default `replay2026` table trades OTA-slot headroom for extra FATFS space — a legacy of once carrying the embedded `doom1.wad` shareware data, which the now-removed [Doom port](doom.md) needed but which no longer ships; `replay2026-expanded` gives back that headroom to the OTA slots and FATFS instead, and `BadgeOTA::isExpandedPartitionLayout` lets a single `firmware.bin` OTA image serve both layouts by detecting which one is mounted at runtime rather than needing separate release artifacts. How a given flash path (factory image vs. firmware-only vs. `badge_sync`) interacts with `nvs` vs. `ffat` vs. `app0`/`app1` is the subject of the [storage model](storage-model.md) page — this page only owns the partition sizes themselves.

## Accessories and form factor

The board outline follows the CR80 reference (standard ID-card dimensions), and the badge is worn on a lanyard through the two-board PCB-A/PCB-B sandwich — PCB-B is the backplate/art board, functioning as both a mechanical cover and a visual/branding layer over the main electronics, and can optionally be SMT-assembled with Wurth M2 standoffs per `hardware/fab/PCB-B/README.md`. WiFi connectivity is 2.4 GHz-only (a property of the ESP32-S3 radio itself, not a firmware restriction), which matters when a badge silently fails to see a venue's 5 GHz-only network — see the [firmware README's WiFi diagnostics](../../firmware/README.md) for the scan-based way to tell the two failure modes apart.
