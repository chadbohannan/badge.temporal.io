[2026-09-15] ingest | Initial wiki bootstrap — created CLAUDE.md, index.md, 15 pages (3 systems, 9 components, 2 concepts, 3 runbooks) from AGENTS.md, README.md, and every subsystem README, as of commit b4c2dd0.
[2026-09-15] enrich | components/badge-device.md — new page: MCU, display, LED matrix, input, IMU, IR, haptics, battery, connectors, partition capacities, form factor. Cross-linked from hardware-package.md, replay2026-firmware.md.
[2026-09-15] ingest | Markdown sweep — read 24 remaining repo .md files. Created components/badge-apps.md, components/doom.md, concepts/third-party-licensing.md.
[2026-09-15] enrich | Corrected components/badge-boops.md: identity's primary store is FATFS `/badgeInfo.txt`, not NVS as previously stated. Enriched data-bundle.md, community-apps-registry.md.
[2026-09-15] enrich | systems/docs-site.md — flagged undocumented, drifted-ahead `docs/markdown/` copy of app docs. concepts/public-repo-scope-boundary.md — flagged 8 files referencing nonexistent `-e echo` PlatformIO envs.
[2026-09-16] ingest | firmware/src/ sweep: apps/, ble/, ota/, messaging/, api/, infra/, identity/, led/. Created components/ble-proximity.md, ota-and-assets.md, scheduler.md, concepts/internal-dram-contention.md.
[2026-09-16] enrich | led-app-runtime.md, storage-model.md (FATFS IOLock + settings-array bugs), data-bundle.md, badge-apps.md, community-apps-registry.md, release-workflow.md, offline-first-runtime.md — see components/ota-and-assets.md and ble-proximity.md for what's cited.
[2026-09-16] ingest | ignition/flash_worker/*.py + flash.py. Created components/ignition-flash-workflow.md: retry-with-reresolve design, USB bootloader recovery, verification probes, `verify_badge_ble` never called.
[2026-09-16] enrich | systems/ignition.md — `--expected-count` preflight, `--latest-release` download path, corrected bootloader-reset claim (native vs. QA-firmware badges).
[2026-09-16] ingest | firmware/src/screens/ + ui/. Created components/gui-screen-stack.md, ui-conventions.md. Found `ScreenAccess`/`badgeState` routing is live but always-permissive — a retired pairing gate.
[2026-09-16] enrich | offline-first-runtime.md — contrasted the screen-access vestige against MapScreens.cpp's clean BLE `#ifdef` guarding as "excluded vs. retired-in-place."
[2026-09-16] ingest | firmware/src/hw/ir/ + data/build-data.py. Created components/nec-ir-protocol.md: two historical NEC_MAX_WORDS/NEC_RX_SYMBOL_COUNT regressions, both silently dropping Boops data.
[2026-09-16] enrich | data-bundle.md — floors.md `sponsors:` tags are parsed-and-discarded, never reach floors.json.
[2026-09-16] update | Created syntheses/known-cruft-and-dead-code.md, a standing ledger of dead/retired/drifted findings. Added a log-length rule and a cruft-tracking instruction to CLAUDE.md.
[2026-09-16] ingest | firmware/scripts/*.py (all 14 files). Created components/build-and-dev-scripts.md. Found upload_dual.py is unwired dead weight superseded by Ignition's FlashBadgesWorkflow.
[2026-09-16] ingest | Fetched live badge.temporal.io (nav, api-reference.html, hacks.html, developer-guide.html). Enriched systems/docs-site.md: live site is the most conservative of 3 doc copies, no stale `-e echo`.
[2026-09-16] ingest | screens/{Contacts,Diagnostics,AssetLibrary,MenuOrder}Screen.cpp, ui/{FontCatalog,QRCodePlate,BadgeDisplay}.cpp. Enriched gui-screen-stack.md, ui-conventions.md, ota-and-assets.md.
[2026-09-16] enrich | Found BadgeDisplay.cpp's renderQR/renderBoop/renderInputTest/renderBoopResult are unreachable pre-GUIManager code — renderMode never set to their MODE_* values. Added to cruft ledger.
[2026-09-16] ingest | ignition/flash.py (Rich TUI polling/panels) + ignition/tests/*.py. Enriched systems/ignition.md: child-status query fan-out, memo tagging, unit-test coverage scope.
[2026-09-16] enrich | doom.md corrected: exit combo (LEFT+RIGHT held 1.5s) is two d-pad buttons, not the analog joystick, which can't hold two X positions at once.
[2026-09-16] enrich | micropython-bridge.md — added board name, badge/temporalbadge module naming, full ports/esp32 module surface, WiFi coexistence rule, gated Bluetooth reason.
[2026-09-16] ingest | screens/ScheduleScreen.cpp + ScheduleData.cpp. Created components/schedule-screen.md. Found the "MINE" personal-schedule toggle is dead (mode never set) and its cache refresh never actually fetches over network.
[2026-09-16] enrich | Added both Schedule findings to cruft ledger; cross-linked from gui-screen-stack.md and data-bundle.md.
