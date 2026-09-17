# Wiki Index

The structured routing layer for the wiki. Each entry links to a page with a one-line description. This file is updated on every ingest.

## Systems

- [Replay 2026 Badge Firmware](systems/replay2026-firmware.md) — the `replay2026` PlatformIO environment (ESP32-S3), the C++/MicroPython split, the two-core threading model, and why Ignition is the authoritative build/flash gate rather than a bare `pio run`
- [Ignition](systems/ignition.md) — the Temporal-orchestrated build-and-flash tool for one badge or a fleet: per-badge child workflows, the batch-boundary "Enter prompt," the `--expected-count` preflight, and why a workflow engine (not a script) drives bulk USB flashing
- [Docs Site](systems/docs-site.md) — the zero-JS, zero-build-step static site at badge.temporal.io, the load-bearing CSS stylesheet order, and how its screenshots are captured from real hardware

## Components

- [The Badge Device](components/badge-device.md) — the physical device's compute, display, LED matrix, input, IMU, IR, haptics, battery/charging, connectors, flash-partition capacities, and form factor/accessories
- [Storage Model: NVS, FATFS, app0](components/storage-model.md) — the three-tier persistence rule (state → NVS via `badge.kv`, code → FATFS), the app0 survival-floor bake set, and how every flash path maps onto which tier it touches
- [badge_sync: The Diff-Sync Engine](components/badge-sync.md) — the single manifest-diffing implementation shared by raw shells, Ignition, and JumperIDE for pushing only changed files without a full reflash
- [MicroPython Bridge](components/micropython-bridge.md) — how `badge.*` Python calls reach native drivers through `badge_mp_api/`, the cooperative `mpy_service_pump()` that keeps apps responsive without threads, and where app source actually lives
- [Badge App Authoring Model](components/badge-apps.md) — the folder-app convention, `badge_app.py`/`badge_ui.py` shared helpers, the dev force-refresh iteration loop, and stale `-e echo` build commands in several app-facing docs
- [Boops: IR Contact Exchange](components/badge-boops.md) — the four-file split (journal/protocol/handlers/feedback) behind badge-to-badge IR contact exchange, its Core 0 IR transport, its v2 manifest-driven wire protocol, and where its identity card actually lives (corrected from an earlier version of this page)
- [NEC IR Protocol Layer](components/nec-ir-protocol.md) — the lowest-level RMT/NEC codecs shared by Boops and the consumer-remote "IR Playground," and two past silent-data-loss regressions in its frame-size ceilings
- [Doom Port (removed)](components/doom.md) — history of the DoomGeneric-based app mode (unplayable on the 1bpp OLED) and its 2026-09-16/17 removal, replaced by a "HELGRIND" placeholder
- [BLE Room Presence (disabled in public builds)](components/ble-proximity.md) — the fully-implemented but source-filtered-out venue beacon system, its HMAC rotating-UUID scheme, and why it can't coexist with WiFi
- [OTA and Asset Registry](components/ota-and-assets.md) — GitHub-Releases firmware OTA, in-place partition-layout migration, and the generic registry.json fetcher that also backs Community Apps
- [The Cooperative Scheduler](components/scheduler.md) — the `IService`/priority-divisor backbone that lets native subsystems share Core 1 without their own FreeRTOS tasks
- [Ignition Flash Workflow](components/ignition-flash-workflow.md) — the concrete Temporal workflow/activity mechanics: hand-rolled retry-with-reresolve, USB bootloader recovery pulses, subprocess-isolated boot/clock verification, and a fully-wired but never-called BLE verification activity
- [The Screen Stack](components/gui-screen-stack.md) — `Screen`/`ListMenuScreen`/`ModalScreen`/`GridMenuScreen`, the IMU-driven nametag overlay, and a pairing-era access-control mechanism that still runs on every screen transition but no longer blocks anything
- [UI Rendering Conventions](components/ui-conventions.md) — the never-hardcode-a-button-letter rule, the native (formerly Python-only) mouse overlay, and the on-screen keyboard's UTF-8-aware byte cursor
- [LEDAppRuntime: Ambient LED Ownership](components/led-app-runtime.md) — the single owner of the ambient LED matrix and the override/restore contract foreground surfaces must follow
- [Community Apps Registry](components/community-apps-registry.md) — the on-demand, GitHub-Release-hosted app-install path that keeps third-party apps out of the factory filesystem, and its automated PR review
- [Data Bundle: Schedule, Speakers, Floors](components/data-bundle.md) — the build-time-generated, embedded `bundle.bin` that backs the schedule/map screens, and why `data/out/` is committed while most generated output isn't
- [Schedule Screen](components/schedule-screen.md) — the native C++ conference-schedule screen, its cache-or-embedded (never live) data path, and a dead "MINE" personal-schedule toggle
- [Hardware Package](components/hardware-package.md) — the two-KiCad-project public hardware release (PCB-A main board, PCB-B backplate), its fabrication-output naming convention, and its public-safety hygiene bar
- [Release Workflow](components/release-workflow.md) — how a `firmware/VERSION` bump becomes a tagged GitHub Release with `firmware.bin`, the factory image, and `community_apps.json`, plus its skip/force-rebuild logic
- [Build and Developer Scripts](components/build-and-dev-scripts.md) — the five PlatformIO `extra_scripts` actually wired into every build (version injection, WiFi-config obfuscation, bundle symbol normalization) versus the standalone recovery-image, MicroPython-vendoring, and dual-flash tools that aren't

## Concepts

- [Offline-First Runtime](concepts/offline-first-runtime.md) — why the badge has no pairing gate or background polling, which features still need WiFi, and the "no background badge-owned API polling" rule
- [The Public/Private Scope Boundary](concepts/public-repo-scope-boundary.md) — how `AGENTS.md`'s public/private split shapes naming, docs practice, and this wiki's own scope, plus a concrete instance of the boundary being violated today
- [Third-Party Licensing Inside an MIT Repo](concepts/third-party-licensing.md) — Apache-2.0 (two imported Community Apps) carves out the live exception to the repo's default MIT license; GPL (Doom) and proprietary WAD game data were a historical exception, removed 2026-09-17
- [Internal-DRAM Contention](concepts/internal-dram-contention.md) — the shared constraint behind TlsGate, BLE's memory anchor, and OTA's pre-install heap prep: contiguous internal DRAM, not total RAM, is what actually runs out

## Runbooks

- [Flashing a Badge](runbooks/flashing-a-badge.md) — choosing between the public factory-image path, a from-source Ignition build, `badge_sync` for a few changed files, and direct PlatformIO
- [Capturing OLED Screenshots](runbooks/capturing-oled-screenshots.md) — `capture_oled_fb.py` and the badge-dev-framebuffer path used for real, on-device docs screenshots
- [Publishing a Release](runbooks/publishing-a-release.md) — the `firmware/VERSION`-bump release procedure, manual recovery/backfill tagging, and forcing a rebuild without a version bump

## Incidents

_None yet._

## Syntheses

- [Known Cruft, Dead Code, and Drift](syntheses/known-cruft-and-dead-code.md) — a running ledger of retired-but-executing code, features excluded from the public build, stale docs, silent data loss, and past silent-failure regressions found during ingestion
