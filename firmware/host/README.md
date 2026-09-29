# Host test harness

The badge firmware built for a desktop: the real `setup()`, `loop()`,
`GUIManager`, every screen, `LEDmatrix`, and `LEDAppRuntime`, running over a small
platform shim. The OLED shows in an SDL window beside the 8x8 LED matrix, or a
scripted run drives the badge headless and checks the result by exit code.

MicroPython runs too: the REPL, the `badge` API, folder apps, and Community Apps.
Design and open items: `wiki/systems/host-test-harness.md`.

## Build and run

Run from `firmware/`.

```
pio run -e host                    # build (AddressSanitizer + UBSan on)
.pio/build/host/program            # live window
.pio/build/host/program --script host/scripts/smoke.txt
host/run-tests.sh                  # build, then run every host/scripts/*.txt and the game unit tests
```

From `firmware/host/`, `make play` builds and opens the window with `initial_filesystem`
loaded, `make build` only builds, and `make test` runs the scripts.

The live window needs `libsdl2-dev`. Without it the build still works and
scripted runs work.

| Option | Meaning |
|---|---|
| `--script FILE` | Run a script headless on the virtual clock. |
| `--window` | Show the window during a scripted run. |
| `--out DIR` | Where `snap` writes files. Default `.pio/host-out`. |
| `--state-dir DIR` | Keep the FAT image and settings between runs. |
| `--fs-dir SRC[:/DEST]` | Copy a host directory onto the badge's filesystem after boot, then rebuild the menu. Repeatable. Files already there are kept. |
| `--repl` | Feed stdin to the MicroPython REPL. Runs in real time. |
| `--max-virtual-ms N` | Fail if the script runs longer than this. |
| `--wall-timeout S` | Real-time watchdog. |

## Live controls

Arrow keys move the joystick (hold Shift for half deflection). `I` `L` `K` `J` are
the Y, B, A, X buttons, laid out as on the badge. A connected game controller works
too: left stick and the four face buttons. The window also has virtual controls below the
LED matrix, drawn from the virtual pins so scripts show up too, and clickable: hold the
mouse on an arrow or a face button. The key list is printed beside them. `S` saves a snapshot and `Esc` quits. With a window open, a snapshot also saves `<prefix>-window.png`, a read-back of the window's own pixels, and checks the OLED and LED matrix pixels against the frames (the controls panel is left out). In a script, a mismatch exits 3.

## Running Python

Add an app to the menu by copying it in. A folder app needs `/apps/<slug>/main.py`:

```
.pio/build/host/program \
  --fs-dir initial_filesystem \
  --fs-dir ../community_apps/starfield_nametag:/apps/starfield_nametag
```

To type into the REPL, run in a terminal with `--repl`, and start typing once the boot
messages stop. Input sent earlier is dropped, as on the badge.

The Python heap is capped at `REPLAY_MP_HEAP_BUDGET`, the same limit the badge uses.
`machine`, `network`, `socket`, `espnow`, and `ssl` import on the host but are stubs: using any
name in them raises `NotImplementedError` (`ssl` keeps the constants `/lib/ssl.py` reads at
import). `_thread` is off, and `badge.http_get`/`http_post` return an error.

## Scripts

One command per line, `#` starts a comment. A script starts when `setup()` returns.

```
press <Y|B|A|X>       release <Y|B|A|X>
tap <btn> [hold_ms]   # default 60 ms; under 20 ms is rejected (see below)
stick <x> <y> <ms>    # screen space, -1..1, y positive is down; then back to centre (0 ms holds it)
wait <ms>
snap <prefix>         # <prefix>-oled.png / -matrix.png / -oled.txt / -matrix.txt
dump oled|matrix      # text rendering to stdout
expect oled|matrix <golden file>
open <screen>         # push a screen directly: helgrind, vectortank, packit, matrix
log <text>            quit [code]
```

A press shorter than the firmware's 20 ms debounce is dropped on release and the
button then stays held, because the host has no contact bounce. `tap` therefore
refuses holds under 20 ms. The stick is smoothed by the firmware, so hold it for a
set time and `wait` for it to settle.

`expect` compares against a golden text file and writes `<golden>.actual` beside it
on a mismatch. After an intended UI change, `host/run-tests.sh --update` rewrites
the goldens.

## Exit status

| Code | Meaning |
|---|---|
| 0 | Script finished and every `expect` passed (or `quit 0`). |
| 1 | Bad command line, or the filesystem could not mount. |
| 2 | The script did not parse. |
| 3 | An `expect` failed, or a window capture did not match the frames. |
| 4 | The script ran past `--max-virtual-ms`. |
| 5 | The wall-clock watchdog fired. |
| 6 | AddressSanitizer report. |
| 7 | UndefinedBehaviorSanitizer report. |
| 42 | The firmware called `esp_restart()`. |
| 43 | The firmware entered deep sleep. |

## Layout

| Path | Contents |
|---|---|
| `HostDefines.h` | Pins and peripheral flags for `-DHARDWARE_HOST`. |
| `shim/` | Arduino core, FreeRTOS, and ESP-IDF subset. Clock, virtual pins, NVS, flash. |
| `harness/` | `main()`, SDL window, input injector, script runner, panels, `--fs-dir` loader. |
| `replacements/` | Whole-class replacements for WiFi, OTA, IR, the Python HTTP API, and the empty port modules. |
| `fixtures/` | Test apps for the scripts. |
| `scripts/` | Test scripts and their goldens. |
| `packit/`, `helgrind/` | SDL-free unit tests for the two game cores (`make test`). Helgrind also has `make check`. Play the games in the harness itself. |

## Limits

The IMU, haptics, battery gauge, and sleep service are not modelled; the firmware's
own no-hardware paths run. WiFi never connects, OTA and HTTP requests fail, and IR is
idle. The OLED panel is the U8g2 frame buffer, so contrast, invert, and power-save
commands are not shown.
