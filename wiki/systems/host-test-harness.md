# Host Test Harness

Scoped 2026-09-28 at user request, on the `matrix_rework` branch. The harness is a single host (Linux/SDL) build that runs the real firmware — [`GUIManager`](../components/gui-screen-stack.md), every screen, [`LEDAppRuntime`](../components/led-app-runtime.md), and (from Phase 2) the [MicroPython bridge](../components/micropython-bridge.md) — with hardware replaced at the lowest layer that has a clean seam. All three phases were built on 2026-09-29 and live in `firmware/host/` (its `README.md` has the commands). The claims below were checked against the `matrix_rework` working tree on 2026-09-28, and the ones Phase 1 changed were corrected on 2026-09-29; see [What runs today](#what-runs-today).

## Why not QEMU

A QEMU full-device emulator (display, LED matrix, input) was scoped first and rejected as too heavy. No QEMU device model exists for either the OLED or the LED-matrix driver. Espressif's own QEMU fork documents I2C peripheral support poorly. QEMU has no plugin mechanism for devices, so every custom peripheral means patching and permanently maintaining a QEMU fork.

## The problem: two one-off harnesses, not a shared one

This section describes the situation before Phase 3, which removed the two per-game viewers; see [Phase 3](#phase-3). The paragraph below is the motivation.

[PACKIT](../components/packit.md) and [Helgrind](../components/helgrind.md) each ship their own standalone host build (`firmware/host/packit/`, `firmware/host/helgrind/`), with separate `Makefile`s, separate SDL `window.cpp`/`display.{h,cpp}` glue (~180–260 lines each), and separate hardcoded key maps. Each compiles exactly one game's core `.cpp` and links U8g2's C library straight against a memory buffer, bypassing the firmware's `oled` class. The U8g2 source comes from `firmware/.pio/libdeps/replay2026/U8g2/src`, so a device `pio run` must happen first. That is cheap per game but does not scale, and it never exercises `GUIManager` navigation, `LEDAppRuntime`, or MicroPython — the pieces most likely to have integration bugs.

## Design principle: stub the silicon, not the classes

Host reimplementations of `oled`, `LEDmatrix`, `Inputs`, or `Filesystem` would duplicate logic and test the mock instead of the firmware. For example, the two-frame double-buffering that `LEDAppRuntime` depends on is implemented in `firmware/src/hardware/LEDmatrix.cpp` (`beginFrameBatch()`/`endFrameBatch()`). So every badge-owned `.cpp` stays real, and the harness replaces only what sits below it.

**Peripheral selection.** `firmware/src/hardware/HardwareConfig.h` already picks a per-target header from a `-DHARDWARE_*` flag; the `replay2026` environment sets `-DHARDWARE_ECHO`, which includes `EchoDefines.h`. That header `#define`s the peripheral flags (`BADGE_HAS_IMU`, `BADGE_HAS_LED_MATRIX`, `BADGE_HAS_HAPTICS`, `BADGE_HAS_BATTERY_GAUGE`, `BADGE_HAS_SLEEP_SERVICE`). They are not `-D` build flags, so they cannot be dropped from the command line. The host build adds `-DHARDWARE_HOST` and a `HostDefines.h` with the same pins and sizes, which decides which peripherals exist on the host. It keeps `BADGE_HAS_LED_MATRIX` and leaves out the IMU, haptics, battery gauge, and sleep service, so their existing no-hardware code paths run. It also leaves out `BADGE_ECHO`, which selects the ADC battery gauge and the `DiagnosticsScreen` code that reads its raw values. One of those no-hardware paths did not compile: the stub `Haptics` namespace in `Haptics.h` lacked `kDefaultPwmFreqHz`, which `DrawScreen.cpp` uses. Phase 1 added the constant to the stub.

- **OLED.** `oled` (`firmware/src/hardware/oled.h`) owns a `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` member, so it needs U8g2's C++ layer (`U8g2lib.cpp`/`U8x8lib.cpp`). Those compile unchanged over an inert `Wire`, so U8g2's own byte callbacks run and move no data. The harness reads the frame buffer through `oled::getBufferPtr()` at yield points, for the SDL window and snapshots. `oled.cpp` runs unchanged. The panel is the buffer, not the chip, so contrast, invert, and power-save commands do not show.
- **LED matrix.** `LEDmatrix` drives the chip through an `Adafruit_IS31FL3731 driver_` member and calls only `begin`, `clear`, `setFrame`, `drawPixel`, and `displayFrame`. A host `Adafruit_IS31FL3731.h` implementing those five methods over an in-memory 8-frame buffer reproduces the chip's frame registers; the SDL panel draws whichever frame `displayFrame` selected. The real `LEDmatrix.cpp` double-buffer logic is then under test. The no-matrix stub class in `LEDmatrix.h` is not used: it never ships on a real badge.
- **Inputs.** `firmware/src/hardware/Inputs.cpp` reads the joystick through `analogRead` (`analogReadMedian3`). It reads the buttons through `digitalRead(kButtonPins[i]) == LOW`, but only for pins whose interrupt has fired. The shim drives both through a virtual pin model, described under [input virtualization](#input-virtualization), so the real interrupt handling, debounce, key repeat, smoothing, deadzone, and axis rotation all run.
- **Filesystem.** `Filesystem` (`firmware/src/infra/Filesystem.{h,cpp}`) is not a call-site-transparent seam: about 11 other files — including `ota/AssetRegistry.cpp`, `micropython/StartupFiles.cpp`, `screens/draw/AnimDoc.cpp`, `screens/SettingsScreen.cpp`, and `api/DataCache.cpp` — call oofatfs `f_*` functions directly on the `FATFS*` from `replay_get_fatfs()`. The seam is one layer lower: `firmware/lib/micropython_embed/src/port/replay_bdev.c`, whose `bdev_readblocks`/`bdev_writeblocks` call ESP-IDF wear-levelling (`wl_read`/`wl_write`/`wl_erase_range`) on an `esp_partition`. The host runs the real `replay_bdev.c` unchanged over a shimmed `esp_partition`/`wear_levelling` API (`host/shim/flash_shim.cpp`): a RAM image of the 6 MB `ffat` partition with 4096-byte sectors, erased to `0xFF`. The firmware's own mount-and-format logic, MicroPython's VFS, and every direct `f_*` caller then behave as on the badge, and `mpy_start()` mounts the volume during `setup()` as it does on a badge. Phase 1 had its own RAM disk under oofatfs; Phase 2 replaced it because MicroPython's `vfs_fat_diskio.c` owns the `disk_*` functions. The firmware's `provisionStartupFiles()` writes the baked app files into the volume. oofatfs's `ff.c` and `ffunicode.c` compile straight from `lib/micropython_embed/src/lib/oofatfs/`. `--fs-dir SRC[:/DEST]` copies more files in after boot, as `uploadfs` does (for example `initial_filesystem`, or a Community App to `/apps/<slug>`), and then calls `rebuildMainMenuFromRegistry()`, which is what `badge.rescan_apps()` calls. `--state-dir` keeps the image and the settings between runs.
- **BLE** is already compiled out: `ble/` is excluded by `build_src_filter` in `firmware/platformio.ini`, and its call sites sit behind `#ifdef BADGE_ENABLE_BLE_PROXIMITY`, the build-flag seam [BLE Room Presence](../components/ble-proximity.md) documents for the public build.

**Radios are the exception to the rule.** Stub the silicon for what is under test; replace whole classes for what is not. WiFi, IR, OTA, and HTTP are not under test, and stubbing them at the silicon level would mean faking a large ESP-IDF surface (RMT, netif, event loop, TLS clients, `Update`) to exercise code nobody runs on the host. So the `[env:host]` `build_src_filter` drops these files and links small host replacements with the same headers:

- `api/WiFiService.cpp` → never connected, `status()` reports disconnected.
- `ota/OTAHttp.cpp` (`getJson`, `resolveRedirect`) → every request fails with a network error. `ota/AssetRegistry.cpp` stays real above it, so the Apps and registry screens run their offline paths.
- `ota/BadgeOTA.cpp` → "no update available". It is the only user of `Update.h`.
- `ir/BadgeIR.cpp` plus `hw/ir/nec_rx.c`/`nec_tx.c` → IR idle. `BadgeIR.h` still includes the pure `nec_mw_{encoder,decoder}.h` headers, so they stay on the include path.
- `micropython/badge_mp_api/mp_api_http.cpp` → `OSError`, in Phase 2. It is the only direct `HTTPClient`/`WiFiClientSecure` user outside `ota/`.
- `micropython/` builds whole from Phase 2 on. `mp_api_http.cpp` is replaced by `host/replacements/mp_api_http_host.cpp`, which returns the same JSON error shape as the real one when WiFi is down.

`api/TlsGate.cpp` only needs FreeRTOS and heap calls, so it stays real.

## The platform shim is the real cost

Replacing a `.cpp` at link time only works if its headers compile on the host, and they do not today. 86 files include `<Arduino.h>`. FreeRTOS headers appear in screens, UI, and MicroPython code, and `Filesystem::IOLock` is itself a FreeRTOS lock. So the fixed cost is one shared shim. This inventory was taken by grepping `#include`s across `firmware/src` (excluding `ble/`) on 2026-09-28. It is for the files that stay real after the radio replacements above:

- **Arduino core:** `millis`/`micros`/`delay`/`yield`/`String`/`Serial`/`Print`/`Stream`, `pinMode`/`digitalRead`/`digitalWrite`/`analogRead`, `attachInterrupt`/`detachInterrupt`/`digitalPinToInterrupt`/`noInterrupts`/`interrupts` (all backed by the virtual pin model below), `Wire` and `SPI` (inert; U8g2's `U8x8lib.cpp` includes both), `setCpuFrequencyMhz` (`esp32-hal-cpu.h`, used by `Power.cpp`), and the `ESP.` object (5 files). Attribute and allocator macros `PROGMEM` (12 files), `IRAM_ATTR`, and `ps_malloc` (7 files, → `malloc`) also belong here. `Serial` and `ESP_LOG*` go to stdout.
- **FreeRTOS:** semaphores and mutexes, queues (`freertos/queue.h`), `vTaskDelay`, `portmacro.h`, and task creation. With the radio classes replaced, the remaining `xTaskCreatePinnedToCore` callers are `irTask` in `main.cpp` and the registry fetch in `ota/AssetRegistry.cpp`. The shim makes task creation a no-op that reports success, which keeps the process single-threaded.
- **ESP-IDF subset:** `heap_caps_*` (`heap_caps_malloc` is plain `malloc`, and `heap_caps_get_largest_free_block` returns `SIZE_MAX`; the MicroPython heap budget below sets the real limit), `esp_ptr_external_ram`, `esp_random`/`esp_fill_random`, `esp_timer`, `esp_sleep`, `esp_pm`, `esp_bt` (`esp_bt_mem_release` in `Power.cpp`, outside `ble/`), `esp_wifi` (included by `Power.cpp`), `esp_mac` (`esp_efuse_mac_get_default` in `identity/BadgeUID.cpp`, returning a fixed host MAC), `esp_hmac`, `esp_netif`/`esp_event` (`esp_netif_init`/`esp_event_loop_create_default` in `main.cpp`), `esp_check`/`esp_err`, `esp_ota_ops`, `esp_flash`, `esp_partition`, `esp_rom_crc`, `esp_system`, `driver/gpio`, `driver/rtc_io`, and `driver/mcpwm_prelude`. `Preferences`, raw `nvs.h`, and `nvs_flash` all run over one host key-value store, which is a file only when `--state-dir` is set. `esp_restart()` exits with a distinct status code, so a script can check that a reboot happened.
- **Register access:** `hardware/Power.cpp` reads and writes chip registers directly. It uses `REG_READ`/`REG_WRITE` on `RTC_CNTL_BROWN_OUT_REG` (the brown-out save/restore) and `REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG)` (USB-host detection). On the host those are absolute addresses and segfault. The shim provides `soc/rtc_cntl_reg.h` and `soc/usb_serial_jtag_reg.h`, and defines the `REG_*` macros over a small in-memory register array keyed by address. Grep `firmware/src` for `REG_READ|REG_WRITE|_PERI_REG` when new code lands. A new raw register access crashes the host build until the array gains that register.
- **mbedtls:** `identity/BadgeUID.cpp` (SHA-1, with an `MBEDTLS_VERSION_NUMBER` check that already handles mbedtls 2 and 3) and `ota/AssetRegistry.cpp` (SHA-256) use it. The plan was to link the system `libmbedtls`, but its headers were not installed on the machine that built Phase 1, so `host/shim/mbedtls_shim.cpp` has small SHA-1 and SHA-256 implementations instead. That is about 120 lines and keeps the build free of a system package.

Linking the real `GUIManager` means linking nearly the whole firmware: `firmware/src/ui/GUI.cpp` includes about 33 screen headers plus WiFi, OTA, `AssetRegistry`, IR, and Boops. So the first milestone is "the whole firmware with the radio classes replaced", not "`GUIManager` plus one screen". The shim is written once, and the hardware stubs above are a few extra functions in it.

The harness `main()` calls the firmware's `setup()` once and then `loop()` repeatedly. `loop()` in `firmware/src/main.cpp` is single-threaded: `scheduler.runOnce()` over the cooperative `Scheduler`, then `guiManager.handleInputIfActive()`. The shim does not need a scheduler of its own.

## Build

The harness is a PlatformIO `native` environment, `[env:host]` in `firmware/platformio.ini`, not another hand-written `Makefile`. It reuses the shared flags and the same `extra_scripts` (`inject_version.py`, `generate_startup_files.py`), adds `-DHARDWARE_HOST`, the shim include path, and SDL2 through `firmware/scripts/host_flags.py`, and fetches U8g2 and ArduinoJson through `lib_deps`. This removes the dependency on a device build having populated `.pio/libdeps/`. Four settings were not in the plan:

- `lib_compat_mode = off`. U8g2 is an Arduino library, and with no framework on the native platform PlatformIO skips it as incompatible.
- `lib_ignore = micropython_embed`. The port's ESP32 sources do not build on the host, and Phase 1 needs only oofatfs from it, through `build_src_filter`.
- C files build as `-std=gnu11`. GCC 15 defaults C to C23, where `qrcode.h`'s own `typedef unsigned char bool` is an error.
- `data/out/bundle.bin` is embedded with `.incbin` in `host/replacements/bundle_embed.cpp`, which defines the two symbols `DataCache.cpp` reads. `board_build.embed_files` does not exist on the native platform.

The host build also runs with AddressSanitizer and UndefinedBehaviorSanitizer. That turns memory errors in screens and `LEDAppRuntime` into a non-zero exit, which is the cheapest correctness check a host build gives.

## Clock and yield point

The shim owns `millis()`. In scripted mode it is a virtual clock, so "press A, wait 500 ms" behaves the same on every run.

The clock cannot advance once per `loop()` frame. Several loops block inside one frame, waiting on `millis()`: `mpy_hal_delay_ms` (`micropython/MicroPythonBridge.cpp`), `bootAnimationDelay` (`ui/BadgeDisplay.cpp`), the wake-arm wait in `hardware/Power.cpp`, and the WiFi connect waits. If `millis()` only moved between frames, these loops would never end. All of them call `delay()` in their bodies.

So `delay()` (and `vTaskDelay`/`yield`, which map to it) is the harness's yield point. Each call:

1. advances the virtual clock by the requested time (live mode sleeps instead),
2. applies any scripted input that is now due, or polls SDL events in live mode,
3. presents the OLED and matrix buffers if a frame is due.

This matters most for MicroPython. A running app blocks inside `mp_embed_exec` and only returns to C through `mpy_hal_delay_ms` → `delay(1)`. If input were read only in the harness's own frame loop, MicroPython apps would receive none. The main loop also passes through the yield point once per frame: `loop()` ends in `Power::applyLoopPacing()`, which is `delay(Policy::loopDelayMs)`.

**Progress guarantee.** If time only moved inside `delay()`, any loop that spins on the clock without calling `delay()` would hang forever. Two such loops exist:

- `Power.cpp`'s charger-probe batch busy-waits on `micros()` on purpose. Its comment says not to turn it into a yield.
- `Scheduler::runFor` (`infra/Scheduler.cpp`) is a `do … while ((millis() - start) < budgetMs)` loop.

Neither runs on the host today. The probe is under `BADGE_HAS_BATTERY_GAUGE`, which `HostDefines.h` leaves out, and `runFor` has no callers. That is luck, not design. So `millis()` and `micros()` read one virtual clock kept in microseconds, and every read advances it by 1 µs. Any clock-polling spin then ends in bounded virtual time, and a run with the same script stays repeatable. `delay()` still does the large jumps and remains the only place input is applied and frames are presented.

**Wall-clock watchdog.** A script timeout measured on the virtual clock cannot catch a spin that never reads the clock, such as `while (!flag) {}` on a flag that a no-op task would have set. The harness therefore also arms a real-time watchdog (for example, `alarm()` with a `SIGALRM` handler that prints the last script step and exits). Its budget scales with the script's virtual length.

## Input virtualization

The badge has one analog joystick and four face buttons. The harness drives them as virtual pins, not as calls into `Inputs`, so the whole input path is the one that runs on the badge.

**Pin model.** The shim keeps one record per GPIO: mode, digital level, analog value, and an attached ISR with its trigger (`CHANGE`, `FALLING`, `RISING`). Digital level and analog value are stored separately. `EchoDefines.h` gives GPIO 13 to both `JOY_X` and `INT_PWR_PIN`, so a single value per pin would let joystick movement read as a power interrupt if `HostDefines.h` copies those numbers. `HostDefines.h` also leaves out pins whose code is behind `#if defined(...)`, such as `CHG_GOOD_PIN`/`CHG_STAT_PIN` in `Power.cpp`. Those paths then compile away and need no chosen level.

**Defaults.** `pinMode(pin, INPUT_PULLUP)` sets the level to `HIGH`, just as the real pull-up does, and a pin the harness has driven keeps that level through later `pinMode` calls. That alone was not enough. `PanicReset::begin()` arms a 50 ms poll of all four button GPIOs before `Inputs::begin()` runs, so the pins are read while still unconfigured. With a default level of `LOW` the poll saw all four buttons held, and after 2 s of virtual time it rebooted the run. On the badge the board holds those pins high. `inputInit()` therefore drives every button released before `setup()`, and centres the stick at analog 2047. Reads are exact and have no noise, so `analogReadMedian3` sees three equal samples.

**Buttons need the interrupt, not just the level.** `Inputs::service()` calls `refreshButtons()` only for buttons flagged in `pendingIrqMask_`. The four `onButton*ISR` handlers set those flags, and `attachInterrupt(..., CHANGE)` registers the handlers in `Inputs::begin()`/`resumeInterrupts()`. If a harness `digitalRead` returned the new level with no ISR call, it would never be seen. The single entry point `host_pin_set(pin, level)` therefore updates the level and then calls the attached ISR when the trigger matches. It runs only at the `delay()` yield point, so ISRs never run concurrently, and `noInterrupts`/`interrupts` can be no-ops. `detachInterrupt` clears the ISR, so `Inputs::suspendInterrupts()` really suppresses input, just as it does on the badge.

**Debounce limits tap length.** `refreshButtons()` ignores an edge that comes within `debounceMs_` (20 ms by default) of the last accepted edge. It has already cleared the pending flag, so nothing samples that edge again. On the badge, contact bounce produces later interrupts that pick up the change. The host has no bounce, so a release less than 20 ms after a press is lost, and the button stays pressed until its next edge. Scripted taps therefore default to a hold well above the debounce time (for example, 60 ms), and the script parser rejects holds under `debounceMs_`.

**Joystick in screen space.** `Inputs::service()` rotates the board-mounted stick 90° (`joyX = 4095 - physicalY`, `joyY = physicalX`) and then applies the inverted-orientation flip. The injector takes a screen-space deflection from -1 to 1 on each axis and applies the inverse rotation once, in one function, before it writes `JOY_X`/`JOY_Y`. Scripts and key maps can then work in screen directions, and the firmware's own rotation and flip still run. The stick is sampled every `Power::Policy::joystickPollMs` and smoothed by `applyJoystickSensitivity()`, so a deflection takes several polls to settle. Scripts should hold the stick for a set time, not set it for a single instant.

**Live key map.** In the SDL window, the arrow keys give full joystick deflection, and holding Shift gives half deflection for deadzone and sensitivity checks. The face buttons sit on I/L/K/J, a diamond that matches the badge layout. Per `firmware/src/ui/ButtonGlyphs.h`, `Up` is Y, `Right` is B, `Down` is A, and `Left` is X. An SDL game controller, if one is connected, maps its left stick to the analog joystick and its face buttons to the same four by position. Both sources write through `host_pin_set` and the injector, the same path the script uses. This single map replaces the separate PACKIT and Helgrind key maps.

**Script vocabulary.** Buttons are named by their glyph letters (`Y`, `B`, `A`, `X`), as in the [UI conventions](../components/ui-conventions.md), not by pin names. The commands are:

- `press <btn>` and `release <btn>`.
- `tap <btn> [hold_ms]`.
- `stick <x> <y> <ms>`, a screen-space deflection held for a time and then released to centre; a time of 0 holds it until the next `stick`.
- `wait <ms>`.

## MicroPython

The `badge.*`/`matrix_app_*` surface in `firmware/src/micropython/badge_mp_api/` (13 `.cpp` files — `mp_api_led.cpp`, `mp_api_display.cpp`, `mp_api_input.cpp`, …) calls the same `oled`/`LEDmatrix`/`Inputs` globals, so it runs on the same stubs. The bridge hooks in `MicroPythonBridge.cpp` (`mpy_hal_stdout_write`, `mpy_hal_stdin_read`/`_available`, `mpy_hal_delay_ms`, `mpy_hal_ticks_ms`) map to stdio and the shim clock. `port/mphalport.c` needs `esp_fill_random`, and `port/embed_util.c` needs `heap_caps_get_largest_free_block` for `MICROPY_GC_SPLIT_HEAP_AUTO`; both come from the shim.

**Heap budget (built).** On the badge the Python heap used to start at 2 MB and grow into all free PSRAM, with no limit in the code, and a host shim cannot copy that: `malloc` has no "largest free block". So the limit moved into the port, and both targets run the same code. `REPLAY_MP_HEAP_BUDGET` in `mpconfigport.h` is the total Python heap including the initial slab. `gc_get_max_new_split()` in `embed_util.c` returns the smaller of the budget left and the largest free PSRAM block, and new areas are allocated PSRAM-only through `replay_mp_alloc_heap()`. The [MicroPython bridge](../components/micropython-bridge.md) page has the details and the two open items: the 4 MB value is a placeholder that needs a `HeapDiag` reading on a badge, and the change has not been run on one. On the host, `host/fixtures/heap_budget` fills the heap with 100 KB blocks until `MemoryError`. It stops at 40 blocks, and `gc.mem_free()` afterwards reports 4.16 MB, which is the free space plus the room left to grow.

The other option, turning off `MICROPY_GC_SPLIT_HEAP_AUTO` for one fixed slab of budget size, would reserve the whole budget at boot even when no app runs, so it was not taken.

Threads are not a concern. `firmware/lib/micropython_embed/src/mpconfigport.h` enables `MICROPY_PY_THREAD` only when `REPLAY_ENABLE_THREAD` is set, which defaults to 0 and is not set in `platformio.ini`. The `mp_thread_init`/`pxTaskGetStackStart` call in the bridge and the FreeRTOS `ulTaskNotifyTake` poll hook are therefore not compiled, and the poll hook already has a non-FreeRTOS fallback.

**ESP32 port modules (built).** `lib/micropython_embed/library.json` compiles `adc`, `machine_pin`, `machine_timer`, `machine_rtc`, `machine_touchpad`, `machine_hw_spi`, `network_common`, `network_wlan`, `network_ppp`, `modsocket` (lwIP), and `modespnow` from `ports/esp32/`. All call ESP-IDF directly, and the pregenerated `genhdr/moduledefs.h` and `genhdr/qstrdefs.generated.h` register them. Regenerating `genhdr` for a host configuration was not possible, because MicroPython's qstr and moduledef generator scripts are not in the repo. The host build therefore keeps `genhdr` and turns the modules off from the command line. `mpconfigport.h` already had a host-safe branch behind `REPLAY_ENABLE_ESP32_MACHINE_WIRELESS`, so `[env:host]` sets that and `REPLAY_ENABLE_FULL_NETWORK` and `REPLAY_ENABLE_ESPNOW` to 0, which drops `machine`, `network`, `socket`, `ssl`, and `espnow`. `moduledefs.h` still names the five module objects, so `host/replacements/port_stubs.c` defines them as stub modules: `import machine` succeeds and any name inside raises `NotImplementedError`. That file also defines `ff_memalloc`/`ff_memfree`, which ESP-IDF's FATFS component supplies on the badge. The extra qstrs in the table are harmless.

Four details of building MicroPython for the host:

- `-DMICROPY_NLR_SETJMP=1` keeps exception handling on `setjmp`, which AddressSanitizer understands.
- The collector scans the C stack conservatively, so `py/gc.c` and the gchelper file build with `-fno-sanitize=address` (`scripts/host_flags.py`), and the harness sets `detect_stack_use_after_return=0`. With it on, locals move to a fake stack and `stack_top` points somewhere unrelated.
- The C files get `-include stdlib.h -include alloca.h`, which the ESP32 toolchain's headers supply.
- `--repl` feeds stdin to `Serial` and runs the harness in real time, so a person or a delayed pipe can type into the REPL. The first `mpy_poll()` drains pending input, so anything sent before boot finishes is lost.

The on-badge tests in `firmware/initial_filesystem/micropython_tests/` and `firmware/data/micropython_tests/` are the first candidates to run off hardware; none are wired into `host/run-tests.sh` yet. Most only use the `badge` API, which the host has. Two import `machine`-family modules (`import_block_test.py` and `test_timers.py`) and would hit the stubs.

## Proposed phasing

Per the user's explicit call, stubs only need to be good enough to speed up testing new features on this branch; hardware-accurate fidelity is not a goal.

1. **Shim, OLED, matrix, and input (built 2026-09-29).** `[env:host]`, `HostDefines.h`, the Arduino/FreeRTOS/ESP-IDF shim, U8g2 no-op callbacks, host `Adafruit_IS31FL3731`, the virtual pin model with interrupt delivery, the joystick and button injector (SDL keyboard and game controller for live use, the same injector for scripts), FAT-image `replay_bdev.c`, the host radio-class replacements, the fake register array, and the `delay()` yield point with the per-read clock tick and wall-clock watchdog. The real firmware boots to `GUIManager`, with OLED navigation driven by the virtual joystick and buttons from the keyboard, a controller, or a script, and the real `LEDmatrix` double buffer and `LEDAppRuntime` live-preview paths running. The matrix panel was modelled on PACKIT's old window layout and PNG writer, which Phase 3 then deleted. Scripted-input mode (below) went in from the start. The matrix is in this phase because its stub is small and the branch exists for matrix work.
2. **MicroPython (built 2026-09-29).** The heap-budget firmware change first, then the ESP32 port-module handling above, against the Phase 1 shim. Folder apps and [Community Apps](../components/community-apps-registry.md) now run off hardware; the `starfield_nametag` community app launches from the main menu and animates.
3. **Retire the one-offs (built 2026-09-29).** PACKIT and Helgrind run as ordinary screens in the firmware, so the shared harness runs them; the `packit-screen` and `helgrind-screen` scripts open each with the `open <screen>` command, so they don't depend on the home grid's tile order, and check goldens. The SDL viewers, `make play`, and PACKIT's PNG code are deleted. `run-tests.sh` also runs the kept unit tests. Helgrind's snapshot code now shares the harness's PNG writer. Kept as fast, SDL-free checks: PACKIT's `test.cpp`, Helgrind's scripted runner (`make test`, game-state asserts and PNG frames), and its `check-world.py` (`make check`), none of which the harness's frame goldens can express.

## Scripted-input mode (for agentic dev loops)

An AI agent driving the harness needs to feed input sequences and check the resulting state without a human watching an SDL window. The harness `main()` takes a mode flag so one binary supports both a live SDL window and a scripted, headless run:

- **Scripted input:** a sequence of button/joystick actions and waits from a file or stdin, in the vocabulary under [input virtualization](#input-virtualization). It is timed on the virtual clock and applied at the `delay()` yield point through the same `host_pin_set` and joystick injector as live keys.
- **State dump:** OLED framebuffer and LED-matrix frame written to PNG or text on demand, reusing the capture approach in [`runbooks/capturing-oled-screenshots.md`](../runbooks/capturing-oled-screenshots.md) and PACKIT's `<prefix>-oled.png`/`<prefix>-matrix.png` snapshot pattern.
- **Assertions:** a script command that compares the current OLED or matrix frame against a golden file (or an inline text rendering) and fails the run on mismatch.
- **Exit status:** zero when the script completes and all assertions pass. Non-zero on a failed assertion, a sanitizer report, a crash, a script timeout on the virtual clock, or the wall-clock watchdog firing. The watchdog and `esp_restart()` each have their own code. An agent's process runner can then tell success from failure without looking at images.

An RPC protocol stays out of scope.

## What runs today

Checked on 2026-09-29. `pio run -e host` links the whole firmware tree, except `ble/` and `micropython/`, with sanitizers on. In a scripted run the firmware boots in about 6.7 s of virtual time (the 4 s boot splash is most of it) and reaches the home grid. Scripts then start.

- **Input.** A scripted `stick 0 1 400` moved the home grid to its second page. `tap B` opened PACKIT, whose pause menu then drew, and it opened the Matrix screen, where the OLED preview and the LED panel both drew. Both paths ran through the real `Inputs` ISR, debounce, smoothing, and rotation.
- **Repeatable.** Two runs of the same script gave byte-identical output, including the marquee text on the home screen. `host/run-tests.sh` runs the scripts under `host/scripts/` against goldens. `--update` rewrites the goldens.
- **Exit codes.** Each was checked with a deliberate failure: parse error 2, `expect` mismatch 3, virtual timeout 4, watchdog 5, `quit N`. Codes 6 and 7 were checked with a stand-alone program that sets the same `__asan_default_options` and `__ubsan_default_options`. Neither sanitizer has fired in the firmware itself.
- **Persistence.** With `--state-dir`, a second run skips the format and loads the saved settings.
- **Window capture.** With a window open, `snap` and the `S` key also read the window back with `SDL_RenderReadPixels`, save `<prefix>-window.png`, and compare every pixel with what `readOled()` and `readMatrix()` call for. Under the dummy driver the OLED and LED-matrix pixels of the 1048×384 window matched for both test scripts; the controls panel is left out of the comparison because it is drawn from the pins, and a deliberately wrong expected colour was reported as a mismatch. In a scripted run a mismatch exits with code 3, like a failed `expect`; a live `S` press only prints the message. The badge drives the matrix at about 10 of 255 PWM, so the window draws lit LEDs in pure red with every value mapped into 128–255 (PWM 1 is 128, PWM 255 is 255); the first version drew them darker than the off colour. Below the matrix the window shows virtual controls (arrows with a stick dot, and the Y/B/A/X diamond) drawn from the virtual pins, so scripts show up too, and they are clickable with the mouse. A key list is printed beside them, in a built-in 5×7 font. The default ambient mode is `Temporal`, a static logo (`kTemporalLogo32` in `LEDAppRuntime.cpp`), so a still matrix on the home screen is expected. The other modes animate.
- **Window.** The SDL code ran under `SDL_VIDEODRIVER=dummy`. It has not been seen on a real display, and neither the keyboard map nor a game controller has been tried by hand.

The shim's register array (`REG_READ`/`REG_WRITE`) compiles but nothing has used it yet: its only caller, the brown-out guard, sits behind `BADGE_HAS_SLEEP_SERVICE`. `pio run -e replay2026` still builds after the two firmware edits (`HardwareConfig.h` gained a `HARDWARE_HOST` branch, and the `Haptics.h` stub gained a constant), checked 2026-09-29. Neither touches the `replay2026` code path.

### Phase 2

Checked on 2026-09-29, after the MicroPython port was built into `[env:host]`.

- **REPL.** MicroPython v1.27.0 boots during `setup()` with the real `replay_bdev.c` formatting and mounting the FAT volume, and prints its banner. Over `--repl`, `print(6*7)` printed 42, `import badge` exposed 135 names, and `os.listdir("/")` showed the baked `lib` and `matrixApps` directories.
- **Badge API.** `oled_print` and `led_set_pixel` from the REPL drew on the OLED and lit the matching LED-matrix pixels, through the real `badge_mp_api` files.
- **Community App.** With `--fs-dir` putting `community_apps/starfield_nametag` at `/apps/starfield_nametag`, the menu gained a tile, a script selected it, and the app ran inside the real `mpy_gui_exec_file()`. Its animated starfield and nametag drew on the OLED, and the app's `time.sleep_ms()` calls reached the yield point, so scripted input kept working while it ran.
- **Heap budget.** The `heap_budget` fixture, launched from the menu, printed `BLOCKS 40`.
- **Tests.** `host/run-tests.sh` runs six scripts (`smoke`, `matrix-screen`, `community-app`, `heap-budget`, `replay-text`, `controls`). All but `controls` check goldens; `replay-text` opens the Matrix screen's Replay mode, opens the on-screen keyboard with Y, edits the wordmark, and checks that send returns to the carousel; `controls` runs with the window and fails if a virtual control is drawn differently from its pin state (skipped without a `DISPLAY`). A script's extra options live in `<name>.args`.

Not covered: the `machine`, `network`, `socket`, `espnow`, and `ssl` modules, which import but raise `NotImplementedError` on any use; `_thread`; the Python-side HTTP API, which returns an error; mouse clicks and the game controller in the window, which need real SDL input; and the `micropython_tests` files. The Phase 2 firmware edits (`mpconfigport.h`, `embed_util.c`) were built for the device, but nothing has run on a badge.

### Phase 3

Checked on 2026-09-29. `host/run-tests.sh` runs eight scripts and two unit-test steps; `packit-screen` (menu, resume, move, rotate, a falling piece, then Exit back to the home grid) and `helgrind-screen` (intro pages, then walking east and south in the world) pass, and the `packit-core` and `helgrind-core` steps (`make test`, and `make check` for Helgrind) pass after the viewers were removed. Neither script plays a game through: game logic stays covered by the per-game tests.

The world-data checker `check-world.py` and the string-width probe stay on the standalone Helgrind binary, because the harness cannot measure a font.
