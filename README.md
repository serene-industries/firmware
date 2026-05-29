# Serene Industries Firmware

Official firmware — source and releases — for all Serene Industries keyboards.

This repository is the **single hub** for the four boards. Board sources are
vendored here (copied in) from their upstream working repos; see
[SOURCES.md](SOURCES.md) for exact provenance and how to rebuild.

## Supported Keyboards

| Keyboard | Stack | Source | Release |
|---|---|---|---|
| **Icebreaker Wireless** | ZMK (nRF52840) | [`serene-industries/zmk`](https://github.com/serene-industries/zmk) @ `icebreaker-studio` | auto-built in CI → Releases (.uf2) |
| **Icebreaker Hotswap** | QMK / VIA | `boards/wired/icebreaker_hotswap/` | Releases page (.uf2) |
| **Icebreaker Hall Effect** | QMK / VIA | `boards/wired/icebreaker_hall_effect/` | Releases page (.bin) |
| **Cleaver Hall Effect** | QMK / VIA | `boards/wired/cleaver/` | Releases page (.bin) |

## Ship wireless firmware from the cloud (no local machine)

The wireless firmware **compiles in GitHub Actions** — you never need a local
toolchain or the Mac.

**All wireless source lives in the fork** `serene-industries/zmk` @ branch
`icebreaker-studio` — board, keymap, Studio subsystems, and ZMK core together:
- keymap / layout / board → `app/boards/arm/icebreaker/`
- subsystem behavior (BLE, lighting, stats) → `app/src/studio/`
- RGB engine / core → `app/src/rgb_underglow.c`
- custom RPC message protos → `serene-industries/zmk-studio-messages` (pulled by west)

To ship:

1. Edit in the fork (`icebreaker-studio`) — any Claude session, web, or GitHub UI.
2. To get an artifact, run this repo's **Firmware (Wireless)** workflow (it pulls
   the fork and compiles), or just go to step 3.
3. Cut a release by pushing a version tag **here in the hub**:
   ```bash
   git tag icebreaker-wireless-v1.0.1 && git push origin icebreaker-wireless-v1.0.1
   ```
   → CI compiles from the fork and **publishes a GitHub Release** with the `.uf2`.
4. The **si-configurator** reads releases from this repo by tag prefix
   (`icebreaker-wireless-v…`), so the new firmware appears within minutes.
   No Vercel rebuild, no local build.

`config/west.yml` here just points the build at the fork; this repo carries no
wireless source of its own.

## Download

Visit the [Releases](https://github.com/serene-industries/firmware/releases) page,
or grab a pre-built wireless `.uf2` from `releases/wireless/`.

## Flashing Instructions

1. Download the firmware file for your keyboard.
2. Put your keyboard into bootloader mode using the key combination:
   - **Icebreaker Hotswap / HE / Cleaver**: WIN/CMD + LEFT SHIFT + ESCAPE
   - **Icebreaker Wireless**: LEFT SHIFT + SUP (Fn) + DEL
3. A USB drive will appear on your computer.
4. Drag and drop the firmware file onto the drive.
5. The keyboard will automatically reboot with the new firmware.

## Icebreaker Wireless Key Combinations

- **Bootloader mode**: LEFT SHIFT + SUP (Fn) + DEL
- **Sleep mode**: LEFT SHIFT + SUP (Fn) + ESC

> ⚠️ **Combos pending hardware verification.** Two earlier docs disagreed
> (`FN + LEFT SHIFT + ` (TILDE)` vs. the above). The values here come from the
> later correction; the authoritative source is the keymap in
> `boards/wireless/icebreaker/icebreaker.keymap` (the `adjust` layer carries
> `&bootloader`). Confirm on a physical unit before relying on this.

## Repository Structure

```
build.yaml                          Cloud build matrix (board + ZMK Studio snippet)
config/west.yml                     Points the wireless build at the zmk fork
boards/
  wired/icebreaker_hotswap/         QMK board (VIA)            ← edit wired here
  wired/icebreaker_hall_effect/     QMK board (VIA) + icebreaker_rgb
  wired/cleaver/                    QMK board (VIA) + cleaver_rgb
patches/                            Standalone patches (also baked into fork core)
releases/                           Pre-built binaries (wireless .uf2, wired .bin)
.github/workflows/firmware.yml      Cloud build + auto-release (wireless / ZMK)
.github/workflows/firmware-wired.yml Cloud build + auto-release (wired / QMK)
SOURCES.md                          Provenance + build instructions
CHANGELOG.md                        Release notes
VERSION                             Per-board shipped versions

Wireless source is NOT here — it's in serene-industries/zmk @ icebreaker-studio.
```

## Building

- **Wireless (ZMK):** builds automatically in CI — see "Ship wireless firmware
  from the cloud" above. To build locally, see [SOURCES.md](SOURCES.md).
- **Wired (QMK):** drop the board folder into a QMK tree and `qmk compile`
  (see [SOURCES.md](SOURCES.md)). Cloud CI for the wired boards is a TODO.

## License

SPDX-License-Identifier: MIT
