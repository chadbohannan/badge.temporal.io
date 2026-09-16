# The Public/Private Scope Boundary

`badge.temporal.io` is a deliberately curated subset of a larger internal project: `AGENTS.md` states plainly that "backend services and private event operations tooling are intentionally out of scope for this repository." This isn't just a licensing choice — it shapes design decisions throughout the codebase, and recognizing where the boundary falls explains several things that would otherwise look like missing features.

## Where the boundary shows up in the code

The [offline-first runtime](offline-first-runtime.md) concept is the clearest expression of this: the badge has no QR pairing gate and no private Replay API transport because those would require backend infrastructure this repo doesn't include. `AGENTS.md` separately warns to "avoid exposing old prototype target names as user-facing concepts" — a sign that the public build target (`replay2026`, see the [firmware system page](../systems/replay2026-firmware.md)) is a cleaned-up successor to internal-only naming that predates the public release, and that new user-facing strings or docs should be checked against that constraint rather than assumed safe by default.

The boundary also governs documentation practice directly: `AGENTS.md` instructs keeping docs "current-only" (no migration notes, stale TODOs, or historical implementation writeups unless they describe behavior that still exists), never committing private machine paths, credentials, or backend implementation details, and always documenting WiFi provisioning with placeholders or gitignored local files (`firmware/wifi.local.env`, `firmware/wifi.local.env.example`) rather than real SSIDs or passwords.

In practice this rule is violated in several places today: `firmware/initial_filesystem/apps/README.md`, both `firmware/initial_filesystem/docs/` guides, `firmware/micropython/README.md` and its `usermods/temporalbadge/README.md`, `firmware/src/doom/README.md`, and both `docs/markdown/` files all give build examples using `pio run -e echo`/`echo-dev`/`echo-doom` — none of which are environments defined in the current public `firmware/platformio.ini` (only `replay2026` and `replay2026-expanded` exist). See the [badge apps](../components/badge-apps.md) page for the full list; this is a concrete instance of the exact "old prototype target name" leak `AGENTS.md` warns against, not a hypothetical one.

## Why this wiki follows the same rule

This wiki inherits the same boundary by construction: it documents the public repository's own systems, components, and history, and has no visibility into whatever private backend or event-operations tooling exists alongside it. When investigating a topic that seems to imply a private counterpart — a backend the badge might talk to, an internal admin flow — the correct move is to document what the public repo actually does and stop there, not to speculate about or reconstruct the private side.
