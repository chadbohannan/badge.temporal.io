# Wiki Helper Scripts

Tooling that supports the operations defined in [`../wiki/CLAUDE.md`](../wiki/CLAUDE.md). The scripts are deliberately stdlib-only (bash + Python 3, no third-party packages) so the template runs anywhere.

## Configuration

This project is a single repository, so `repos.txt` normally needs just one line: this checkout's own absolute path. Copy `repos.txt.example` (at the repo root) to `repos.txt` and fill it in. `repos.txt` holds a machine-local path and is gitignored — project identity (name, description, layout) lives committed in [`../AGENTS.md`](../AGENTS.md) and [`../wiki/CLAUDE.md`](../wiki/CLAUDE.md) instead.

## Scripts

| Script | What it does | Operation |
|--------|--------------|-----------|
| `pull-repos.sh` | Fast-forwards each tracked repo's `main`/`master`, skips feature branches, prints `repo before..after` for any that moved. Reads `repos.txt` (or `REPOS_DIR`/`REPOS_FILE` env overrides). | Sync |
| `lint-wiki.py` | Structural lint: broken internal links, orphan pages, and reference counts. Does **not** check contradictions or staleness — those need prose reading. Exit code 1 if broken links found. | Lint |

## Usage examples

```bash
# Sync: pull this repo (and any others listed in repos.txt) and see what moved
scripts/pull-repos.sh

# Lint: structural health check over wiki/
scripts/lint-wiki.py wiki
```

## Project-specific scripts

Operational tooling tied to this codebase already lives closer to what it operates on: firmware build/flash helpers under [`firmware/scripts/`](../firmware/scripts/), Ignition's build-and-flash pipeline under [`ignition/`](../ignition/), and the schedule/data build under [`data/build-data.py`](../data/build-data.py). This directory stays limited to the generic wiki-maintenance core.
