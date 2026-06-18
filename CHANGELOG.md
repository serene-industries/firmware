# Changelog

All notable changes to Serene Industries firmware are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Cleaver Hall Effect — 0.4.2.5

### Fixed
- **Wake from sleep needed 4–5 presses.** Waking a sleeping host over USB
  relied on `matrix_scan()` reporting a key down, but `he_update_key()` only
  sets the matrix bit after `DEBOUNCE_THRESHOLD` (5) consecutive pressed scans.
  During USB suspend the scan loop is throttled by `suspend_power_down()`, so
  those scans are far apart and the counter resets between quick taps — a single
  tap never reached 5. Track USB suspend state via the weak
  `suspend_power_down_kb()`/`suspend_wakeup_init_kb()` hooks and bypass the
  debounce in `he_matrix_scan()` while suspended, so the first press past the
  actuation threshold wakes the host.

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
- **Blue status LED stayed lit constantly (and through sleep).** `LED_BOOT`
  (P1.00) was driven solid whenever BLE was connected, and the indicator never
  watched activity state — so on deep sleep the GPIO latched on, leaving the LED
  lit while asleep and draining the battery. `bt_indicator` now subscribes to
  `zmk_activity_state_changed` and turns the status LEDs off on idle/sleep,
  restoring the connection indicator on wake. The LED now lights only while the
  board is in use (ZMK `icebreaker-studio` @ `658a71e`).
  - Note: these are on/off GPIO LEDs (no PWM), and in deep sleep the SoC is
    fully powered down, so "off" is the only low-power state available there.

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
