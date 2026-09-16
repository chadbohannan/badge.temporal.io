# Release Workflow

`firmware/VERSION` is the single source of truth for the public firmware version — a bare semantic version string like `1.0.1`, no `v` prefix, no other file duplicating it. `.github/workflows/release-firmware.yml` watches for that file changing on `main`: when a version bump lands, the workflow derives the matching `v<version>` tag, builds and verifies every release artifact, then creates the tag and the GitHub Release at that exact merge commit. This makes "bump `firmware/VERSION` in a PR" the entire normal release procedure — no separate manual tagging step, no hand-run build.

Three artifacts come out of a release build, all produced from the `replay2026` PlatformIO environment described on the [firmware system page](../systems/replay2026-firmware.md):

- `firmware.bin` — the OTA application image. The badge's own OTA updater ([`BadgeOTA`](../components/ota-and-assets.md)) checks GitHub Releases on `temporal-community/badge.temporal.io` for this exact asset name.
- `replay2026-factory-16MB.bin` — a complete factory image: bootloader, partition table, application firmware, FAT filesystem, and `firmware/initial_filesystem/doom1.wad`. This is what [Ignition](../systems/ignition.md)'s `--latest-release` path downloads and flashes.
- `community_apps.json` — the registry described on the [Community Apps](community-apps-registry.md) page, regenerated fresh from `community_apps/` at release time rather than reused from a previous build.

## Recovery paths and idempotency

Pushing a matching `v<version>` tag by hand (`git tag v$(cat firmware/VERSION) && git push origin v...`) remains available as a backfill or recovery path if the automatic tag-on-merge didn't fire — useful for bootstrapping a version that was bumped before this automation existed. Publishing a GitHub Release manually also triggers the workflow; running it manually with the tag field blank derives `v<firmware/VERSION>` the same way the automatic path does, while supplying an explicit tag lets an existing release be rebuilt. The workflow refuses to move a conflicting existing tag during automatic publishing, which is the guard against two different commits silently claiming the same version tag.

To keep re-running the workflow cheap, release builds skip entirely when firmware, embedded data, Community Apps, the `release-assets/` docs, and the workflow file itself haven't changed since the previous release tag — the `force_rebuild` manual option overrides that skip when an existing release needs regenerated assets without a version bump (for example, after a Community App submission lands and the registry needs to reflect it). See the [publishing-a-release runbook](../runbooks/publishing-a-release.md) for the exact commands.
