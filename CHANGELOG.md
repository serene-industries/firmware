# Changelog

All notable changes to Serene Industries firmware are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- **Repository reconciled (2026-05-29).** This repo is now the real firmware hub.
  All four board sources (Icebreaker Wireless / Hotswap / Hall Effect, Cleaver)
  are vendored here from their working repos. See [SOURCES.md](SOURCES.md).
- Wireless keymap: `HOME`/`END` above the Fn key corrected to `PG_UP`/`PG_DN`
  (ZMK `icebreaker-studio` @ `e6224af2`).
- Documented corrected wireless key combinations (bootloader / sleep) — pending
  hardware verification.

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
