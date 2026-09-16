# Flashing a Badge

The right flashing command depends on what you're trying to preserve and whether you're validating a source change or just getting a badge running. All paths ultimately go through [Ignition](../systems/ignition.md) or direct PlatformIO, and picking between them means first knowing which [storage tier](../components/storage-model.md) you're willing to touch.

## Just want a working badge (public path)

```bash
cd ignition
./setup.sh   # first time only
./doctor.sh  # first time only
./start.sh --latest-release
```

This downloads and flashes the latest `replay2026-factory-16MB.bin` from GitHub Releases. It's a full factory image write: bootloader, partition table, firmware, and FATFS all get replaced, which wipes any existing on-badge FATFS contents (apps, docs, uploads) but leaves NVS (identity, contacts, saves) untouched only if the badge already had valid NVS content from a prior boot — a genuinely blank chip has no NVS to preserve. Pin a specific release with `--release-tag v1.0.0`, or flash an already-downloaded image with `--no-build --factory-image ~/Downloads/replay2026-factory-16MB.bin`.

## Validating a firmware change from source

```bash
cd ignition
yes '' | ./start.sh -e replay2026
```

Per `firmware/src/README.md`, this — not a bare `pio run` — is the authoritative compile gate, because the local `.pio/build/replay2026/` cache has masked header-dependency failures across past refactors. `./start.sh --build-and-flash` explicitly forces a fresh build and a fresh default filesystem image rather than reusing the last one.

For a quick local sanity check before committing to a full Ignition run, `cd firmware && ./build.sh replay2026 -n` forces a clean rebuild without flashing anything.

## Only changed a few app files

Don't reflash firmware or FATFS at all — use the [diff-sync engine](../components/badge-sync.md):

```bash
cd firmware
python3 scripts/badge_sync.py sync /dev/cu.usbmodemXXXX
```

Disconnect any active serial monitor first (`pkill -f "device monitor"`); only one process can hold the badge's serial port. This preserves both NVS and any FATFS extras the badge already has — the opposite trade-off from a factory image flash.

## Direct PlatformIO (secondary path)

```bash
cd firmware
~/.platformio/penv/bin/pio run -e replay2026            # build
~/.platformio/penv/bin/pio run -e replay2026 -t upload  # flash firmware only
~/.platformio/penv/bin/pio run -e replay2026 -t uploadfs # flash filesystem only
```

Always use the wrapped `~/.platformio/penv/bin/pio`, not a bare `pio` on `$PATH` — the system shim often resolves through pyenv/micromamba to a Python without the `platformio` package installed, producing a `ModuleNotFoundError` that has nothing to do with the actual build. `firmware/build.sh` and `ignition/start.sh` already call the wrapped binary; this only bites when invoking `pio` by hand.

## WiFi provisioning while flashing

```bash
./start.sh -e replay2026 --wifi-ssid "YourNetwork" --wifi-pass "YourPassword"
```

or copy `firmware/wifi.local.env.example` to the gitignored `firmware/wifi.local.env` and run `./start.sh -e replay2026` with no flags. Never commit real credentials to either file — see the [public/private scope boundary](../concepts/public-repo-scope-boundary.md).
