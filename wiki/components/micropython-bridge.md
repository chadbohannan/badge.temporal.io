# MicroPython Bridge

User-facing badge apps are written in MicroPython, not C++, but every hardware capability they touch — the OLED, LED matrix, IMU, haptics, mouse overlay, IR — is a native driver underneath. The bridge that connects the two lives split across `firmware/src/micropython/` (the C++ side of the embedding) and `firmware/micropython/usermods/temporalbadge/` (the Python-visible usermod outside `firmware/src/`).

## Layering

`firmware/src/micropython/ReplayMicropythonAPI.cpp` is a thin pump aggregator; `MicroPythonBridge.cpp` owns lifecycle and garbage collection around each foreground app run; `StartupFiles.{cpp,h}` provisions the generated startup files (the app0 bake set described on the [storage model](storage-model.md) page) onto the FATFS partition at boot. Below that, `firmware/src/micropython/badge_mp_api/` holds one file per hardware category — `mp_api_display.cpp`, `mp_api_input.cpp`, `mp_api_led.cpp`, `mp_api_imu.cpp`, `mp_api_haptics.cpp`, `mp_api_mouse.cpp`, `mp_api_ir.cpp`, `mp_api_badge_data.cpp`, `mp_api_dev.cpp` — each implementing the `temporalbadge_runtime_*` functions that the usermod's QSTR table and `MP_DEFINE_CONST_FUN_OBJ_*` bindings expose to Python as `badge.*` calls.

Adding a new `badge.<thing>` API touches four places in order: the `temporalbadge_runtime_*` implementation in the matching `badge_mp_api/<category>.cpp`, its declaration in `temporalbadge_runtime.h`, the HAL pass-through in `temporalbadge_hal.{c,h}`, and the Python binding registration in `modtemporalbadge.c` — then a rebuild so generated MicroPython QSTR data picks up the new Python-visible name.

## Keeping Python responsive without threads

MicroPython apps run cooperatively on Core 1 alongside the GUI. `mpy_service_pump()` in `ReplayMicropythonAPI.cpp` is what runs during any Python-side sleep or poll call, servicing inputs, OLED/LED ownership, the mouse overlay, IMU, and haptics so a badge app that calls `time.sleep()` doesn't make the whole badge feel frozen. This is why native-backed UI helpers are structured as thin wrappers rather than long-running C++ calls: shared rendering logic belongs in `firmware/src/ui/OLEDLayout.{h,cpp}`, `ButtonGlyphs.{h,cpp}`, or `QRCodePlate.{h,cpp}` first, exposed through a small `temporalbadge_runtime_ui_*` wrapper, with `initial_filesystem/lib/badge_ui.py` calling that helper and falling back to a pure-Python implementation when it's absent.

## The Python heap: a 2 MB floor and a 4 MB ceiling

`mp_init_and_mount()` in `MicroPythonBridge.cpp` allocates the Python heap once at boot: `kMicroPythonHeapSize` (2 MB) through `ps_malloc`. The runtime then lives for the whole boot. `mp_soft_reboot()` would tear it down and rebuild it, but nothing calls it.

The 2 MB is only the starting size. `firmware/lib/micropython_embed/src/mpconfigport.h` sets `MICROPY_GC_SPLIT_HEAP_AUTO`, so when an allocation fails even after a collection, `gc_try_add_heap()` in `py/gc.c` adds a new heap area. Each new area roughly doubles the total heap, clipped to whatever `gc_get_max_new_split()` returns.

Until 2026-09-29 that function returned `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)`, and the area came from plain `malloc`. Nothing capped the heap: it could grow 2 → 4 → 8 MB into all free PSRAM and starve the firmware's own PSRAM buffers (`DataCache.cpp`, `EditorScreen.cpp`, the OTA partition-table buffers in `BadgeOTA.cpp`). With `heap_caps_malloc_extmem_enable(0)` in `configureBootHeapPolicy()` (`main.cpp`), a fragmented PSRAM could also put a new area into the internal DRAM that `TlsGate` and BLE compete for; see [internal-DRAM contention](../concepts/internal-dram-contention.md). Both were read from the code and never seen on a badge.

