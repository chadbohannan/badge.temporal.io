# badge_sync: The Diff-Sync Engine

`firmware/scripts/badge_sync.py` is the single implementation of file-level badge synchronization that every other tool reuses — raw developer shells, [Ignition](../systems/ignition.md)'s `sync_badge_filesystem` activity, and JumperIDE's "Sync Filesystem" button all speak the same MicroPython raw-REPL protocol (Ctrl-A) against the same manifest format, rather than each having its own bespoke push mechanism.

## How it decides what to push

It lists every file already on the badge's `/` with size and an FNV-1a hash via a small walker script (an extension of `viperide_reinit.py`), then diffs that against two manifests generated alongside `StartupFilesData.h`: `firmware/data/manifest.json` (the byte-mirror consumed by `pio -t uploadfs`) and `firmware/initial_filesystem/manifest.json` (the hand-edited source of truth). Missing or stale files are pushed as base64-chunked writes over the raw REPL. Clearing files on the badge that aren't in either manifest — i.e. deleting user uploads and extras — is off by default and only happens with `--clear-extras`, which matters because [the storage model](storage-model.md) treats FATFS extras (a manually-copied asset, or a user's own uploaded file) as things a normal sync should preserve, not wipe.

## When to reach for it

`badge_sync.py sync` is the fast path when only a couple of app files changed and a full `uploadfs` (which rewrites the entire FATFS image) would be needlessly slow. It's explicitly *not* a substitute for `uploadfs` after a fresh factory flash if the goal is to restore the full source tree's filesystem contents — that's a full-image job, not a diff job, though `badge_sync sync` can be run afterward if only a partial restore is wanted.

Any active serial monitor must be disconnected first (`pkill -f "device monitor"`); only one process can own the badge's USB serial port at a time, the same constraint that applies to `capture_oled_fb.py` (see the [OLED screenshot runbook](../runbooks/capturing-oled-screenshots.md)) and to JumperIDE.
