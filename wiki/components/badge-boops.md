# Boops: IR Contact Exchange

"Boops" is the badge-to-badge local contact exchange over infrared — the mechanism for two badges physically pointed at each other to swap contact info without any network. The public facade is `firmware/src/boops/BadgeBoops.h`, but the implementation is deliberately split across four files by responsibility rather than left as one large state machine:

- `BoopsJournal.cpp` owns `/boops.json` and local writes — the persisted record of every past exchange.
- `BoopsProtocol.cpp` owns the state machine and frame codec — the actual IR conversation.
- `BoopsHandlers.cpp` is a per-type ops table (peer / exhibit / queue / kiosk / checkin / unknown), each registered with a `BOOP_HANDLER` macro that takes a capitalized type name, lowercase name, label, and a `doFieldExchange` flag.
- `BoopsFeedback.cpp` drives the marquee event ring and the haptic/LED cues that tell a user an exchange happened.

`Internal.h` exposes the symbols these four files share with each other without leaking them outside `boops/`. Adding a new boop handler means adding a block in `BoopsHandlers.cpp`, implementing the four `<lower>_<callback>` functions the macro expects, and adding a `BoopType` enum entry.

## Transport and threading

The high-level transport that `BoopsProtocol.cpp` rides on is `firmware/src/ir/BadgeIR.{cpp,h}`, which runs as a Core 0 task (`irTask`) with a Python-facing queue and a multi-word frame API for boop pairing. Below that, [`firmware/src/hw/ir/`](nec-ir-protocol.md) implements the low-level NEC IR protocol itself — RX/TX state machines and the multi-word encoder/decoder — kept in its own folder specifically because those files are protocol-abstraction-pure with no other dependencies, unlike everything layered on top. This split is why `irTask` and the low-level RMT decoders both run on Core 0 in the [firmware's two-core split](../systems/replay2026-firmware.md), separate from the GUI/MicroPython work on Core 1.

That low-level layer has twice been the site of a silent regression that broke the exchange phase above it: `NEC_MAX_WORDS` and `NEC_RX_SYMBOL_COUNT` were each independently shrunk for reasons that made sense before WiFi-based post-boop sync was removed, and each silently dropped `DATA` frames once Boops became IR-only — see [NEC IR protocol layer](nec-ir-protocol.md) for both incidents. Anyone touching the exchange phase's frame sizes or `kMaxTlvBytes` should check those two constants haven't drifted out of sync again.

## Debugging

Flipping `log_ir` or `log_boop` to `1` in `settings.txt` and watching `firmware/serial_log.py` is the documented way to trace an IR/boops session live, per `firmware/src/README.md`'s "how do I" table.

## Where identity comes from

**Correction (2026-09-15):** an earlier version of this page said the boop identity card "lives in NVS... rather than anywhere on FATFS." That's backwards for the primary path. `firmware/codeDocs/BoopSystem.md` documents the actual boot reconcile order in `main.cpp`: (1) try `BadgeInfo::loadFromFile()`, which reads the INI-format `/badgeInfo.txt` on FATFS; (2) if that file is missing, fall back to NVS (`BadgeStorage::loadBadgeInfo` + `loadMyTicketUUID`); (3) if NVS is also empty, seed random defaults from word lists via `esp_random()`; (4) write whatever was resolved back to `/badgeInfo.txt` and apply it to globals. So FATFS's `/badgeInfo.txt` is the primary source of truth in normal operation, and NVS is the legacy/fallback path rather than the other way around — `BadgeInfo.{cpp,h}` in `firmware/src/identity/` owns both. This still fits the [storage model](storage-model.md)'s tiers, just the other tier: identity is user-editable content the factory filesystem can legitimately re-seed with random defaults if lost, unlike truly irreplaceable state. `BadgeUID.{cpp,h}` separately derives the per-badge numeric UID used on the wire from the ESP32-S3's eFuse MAC plus an internally generated UUID v5, and is independent of the identity card's storage tier.

When paired with a server (`BadgePairing.cpp`'s `mergeFromServer`), an authoritative attendee record overwrites only fields that are still blank — user-typed optional fields are never clobbered by a later server sync, and the same non-destructive rule applies to fields filled in by an IR field exchange with a peer.

## Wire protocol (v2)

The current wire protocol (`kBoopProtocolVer = 0x02`) is a two-phase design: a lightweight NEC-multi-word **beacon phase** for mutual discovery, followed by a **manifest-driven streaming exchange phase** for the actual contact-card transfer. `BadgeIR` is pure transport (RMT hardware, `sendFrame`/`recvFrame`, self-echo filtering); `BadgeBoops` owns everything above that — phase state, codecs, journal writes, and per-type dispatch.

Beacon-phase frames (`0xB0` `IR_BOOP_BEACON`, `0xB1` `IR_BOOP_DONE`) keep the v1 wire layout so old, not-yet-reflashed badges interoperate for peer boops: a v1 sender's missing `BoopType` byte reads as `0x00` (`BOOP_PEER`), which happens to be the correct value. Once both sides mutually confirm (`beaconRxCount`/`beaconTxCount` thresholds met, then a few extra `0xB1` confirms so the peer is sure to see them), the state machine calls the matched `BoopHandlerOps.onLock` and — only for handlers with `doFieldExchange` set, only for peer-type boops, and only if `kBoopIrInfo` is enabled — transitions into `BOOP_PHASE_EXCHANGE`.

Exchange-phase frames (`0xC0`-`0xC5`: `MANIFEST`, `STREAM_REQ`, `DATA`, `NEED`, `FIN`, `FINACK`) implement a pull model: each side first advertises what it has and will send (`MANIFEST`, so empty or user-gated-off fields — email/website/phone/bio are individually toggleable via the `boop_fields` bitmask — never hit the wire at all), then the lower-UID badge ("primary") pulls first via `STREAM_REQ`, repairs gaps with targeted `NEED` requests instead of retransmitting whole frames, and the higher-UID badge ("secondary") pulls second the same way before both sides trade `FINACK` and close. There is deliberately **no timeout** in the exchange phase — the only exits are normal completion, explicit user cancel (Left button), or leaving the Boop screen; retransmission of the last unacknowledged "meta" frame runs every `kRetxMs` (2800 ms) for as long as the user keeps two badges pointed at each other. A per-frame payload cap (`kMaxTlvBytes = 120` bytes) keeps a typical full identity (name + title + company + email + website + one 32-byte bio chunk, ≈115 B) inside a single `DATA` frame, because splitting one exchange across two back-to-back `DATA` frames is an open, not-yet-fixed reliability bug (suspected `rmt_receive` re-arm race — see `firmware/codeDocs/BoopSystem.md`'s open-work section for candidate fixes).

Only 8 of 9 possible field tags are ever transmitted — `FIELD_ATTENDEE_TYPE` is unconditionally masked off on TX and never rendered by any screen, kept only so already-flashed senders' wire slot stays valid. No bitmaps, boop counts, timestamps, or notes cross the IR link; `user_notes` in particular is local-only and edited through the Contacts menu's `TextInputScreen`.

## What a boop records

`/boops.json` on FATFS mirrors the server's `BoopListResponse` shape (so it round-trips without transformation), caps at `kMaxBoopRecords = 30`, and tracks only the most recent visit per badge pair — a repeat boop bumps `boop_count` and overwrites `last_seen` rather than appending history. Records are keyed by the sorted `badge_uuids` pair; local-only boops are written with `status: "local"` and `pairing_id: 0`. Legacy records from older firmware (which kept a `connected_at` visit-history array) are migrated to the `last_seen`-only shape on load, which also shrinks the file on disk. Non-peer boops (`BOOP_EXHIBIT`, `BOOP_QUEUE_JOIN`, `BOOP_KIOSK_INFO`, `BOOP_CHECKIN`) skip the field-exchange phase entirely and instead just record an `installation_id`/`installation_kind` pair from the handler's `onLock`.

Two `[boop]` settings in `/settings.txt` govern exchange behavior at runtime: `boop_ir_info` (0/1, default on) gates whether identity exchange happens at all on offline boops, and `boop_fields` (0-0x1FF bitmask, default 0x1FF) lets a user opt out of broadcasting email/website/phone/bio individually while core fields (name/title/company) stay effectively always-on.