The budget change replaces that. `mpconfigport.h` defines `REPLAY_MP_HEAP_BUDGET`, the size of the whole Python heap including the initial slab, and defines `MP_PLAT_ALLOC_HEAP(n)` as `replay_mp_alloc_heap(n)`. `embed_util.c` implements that as `heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, and `gc_get_max_new_split()` now returns the smaller of the budget left and the largest free PSRAM block. It sums each area's tables and pool over the `MP_STATE_MEM(area)` list on every call, so `gc_sweep_free_blocks()` releasing an empty area needs no bookkeeping. The vendored `py/gc.c` is unchanged. A Python area can no longer land in internal DRAM.

Two things are still open. The value is a placeholder: 4 MB, the slab plus one doubling. It needs a `HeapDiag::printSummary` reading of PSRAM on a badge running a Python app beside WiFi, TLS, and `DataCache`. And the change alters badge behavior, since a Python app that used to reach 8 MB now gets `MemoryError` at 4 MB. It was built for the device (`pio run -e replay2026`) and exercised on the [host harness](../systems/host-test-harness.md), where a fixture that fills the heap with 100 KB blocks stops at 40 of them; it has not been run on a badge. `gc.mem_free()` reports the free space plus the room left to grow, so on a fresh boot it shows about 4.16 MB.

`mp_soft_reboot()` also has a latent leak. It calls `gc_init()` on the 2 MB slab, and `gc_setup_area()` sets `next = NULL` without freeing the areas that were added. It is harmless only while the function stays unused.

## Ambient LED handoff

The ambient LED matrix is owned by [`LEDAppRuntime`](led-app-runtime.md), not by whatever Python app happens to be running in the foreground. A MicroPython app that wants to draw to the LEDs must call `led_override_begin()`/`led_override_end()` (or `matrix_app_start()`), which is how the runtime knows to restore the saved ambient pattern when the app exits instead of leaving the matrix in whatever state the app last left it.

## A real MicroPython port, not just a `badge` module

Apps see two layers of API, not one. Beyond `badge.*` (the module backed by the `temporalbadge` usermod), the embed port — registered as board `TEMPORAL_BADGE_S3` (`firmware/micropython/boards/TEMPORAL_BADGE_S3/mpconfigboard.h`) — exposes the standard `ports/esp32` MicroPython surface to anyone connected over `mpremote`/ViperIDE/JumperIDE: `machine.Pin/ADC/PWM/Timer/WDT/SPI/SoftI2C/SoftSPI/I2S/UART/RTC/TouchPad`, `network.WLAN`/`socket`/`ssl`, `espnow.ESPNow`, `_thread`, `select`, and the usual stdlib (`os`, `time`, `json`, `binascii`, `math`, `cmath`, `random`, `heapq`, `uctypes`, `asyncio`). Two of these have badge-specific caveats: `network.WLAN` piggybacks on the WiFi driver Arduino's `WiFiService` already started for `BadgeAPI` HMAC calls — `network_common.c`'s `esp_initialise_wifi()` checks whether WiFi is already running before touching it, so Python code can read RSSI via `network.WLAN(network.STA_IF).status()` without disrupting the badge's connection — and `_thread` spawns real FreeRTOS threads but pins them to `MP_TASK_COREID` (Core 1), the same core the GUI and the foreground app already share cooperatively.

`bluetooth` (NimBLE) is off by default, gated behind `REPLAY_ENABLE_BLUETOOTH=1`, because Arduino ships a pre-compiled NimBLE without private headers — turning it on requires vendoring the full NimBLE source tree rather than flipping a config flag alone. This is a separate gate from the [BLE room presence](ble-proximity.md) system, which is excluded at the `build_src_filter` level instead.

## App source lives outside firmware/src

MicroPython app source is tracked under `firmware/initial_filesystem/`, not `firmware/src/` — the C++ tree only holds the bridge and drivers. The [app authoring model](badge-apps.md) page covers the folder-app conventions, shared helper libraries, and dev-iteration workflow that live in that tree. After editing an app or `firmware/initial_filesystem/lib/badge_app.py` / `badge_ui.py`, the expected sequence is `black` (formatting) and `python3 -m py_compile` (syntax check) on the changed files, then `python3 scripts/generate_startup_files.py` to regenerate the startup header and manifest, then a normal `pio run -e replay2026`. Only the source under `initial_filesystem/` is committed; PlatformIO regenerates everything derived from it during the build.
