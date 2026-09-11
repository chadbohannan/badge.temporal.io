# Replay 2026 Badge Docs Site

This directory is the static public documentation site for the Replay 2026
Badge. Files are plain HTML and CSS: no client-side JavaScript, build step, or
package manager. Vercel serves this directory directly.

Open [`index.html`](index.html) directly in a browser to preview the site.
The docs CI serves this directory directly and runs Lighthouse against the home
page before docs changes are merged.

## Files

- `index.html`: docs home page.
- `basics.html`: first-use badge overview.
- `get-started.html`: flashing, JumperIDE, and first app workflow.
- `developer-guide.html`: MicroPython developer guide.
- `api-reference.html`: badge MicroPython API summary.
- `apps.html`: app structure and filesystem notes.
- `hardware.html`: hardware overview and source package pointers.
- `hacks.html`: advanced topics, IR, and gotchas.
- `css/`: shared static stylesheets loaded directly by the HTML pages.
- `assets/photos/`: public event photos used by repo docs.
- `assets/screenshots/`: captured badge OLED screenshots used by repo docs.

## Stylesheet Order

Pages link `base.css` first, then the page-level sheets they need, then
`css/responsive.css` last. That order matters: the page-level sheets redeclare
selectors that `base.css` also sets, at the same specificity, so whichever
sheet comes last wins. All breakpoint rules live in `responsive.css` for that
reason -- a media query carries no extra specificity, so a media block placed
in an earlier sheet is silently overridden by any later sheet that touches the
same selector. Keep `responsive.css` last when adding a stylesheet to a page.

Shared values live as custom properties on `:root` in `base.css` (`--mono` for
the type stack, `--panel-solid` for card backgrounds, plus the colour tokens).

## Badge Screenshots

Use the firmware helper to capture the attached badge OLED as a PNG:

```bash
cd ../firmware
python3 scripts/capture_oled_fb.py \
  --out ../docs/assets/screenshots/my-screen.png
```

If multiple badges are connected, run the helper with `--list-ports`, then pass
the desired `--port`.

## WiFi Diagnostics

Badges can join 2.4 GHz WiFi networks only. For connection debugging, use
MicroPython's `network.WLAN` module from the raw REPL to print visible SSIDs,
channels, RSSI, and auth modes:

```python
import network, time

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
time.sleep(1)

for ssid, bssid, channel, rssi, authmode, hidden in wlan.scan():
    name = ssid.decode("utf-8", "ignore")
    print(name, "channel", channel, "rssi", rssi, "auth", authmode)

wlan.active(False)
```

The scan output does not include passwords.
