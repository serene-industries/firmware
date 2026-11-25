# Serene Industries firmware hub — conventions for Claude

- This repo must stay PUBLIC: it vendors GPLv2 QMK board source (Cleaver HE,
  Icebreaker HE/Hotswap) and hosts the release binaries the configurator
  downloads (GitHub Releases + the `releases/<tag>/` mirror fetched from
  raw.githubusercontent.com by the browser flasher).
- Releases: run `firmware-wired.yml` via workflow_dispatch with the tag
  (e.g. `cleaver-he-v0.4.2.12`); CI injects VIA_FIRMWARE_VERSION from the tag
  and mirrors binaries into `releases/<tag>/`. Publish straight to stable.
- Do not add `Claude-Session:` or `Co-Authored-By: Claude` trailers to
  commits (owner's instruction, 2026-11-08). Plain commit messages only.

## Firmware rules (learned 2026-11-24)

- **Confirm your code is compiled.** QMK only auto-adds `<folder>.c`
  (`hall_effect.c`), so `cleaver_he.c` and `icebreaker_he.c` are NOT built.
  Hooks placed there never run. Put code in a file listed in `SRC`
  (`matrix.c`, `he_switch_matrix.c`, the keymap's `via_apc.c`) and check the
  CI log for a `Compiling: .../<file>` line before publishing.
- **A green CI build proves it links, not that it runs.** Pair every new
  firmware path with a way to observe it from the configurator (a counter,
  a version, a read-back) before telling anyone to flash it.
- **Port to every board the feature claims.** Cleaver HE and Icebreaker HE
  share matrix code but are separate trees; a change in one is absent from
  the other until copied and released.
- The fork's core (`quantum/`, `tmk_core/`) is byte-identical to stock QMK
  0.25.0 in via.c, dynamic_keymap.c, quantum.c, action.c, send_string.c.
  Suspect board files and the configurator before the core.
- Test builds: publish with `prerelease: true`; the visualizer page offers
  prereleases, the main configurator does not.
- **Flashing wipes VIA state when the build date changes.** `via.c` derives
  its EEPROM magic from `QMK_BUILDDATE` (YY-MM-DD). A build from another
  day fails `via_eeprom_is_valid()`, so boot runs `dynamic_keymap_reset()`
  and `dynamic_keymap_macro_reset()`: remaps and macros are gone. Same-day
  builds keep them. This hits customers on every firmware update until the
  magic is pinned (open decision; see si-configurator ROADMAP).


## Per-key HE settings (branch work, 2026-11-25, not released)

- `he_perkey.c` / `he_perkey.h` in each HE board folder (identical files, listed
  in `SRC`): VIA custom value ids 22 info, 23 one key's levels by matrix row and
  column, 24 the key-cancel pair, 25 the MCU's unique ID. The header documents
  the bytes. Every VIA keymap's `via_apc.c` forwards those ids to it.
- The keyboard-level EEPROM block grew from 3 bytes to `7 + 3 * SENSOR_COUNT`:
  mode, a two-byte layout mark, the key-cancel pair, and deadzone / down / up
  per key. A block without the mark is treated as a fresh board. Macro space
  shrinks by the same amount (about 210 bytes).
- Behaviour that changed with it: the mode and the rapid-trigger numbers now
  survive a power cycle (before, only the mode keys stored the mode and nothing
  read it back; rapid-trigger numbers reset on every boot); the Cleaver's
  key-cancel pair defaults to A and D (it was sensors 32 and 34, which are S
  and F on the Cleaver's map).
- The all-keys actuate and release setters are applied by the keymap's
  housekeeping about 2 s after the last change; the per-key command applies at
  once. The deadzone getter answers in two bytes, high first.
- The configurator's client functions and an emulated board written from this
  source live in `si-configurator` (`lib/viaProtocol.ts`, `tests/he/`).
