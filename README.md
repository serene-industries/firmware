# Serene Industries Firmware

Official firmware — source and releases — for all Serene Industries keyboards.

This repository is the **single hub** for the four boards. Board sources are
vendored here (copied in) from their upstream working repos; see
[SOURCES.md](SOURCES.md) for exact provenance and how to rebuild.

## Supported Keyboards

| Keyboard | Stack | Source | Release |
|---|---|---|---|
| **Icebreaker Wireless** | ZMK (nRF52840) | `boards/wireless/icebreaker/` | `releases/wireless/` (.uf2) |
| **Icebreaker Hotswap** | QMK / VIA | `boards/wired/icebreaker_hotswap/` | Releases page (.uf2) |
| **Icebreaker Hall Effect** | QMK / VIA | `boards/wired/icebreaker_hall_effect/` | Releases page (.bin) |
| **Cleaver Hall Effect** | QMK / VIA | `boards/wired/cleaver/` | Releases page (.bin) |

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
boards/
  wireless/icebreaker/          ZMK board definition (dts, keymap, defconfig, …)
  wired/icebreaker_hotswap/     QMK board (VIA)
  wired/icebreaker_hall_effect/ QMK board (VIA) + shared icebreaker_rgb
  wired/cleaver/                QMK board (VIA) + shared cleaver_rgb
zmk-app-src/studio/             ZMK Studio subsystems (app-level; see SOURCES.md)
patches/                        Patches to apply on top of upstream ZMK
releases/wireless/              Pre-built wireless .uf2 firmware
SOURCES.md                      Provenance + build instructions
CHANGELOG.md                    Release notes
VERSION                         Wireless firmware version
```

## Building

Per-stack build steps are in [SOURCES.md](SOURCES.md). The vendored board files
are snapshots; a full build still needs the corresponding ZMK or QMK tree.

## License

SPDX-License-Identifier: MIT
