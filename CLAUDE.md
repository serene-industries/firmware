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


## Per-key HE settings (on main and stable since 2026-11-25: Icebreaker HE 1.0.5, Cleaver HE 0.4.2.15; the Cleaver build went out before anyone had seen it on a Cleaver)

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

## The instrument mode (branch work; BETA builds up to Icebreaker HE 1.0.10 / Cleaver HE 0.4.2.20, 2026-11-26; the owner played 1.0.6 and 1.0.7, and runs 1.0.10, on an Icebreaker HE; the Cleaver builds are unseen on a board)

- **On the owner's Icebreaker HE, 1.0.10, 2026-11-27** (from the configurator's DIAGNOSTICS line): the board
  enumerates as keyboard + raw HID + MIDI and the browser hears its notes (13 of 13); the moving stream runs at
  1 ms steps with no update missed; the bridge holds its seven control keys with nothing dropped; the lights
  report 28 keys painted; the scan rate reads 952 a second with the mode on. Not known from it: how the lights
  look, the drum layout on the board, the scan rate with the mode off, anything with a DAW.

- `he_synth.c` / `he_synth.h` in each HE board folder (identical, in `SRC`). Off at power-up; the keycode SYNTH
  (QK_KB_9, on the function layer beside the mode keys) or VIA value id 27 switches it. While on, the board
  carries a piano, by where a key sits (matrix row and column, the same on both boards) and not by what it types:
  the home row A to ' is the white keys from middle C (60), the row above holds the black keys over the gaps a
  piano has them in (W E, T Y U, O P, ]), and Z and X step the octave down and up (one step a press, a note
  that sounds keeps its pitch). Those keys do not type: `he_matrix_scan` hands them to `he_synth_key` and skips
  the typing path. Every other key types. The owner, of the first builds' tracker layout by keycode (two manuals,
  1.0.6 / 0.4.2.16): "the notes need to be positioned like on the piano, otherwise the placement makes no sense"
  and "the closest to a visual overlay as if a piano was projected onto the keyboard; people need to have an
  intuitive sense of how to play it immediately". It is also the layout Live and GarageBand give a computer
  keyboard. A build says which layout it has in d[18] of the id 27 get (1 the piano, 0 the older builds).
- Velocity is the time from the first depth to the second (12 % and 60 %), counted in scans and placed between
  scans from the readings either side of each crossing; converted with the measured scan rate; 2 ms and under is
  127, 80 ms and over is 1. Past the second depth travel is polyphonic pressure (a hundred a second per key at
  most). The note ends above the release depth (30 %). A change of mode, octave or channel ends what sounds
  first, on the channel it was started on.
- Nothing is stored: the settings are the defaults after a power cycle, and the EEPROM layout is unchanged.
- **USB changed**: `MIDI_ENABLE = yes` and `"console": false`. The STM32F411 has three endpoints besides the
  control one; MIDI took the console's. The debug printers are empty without the console, so DEBUG0 to DEBUG5
  do nothing in these builds. A board with this build enumerates as keyboard + raw HID + MIDI; nobody has seen
  that on a board yet, which is why it is a test build.
