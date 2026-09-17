# Capturing OLED Screenshots

The firmware exposes a raw framebuffer dump through the MicroPython `badge.dev("fb")` call. `firmware/scripts/capture_oled_fb.py` wraps that call and writes a scaled PNG matching the physical OLED's black background and white pixels, so documentation screenshots reflect the real on-device rendering rather than a mockup — this is how images under `docs/assets/screenshots/` (used by the [docs site](../systems/docs-site.md)) are produced.

```bash
cd firmware

# List connected badge serial ports.
~/.platformio/penv/bin/python scripts/capture_oled_fb.py --list-ports

# Capture the current screen.
~/.platformio/penv/bin/python scripts/capture_oled_fb.py \
  --out ../docs/assets/screenshots/my-screen.png

# Multiple badges connected: choose the port explicitly.
~/.platformio/penv/bin/python scripts/capture_oled_fb.py \
  --port /dev/cu.usbmodemXXX \
  --out ../docs/assets/screenshots/my-screen.png
```

`--scale 4` or `--scale 6` controls output PNG size.

Only one process can own the badge's USB serial port — close JumperIDE, any serial monitor, and Ignition before capturing, the same constraint documented for the [diff-sync engine](../components/badge-sync.md).
