# Serene Industries Firmware

Official firmware — source and releases — for all Serene Industries keyboards.

This repository is the **single hub** for the four boards. Board sources are
vendored here (copied in) from their upstream working repos; see
[SOURCES.md](SOURCES.md) for exact provenance and how to rebuild.

## Supported Keyboards

| Keyboard | Stack | Source | Release |
|---|---|---|---|
| **Icebreaker Wireless** | ZMK (nRF52840) | `config/boards/arm/icebreaker/` | auto-built in CI → Releases (.uf2) |
| **Icebreaker Hotswap** | QMK / VIA | `boards/wired/icebreaker_hotswap/` | Releases page (.uf2) |
| **Icebreaker Hall Effect** | QMK / VIA | `boards/wired/icebreaker_hall_effect/` | Releases page (.bin) |
| **Cleaver Hall Effect** | QMK / VIA | `boards/wired/cleaver/` | Releases page (.bin) |

## Ship wireless firmware from the cloud (no local machine)

The wireless firmware **compiles in GitHub Actions** — you never need a local
toolchain or the Mac.

1. Edit the board/keymap under `config/boards/arm/icebreaker/` (from any Claude
   Code session, web, or the GitHub UI) and push to `main`.
   → CI compiles it and uploads `icebreaker_wireless.uf2` as a build artifact.
2. When it's good, cut a release by pushing a version tag:
   ```bash
   git tag icebreaker-wireless-v0.2.1 && git push origin icebreaker-wireless-v0.2.1
   ```
   → CI compiles and **publishes a GitHub Release** with the `.uf2`.
3. The **si-configurator** reads releases from this repo by tag prefix
   (`icebreaker-wireless-v…`), so the new firmware appears in the configurator
   within minutes. No Vercel rebuild, no local build.

The ZMK core (with our patches) is pulled by `config/west.yml` from
`serene-industries/zmk@icebreaker-studio`; the board itself lives here.

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
build.yaml                      Cloud build matrix (board + ZMK Studio snippet)
config/
  west.yml                      Pulls patched ZMK from serene-industries/zmk
  boards/arm/icebreaker/        ← EDIT WIRELESS HERE (dts, keymap, defconfig, …)
boards/
  wired/icebreaker_hotswap/     QMK board (VIA)
  wired/icebreaker_hall_effect/ QMK board (VIA) + shared icebreaker_rgb
  wired/cleaver/                QMK board (VIA) + shared cleaver_rgb
zmk-app-src/studio/             ZMK Studio subsystems mirror (app-level; SOURCES.md)
patches/                        Standalone patches (also baked into the fork core)
releases/wireless/              Pre-built wireless .uf2 (pre-CI history)
.github/workflows/firmware.yml  Cloud build + auto-release
SOURCES.md                      Provenance + build instructions
CHANGELOG.md                    Release notes
VERSION                         Wireless firmware version
```

## Building

- **Wireless (ZMK):** builds automatically in CI — see "Ship wireless firmware
  from the cloud" above. To build locally, see [SOURCES.md](SOURCES.md).
- **Wired (QMK):** drop the board folder into a QMK tree and `qmk compile`
  (see [SOURCES.md](SOURCES.md)). Cloud CI for the wired boards is a TODO.

## License

SPDX-License-Identifier: MIT