- The scan rate is measured (`he_synth_scan_begin`, half-second windows) and reported in info d[7..8] and in id 27.
- The stream's start value 2 asks for 250 frames a second (4 ms); 1 is 125 as before.
- **Start value 3 is the keys that move** (`moving_task` in `he_stream.c`, feature bit 0x80): a whole frame is
  three reports on an endpoint that carries one a millisecond, so whole frames stop near 330 a second. This sends
  one report a millisecond (id 0xF1: sequence, the board's clock in ms, up to 13 pairs of matrix index and
  travel) with only the keys whose travel changed by two steps or more since they were last sent, a whole frame
  four times a second to stand on, a report with no keys every 16 ms while nothing moves, and one millisecond in
  eight left free for the configurator's commands (so 875 reports a second at most; never fill the endpoint, the
  commands share it). The owner asked for it ("the stream is 200-something frames per sec instead of a higher
  rate that we discussed"). Not measured on a board: what it does to the scan rate (DIAGNOSTICS shows the rate).
- **The DAW bridge** (`he_midi.c` / `he_midi.h`, identical in both boards, in `SRC`; VIA value id 28; feature bit
  1 of a second features byte, info d[9]; BETA builds Icebreaker HE 1.0.8 / Cleaver HE 0.4.2.18, 2026-11-26,
  unseen on a board and never tried with a DAW). The owner: "make sure all of this is always in sync with the
  DAWs". A page cannot join a DAW's clock by itself, but the board is a MIDI device the DAW talks to and a HID
  device the page talks to, so it carries both ways, only while the instrument mode is on:
  clock in (MIDI clock, start, stop, continue, song position from the host; each tick stamped with the board's
  millisecond clock; tempo over the last quarter note; told to the page in report 0xF2 on every sixth tick and
  on every change, riding with the moving stream on the same clock); notes out (the page hands over events with
  the board-clock millisecond they are due, the board sends each in that millisecond; a queue of 48, counted
  when it overflows; all notes off on the channels used when the mode ends); keys as controls (twelve slots: a
  key's depth as a controller number, pitch bend up or down, or channel pressure, sent by the board itself at a
  hundred a second at most; such a key does not type; a note key or an octave key cannot be one); and "quiet"
  (the board's own notes are counted but not sent while the page plays them itself: what sounds is ended first).
  Nothing is stored. `midi_task()` runs once per `keyboard_task`, so a tick's stamp is good to one scan.
  Not built: the board as clock master when there is no DAW, MPE, the encoder.
- **The instrument's lights** (BETA builds Icebreaker HE 1.0.9 / Cleaver HE 0.4.2.19, 2026-11-26, unseen on a
  board; the owner: "for the synth drums, and for the piano keys, can we light up only the LEDs that correspond
  with the layout?"). While the mode is on only the keys that do something in it are lit: a white key white, a
  black key amber (hue 21), an octave key or a control key dim white, every other key dark; a key that is down
  (from the first depth) is at the brightness the lights had, the others at two fifths of it. `he_synth.c`
  decides (`light_kind`, `lights_paint`, `he_synth_lights_task` once a scan from `matrix.c`), the board's rgb
  file paints (`he_lights_paint`, `he_lights_end` in `icebreaker_rgb.c` / `cleaver_rgb.c`: it owns the
  sensor-to-LED map, saves the lighting's mode and colour at the first paint and puts them back on leaving).
  The strip is written at most forty times a second, and once a second to undo whatever else painted (a mode
  blink, a brightness key); not while calibrating or while a slider is shown. With the lights painted the
  keymap's four-key blink for the mode is skipped: the lights are the sign. A board whose lights are switched
  off stays dark and blinks as before. Id 27: get d[19] (bit 0 has them, bit 1 wanted, bit 2 painted now) and
  d[20] (keys lit); set d[1] 2 turns them off or on (not stored). Not measured on a board: what a strip write
  costs the scan rate and the 1 ms stream (DIAGNOSTICS shows the rate), and how the colours look through the
  keycaps. A drum layout will light the same way once the drum machine exists: whatever has a role is lit.
- **The drum layout and its lights** (BETA builds Icebreaker HE 1.0.10 / Cleaver HE 0.4.2.20, 2026-11-26, unseen
  on a board; the owner picked all four of a drum machine's light habits, "everything listed if they don't
  conflict", and asked why the lights were not built with the drum machine). The drum machine itself lives in
  the configurator; the board only needs to know its keys and show its lights. Id 27 set d[1] 3 turns the
  layout on or off: pads at rows 3, 2, 1, columns 1 to 4, steps at rows 0 to 3, columns 7 to 10, and up to six
  more keys the configurator names. While it is on those keys neither type nor play (`ROLE_DRUM`), the piano is
  off, and Z and X step no octave. The lights each take one property, so none hides another
  (`lights_paint_drums`, painted by `he_lights_paint_hsv` in the rgb files): a pad is its voice's colour
  (`DR_HUE`, round the wheel; the pad in hand brighter, a held pad full), a step with a hit is the colour of the
  voice in hand and as bright as its level, paler (saturation 110) if it holds a value of its own, the first
  step of a beat is marked faintly, and the step being played is full white. **The board moves that running
  light itself** (`drum_now`): on the DAW's clock (`he_midi_tick() / 6`), or from a board-clock time and a step
  length the configurator gave (set d[1] 5), so it is right when the browser is busy. Set d[1] 4 carries the
  voice in hand and its sixteen steps. The get says bit 3 of d[19] (knows the layout), bit 4 (it is on) and
  d[21] (the step the light is on, from 1). The mode going off turns the layout off. Not known: how eleven
  hues look through the keycaps, and what a strip write a sixteenth note costs the scan rate.
- Host tests: `run_synth` in `tests/host/run.py` (the piano by place whatever the keymap says, a strike, its
  velocity against its speed, pressure, the note's end, the octave keys, typing kept apart), `run_stream` (the
  moving stream: the first whole frame, the beat, a press as its own key, two keys in one report, the free
  millisecond, a one-step flicker not sent, stopping by itself), and the comparison with the configurator's
  emulated board covers id 27, the notes and the octave keys; `run_bridge` (the clock: start, ticks, tempo,
  stop, continue, a new position, none; events out at their millisecond; the queue's limit; controls: values,
  the two bend keys as one bend, the rate, a note key refused, rest on leaving; quiet), and the comparison
  covers id 28, clock bytes from the host and every message the bridge sends. The host build now compiles the stream code
  (`-DRAW_ENABLE`); before 2026-11-26 `he_stream.c` was built as empty stubs there and nothing tested it.
