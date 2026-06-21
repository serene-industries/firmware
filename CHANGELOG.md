# Changelog

All notable changes to Serene Industries firmware are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Cleaver Hall Effect — 0.4.2.7

### Changed
- **Windows layout: the key right of the spacebar (was right-side ⌘/Win) is now a
  Fn key.** Fn is now reachable on the bottom row as well as next to the arrow keys.

### Cleaver Hall Effect — 0.4.2.6

### Added
- **macOS layout option.** A Mac layout (Command sits next to the spacebar) is now
  available alongside the default Windows layout — selectable in the configurator's
  Firmware tab.

### Changed
- **Default lighting is now solid green at full brightness.** Applies on a fresh
  install or after resetting keyboard settings; existing custom colors are kept.

### Cleaver Hall Effect — 0.4.2.5

### Fixed
- **Waking the computer needed several key presses.** Waking a sleeping computer
  from the keyboard could take 4–5 presses; a single press now reliably wakes the
  host.

### Cleaver Hall Effect — 0.4.2.4
- Re-vendored from `SmollChungus/qmk_firmware@28a538cf` (dev_cleaver, 2026-04-28),
  the latest Cleaver line by Matthijs Muller. Includes the **row fix** ("rotate
  uneven row leds for 0.4.2"), matrix-scan cleanup, caps-lock-LED calibration
  fix, and console-off default. (Previously vendored a version behind these.)

### Changed
- **Repository reconciled (2026-05-29).** This repo is now the real firmware hub.
  All four board sources (Icebreaker Wireless / Hotswap / Hall Effect, Cleaver)
  are vendored here from their working repos. See [SOURCES.md](SOURCES.md).
- Documented corrected wireless key combinations (bootloader / sleep) — pending
  hardware verification.
- **CI: wireless releases now cut automatically from `VERSION`.** Bumping the
  `icebreaker-wireless` line in `VERSION` on `main` makes CI build and publish
  `icebreaker-wireless-v<version>` (if it doesn't already exist) — no tag push
  needed. Pushing the tag directly still works too. (`.github/workflows/firmware.yml`)

> Wireless ZMK changes now ship via tagged releases — see the entries below
> for the latest cuts.

## Wireless (ZMK) — [1.0.2] - 2026-06-03

### Fixed
- **Blue status LED stayed lit constantly (and during sleep).** The blue
  "connected" light no longer stays on around the clock or while the keyboard is
  asleep — it now lights only while the keyboard is in use, which also removes
  the related battery drain.

## Wireless (ZMK) — [1.0.1] - 2026-05-29

### Fixed
- **ESC key LED glitching after deep sleep.** On boards left idle long enough
  to reach deep sleep (~15 min), the ESC key (LED #0) would light a random
  purple/blue color and flicker. Root cause: the WS2812 data line (LED_LV =
  P0.13) was left floating by the `spi3_sleep` pinctrl, so once the LED power
  rail (P1.10) was cut on deep-sleep entry the floating line back-fed the first
  LED's DIN through its protection diode, parasitically powering it. Only LED #0
  was affected because the rest of the chain is fed from the previous LED's
  (now unpowered) DOUT. Fixed by adding `bias-pull-down` to the sleep pinctrl so
  the data line is held low while asleep (ZMK `icebreaker-studio` @ `968db2b`).
  - The "no rotary encoder connected" correlation in the field reports was
    incidental: those boards simply sat untouched long enough to reach deep
    sleep. The encoder pins (P0.29/P0.2) do not touch the LED data line.

### Changed
- First wireless release cut from the consolidated hub pipeline (built from the
  `serene-industries/zmk` fork @ `icebreaker-studio`, release hosted here). Also
  carries the previously-unreleased `HOME`/`END` → `PG_UP`/`PG_DN` keymap fix.

## Wireless (ZMK) — [0.2.0] - 2026-01-17

### Fixed
- RGB underglow settings now persist across reboots
  - Root cause: `zmk_rgb_underglow_set_hsb()` was not calling save
  - The RGB behavior converts relative commands (brightness up/down) to absolute HSB commands

### Changed
- Settings save debounce reduced to 0ms for immediate persistence
- Added commit handler for proper initialization after settings load

## Wireless (ZMK) — [0.1.0] - 2026-01-16

### Added
- Initial release with ZMK Studio support
- RGB underglow with WS2812 LEDs
- Rotary encoder support
- BLE connectivity with 5 profiles
- USB HID support
- Battery monitoring via MAX17048 fuel gauge
- Deep sleep support (15 min idle timeout)
- Soft off support for shipping/storage
