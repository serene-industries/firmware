# Vendored Source Provenance

This hub **vendors** (copies in) board sources from their working repos so the
firmware lives in one place. This file records exactly where each tree came from
and how to rebuild. Reconciliation date: **2026-05-29**.

> Vendored files are point-in-time snapshots. When you change an upstream repo,
> re-vendor the affected board here to keep the hub current (this is the drift
> the hub is meant to eliminate — keep this file honest).

## Wireless — ZMK

- **`boards/wireless/icebreaker/`**
  - From: `serene-industries/zmk`, branch `icebreaker-studio`
  - Commit: `e6224af2` ("Fix keymap: Change HOME/END to PG_UP/PG_DN")
  - Path upstream: `app/boards/arm/icebreaker/`
- **`zmk-app-src/studio/`** — app-level ZMK Studio subsystems (cannot build
  standalone; they belong in `app/src/studio/` of the ZMK tree). Includes
  `ble_subsystem.c` (BLE profile management RPC), `lighting_subsystem.c`, `rpc.c`.
  - From the same `serene-industries/zmk@e6224af2`, path `app/src/studio/`.
- **`patches/0001-fix-rgb-underglow-settings-persistence.patch`**
  - From the now-retired overlay repo `-icebreaker-zmk-studio-firmware@dff5533`.

### Build (wireless)

```bash
west init -l zmk/app && west update
cd zmk && git apply ../patches/*.patch
cp -r ../boards/wireless/icebreaker app/boards/arm/icebreaker
# copy the studio subsystems into the app tree as well:
cp ../zmk-app-src/studio/* app/src/studio/
west build -p -b icebreaker -S studio-rpc-usb-uart app \
  -- -DZMK_CONFIG="$(pwd)/app/boards/arm/icebreaker"
# output: build/zephyr/zmk.uf2
```

> Uncommitted local WIP existed at reconciliation time (an `rgb_underglow.c`
> rework +184 lines, a new `stats_subsystem.c` typing-stats subsystem, and
> `SUBSYSTEM_ROADMAP.md`). Per decision, only the committed keymap fix was made
> canonical. That WIP is **not** vendored here; it is preserved on
> `serene-industries/zmk` branches `backup/icebreaker-studio-wip-20260529` and
> `backup/icebreaker-stash-alt-variant-20260529`.

## Wired — QMK

- **`boards/wired/icebreaker_hotswap/`**
  - From the QMK tree at `serene-industries/firmware`'s qmk clone, `keyboards/icebreaker/hotswap/` (upstream-based, commit `56a2e332`).
- **`boards/wired/icebreaker_hall_effect/`** (+ shared `icebreaker_rgb.{c,h}`)
  - From: `SmollChungus/qmk_firmware`, branch `dev_cleaver`, commit `a9ca3aa9`
  - Path upstream: `keyboards/icebreaker/`
- **`boards/wired/cleaver/`** (+ shared `cleaver_rgb.{c,h}`)
  - From: `SmollChungus/qmk_firmware@a9ca3aa9`, path `keyboards/cleaver/`

> **Hall-Effect variant divergence:** a *second, different* Icebreaker HE board
> existed only on the local machine (`~/qmk_firmware/keyboards/icebreaker/hall_effect/`,
> never committed). It differs in `config.h`, `matrix.c`, and keymaps. The
> tracked/published SmollChungus version was chosen as canonical above; the
> local-only variant is preserved on `serene-industries/qmk_firmware` branch
> `backup/icebreaker-he-board-local-20260529` for review. **Confirm which HE
> board ships to customers before cutting an HE release.**
>
> Note: `SmollChungus/qmk_firmware@origin/dev_cleaver` is `28a538cf` — 4 commits
> ahead of the vendored `a9ca3aa9`. Re-vendor if those commits are wanted.

### Build (wired)

Drop the board folder into a QMK tree under `keyboards/` and build with QMK/VIA
as usual, e.g.:

```bash
qmk compile -kb icebreaker/hall_effect -km via
```

## Retired / superseded repos

- `-icebreaker-zmk-studio-firmware` (overlay) — folded into this hub; was one
  commit behind the ZMK fork. Locally renamed `… OBDOLETE`.
- `zmk-config`, `serene-zmk-configurator` — abandoned 2024 predecessors.

See the reconciliation summary for archival status.
