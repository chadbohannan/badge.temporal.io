# Publishing a Release

The normal path is a one-line change: bump `firmware/VERSION` to a stable semantic version (e.g. `1.2.3`, no `v` prefix) in a pull request. Once that lands on `main`, the [release workflow](../components/release-workflow.md) derives tag `v1.2.3`, builds `firmware.bin` and `replay2026-factory-16MB.bin`, regenerates `community_apps.json` from the current [Community Apps registry](../components/community-apps-registry.md), and creates the GitHub Release with all three assets — no manual tagging or manual build step required.

## Before merging the version bump

Run the same checks CI runs, from `firmware/`:

```bash
pio run -e replay2026
pio run -e replay2026 -t buildfs
./make_factory.sh replay2026 --no-build
```

## If the automatic tag-on-merge doesn't fire

Push the tag by hand as a recovery/backfill path:

```bash
git tag v$(tr -d '[:space:]' < firmware/VERSION)
git push origin v$(tr -d '[:space:]' < firmware/VERSION)
```

Publishing a GitHub Release manually also triggers the workflow. Leaving the tag field blank on a manual run derives `v<firmware/VERSION>` the same way — useful for bootstrapping a version bumped before this automation existed — while supplying an explicit tag rebuilds an existing release's assets.

## Forcing a rebuild without a version bump

Release builds skip automatically when firmware, embedded data, Community Apps, `release-assets/` docs, and the workflow file itself are unchanged since the previous release tag. Use the manual `force_rebuild` workflow input when only the Community Apps registry needs regenerating (a new app merged to `community_apps/` after the last release) or an existing release's assets need to be rebuilt for any other reason that doesn't warrant a version bump.
