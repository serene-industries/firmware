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
- The keyboard-level EEPROM block grew from 3 bytes to
  `5 + 2 * HE_CANCEL_PAIRS + 3 * SENSOR_COUNT`: mode, a two-byte layout mark,
  the key-cancel pairs, and deadzone / down / up per key. A block without the
  mark is treated as a fresh board. Macro space shrinks by the same amount
  (about 225 bytes).
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
- A board coming from the firmware before this layout keeps its levels and its
  calibration: `he_perkey_carry_over()` reads the old per-key block from where
  it sat (straight after the old 3-byte keyboard block) before anything is
  written. Without it the longer keyboard block makes the core report both
  blocks invalid and the board starts from defaults, calibration included.
  The keymap and macros are still erased by a flash from another day.
- **Rapid-trigger deadzone is kept at 15 or more** (`HE_MIN_DEADZONE`, both the
  all-keys command and the per-key one). The scan lets a key go only once it
  is back at deadzone minus 8; with a deadzone near the rest level a released
  key stays down. The host tests show it. Released firmware (Icebreaker HE
  1.0.4, Cleaver HE 0.4.2.14) takes any value from the all-keys command: the
  configurator's slider and the visualizer both stop at 15.
- **Key-cancel pairs: eight slots** (`HE_CANCEL_PAIRS`, `he_pairs[]`, value id
  24 with a pair index). A key is in one pair only; the set command refuses
  anything else. The scan (`he_update_key_keycancel`) keeps each pair's own
  state, so pairs work side by side and a key outside a pair pressed in
  between no longer breaks the cancel (it did when the pair was one global).
  The keyboard-level block is `5 + 2 * HE_CANCEL_PAIRS + 3 * SENSOR_COUNT`
  bytes, layout mark 0x48 0xE2.
- **Calibration read-back** (value id 26): whether the board is calibrating,
  which keys' bottom level has moved, one key's rest, bottom and stored
  bottom as raw ADC, leave without saving, one key back. Start and save are
  still ids 4 and 5. Changed with it: on save a key that was not pressed
  keeps the bottom level it had (before, it was stored at the start value,
  570 on the Icebreaker and 490 on the Cleaver, which made the key read
  100 % almost at once); the stream keeps showing travel while calibrating,
  against the level stored before.
- **A key is done when its press bottoms out** (`he_cal_note`, owner
  2026-11-25: a switch cannot be pushed further than it goes, so no fixed
  level may decide). Done = the reading sat still at its deepest point for
  100 ms, at least a quarter of the expected travel from rest, and not under
  half the middle travel of the keys done so far (a light press held still
  goes back to "not pressed yet"). A tap never counts; a deeper press later
  replaces a shallower level. The key's light is red until done, then green:
  the old yellow state, set by fixed levels (`CEILING_MEDIUM`,
  `CEILING_GOOD`), left keys yellow that had bottomed out. On save only done
  keys get a new level. Unproven on a board: the 100 ms and the tolerance
  (travel / 16 + 2 counts) are chosen from the source, not measured.
- `tests/host/` runs the boards' real source on a computer (`python3
  tests/host/run.py`, also a workflow on every push that touches the HE
  boards): VIA packets in, replies out, keys held through a stand-in ADC, the
  real scan, the EEPROM kept in a file across power cycles. Given the
  configurator's bundled `tests/he/emu-driver.ts` it also checks that the
  emulated board answers every packet as this source does. Run it before any
  HE release; it proves the logic, not the MCU.
