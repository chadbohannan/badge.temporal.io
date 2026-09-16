# Hardware Package

`hardware/` is the public design package for the two-board Replay 2026 Badge assembly, based on the final released hardware files from 2026-05-14. This page covers the design-file package itself; for the resulting device's specs, connectors, and capacities, see the [badge device](badge-device.md) page. It's organized by purpose rather than by board: `pcb/` holds the editable KiCad source, `fab/` holds production-facing fabrication outputs, `cad/` holds mechanical STEP references, and `assets/` holds artwork, renders, and exported board views.

## Two boards, two projects

`pcb/PCB-A/` is the main electronics board (`v0-Replay-26_PCB-A.kicad_pro`, with its own project symbol library `Temporal.kicad_sym` and footprint library `Temporal.pretty/`). `pcb/PCB-B/` is the backplate/art board (`pcb-backplate.kicad_pro`), which also ships an exported mechanical model (`pcb-backplate.step`) for cases where the board geometry is needed without opening KiCad. Some optional per-component 3D model paths in the PCB-A project point at placeholders under `pcb/assets/models/` — the original external library models weren't included in the public release, so those paths won't resolve to real geometry in a from-scratch KiCad checkout.

## Fabrication outputs and revisions

`fab/PCB-A/` and `fab/PCB-B/` hold what a fab house or assembler actually needs: Gerber packages, BOM and CPL (component placement list) files, with JLCPCB-specific BOM/CPL variants provided separately under `fab/PCB-A/JLCPCB/`. Production files follow a fixed naming pattern, `V0-Replay-26_<board>_<filetype>-<date-or-build>-R<revision>.<ext>`, and revisions are meant to be kept rather than deleted when a new one is generated — the current main-board Gerber package is `R3`, one revision ahead of the imported production note's `GBR-R2` change (increased via diameter to 0.3 mm, plus silkscreen for the SSD1309 display), so the note and the shipped Gerber intentionally don't describe the identical revision. The backplate is a simpler 1.6 mm two-layer board with matte black solder mask and ENIG finish; its BOM/CPL files only matter if the backplate is SMT-assembled with Wurth M2 standoffs, which the original production plan intended to skip.

## Keeping the package public-safe

This directory has an explicit hygiene bar beyond the repo's general `.gitignore`: no `.DS_Store`, `__MACOSX`, KiCad lock files (`.kicad_prl`), `.history` folders, nested `.git` folders, backup files, private supplier notes, or generated caches. Any time the PCB source changes, the matching Gerber/BOM/CPL/mechanical exports need regenerating and reviewing before publishing, with the filename revision bumped and `hardware/README.md` updated to describe the change — the README is the changelog for hardware revisions, not just a directory map.
