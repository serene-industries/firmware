# Vendored Source Provenance

This hub **vendors** (copies in) board sources from their working repos so the
firmware lives in one place. This file records exactly where each tree came from
and how to rebuild. Reconciliation date: **2026-05-29**.

> Vendored files are point-in-time snapshots. When you change an upstream repo,
> re-vendor the affected board here to keep the hub current (this is the drift
> the hub is meant to eliminate — keep this file honest).

## Wireless — ZMK

**All wireless source lives in the fork `serene-industries/zmk` @ `icebreaker-studio`**
— it is NOT vendored into this hub. The hub only builds it (`config/west.yml`
points at the fork) and hosts the releases.

In the fork:
- `app/boards/arm/icebreaker/` — board, keymap, defconfig
- `app/src/studio/` — Studio subsystems: `ble_subsystem.c`, `lighting_subsystem.c`,
  `stats_subsystem.c` (disabled), `rpc.c` routing
- `app/src/rgb_underglow.c` (+ header) — RGB engine / core change (RGB persistence)
- `app/west.yml` — pulls Zephyr/modules + the custom protos repo
  `serene-industries/zmk-studio-messages` (lighting/ble/stats `.proto`)

`patches/0001-fix-rgb-underglow-settings-persistence.patch` (kept here for
reference) is already baked into the fork's core, so no patch step is needed.

### Build (wireless)

The hub CI does this automatically. To build locally from the fork:

```bash
git clone -b icebreaker-studio https://github.com/serene-industries/zmk
cd zmk && west init -l app && west update && west zephyr-export
west build -s app -b icebreaker -S studio-rpc-usb-uart
# output: build/zephyr/zmk.uf2
```

> Backups from the reconciliation are on `serene-industries/zmk` branches
> `backup/icebreaker-studio-wip-20260529` and `backup/icebreaker-stash-alt-variant-20260529`.

## Wired — QMK

- **`boards/wired/icebreaker_hotswap/`**
  - From the QMK tree at `serene-industries/firmware`'s qmk clone, `keyboards/icebreaker/hotswap/` (upstream-based, commit `56a2e332`).
- **`boards/wired/icebreaker_hall_effect/`** (+ shared `icebreaker_rgb.{c,h}`)
  - From: `SmollChungus/qmk_firmware`, branch `dev_cleaver`, commit `28a538cf`
  - Path upstream: `keyboards/icebreaker/`
- **`boards/wired/cleaver/`** (+ shared `cleaver_rgb.{c,h}`) — **Cleaver HE v0.4.2.4**
  - From: `SmollChungus/qmk_firmware@28a538cf` (dev_cleaver HEAD, 2026-04-28),
    path `keyboards/cleaver/`
  - **Includes Matthijs Muller's 0.4.2 row fix** ("rotate uneven row leds for
    0.4.2", `1211f24d`) plus the matrix-scan cleanup (`f3b8f5ff`), caps-lock-LED
    fix (`178b373a`), and console-off default (`28a538cf`).
  - Note: **Matthijs Muller == SmollChungus** (`smollchungusm`, mpamuller) — he
    designs and maintains the Cleaver; `dev_cleaver` is his canonical line.

> **Hall-Effect variant divergence:** a *second, different* Icebreaker HE board
> existed only on the local machine (`~/qmk_firmware/keyboards/icebreaker/hall_effect/`,
> never committed). It differs in `config.h`, `matrix.c`, and keymaps. The
> tracked/published SmollChungus version was chosen as canonical above; the
> local-only variant is preserved on `serene-industries/qmk_firmware` branch
> `backup/icebreaker-he-board-local-20260529` for review. **Confirm which HE
> board ships to customers before cutting an HE release.**
>
> Vendored at `dev_cleaver` HEAD (`28a538cf`). When Matthijs pushes new Cleaver
> work, re-vendor `keyboards/cleaver` and `keyboards/icebreaker` from the new tip
> and bump the Cleaver version below.

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
