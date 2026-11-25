# Host tests for the Hall-effect boards

These run the boards' real source on a computer, with no keyboard attached.
`he_switch_matrix.c`, `he_perkey.c`, `he_stream.c` and the VIA keymap's
`via_apc.c` are compiled unchanged with gcc; `stubs/` stands in for the QMK
headers they include, and `harness.c` stands in for the MCU (EEPROM, timer,
mux pins, ADC) and for the core's four datablock calls. The keymap's
save-after-2-s block is lifted out of `keymap.c` at build time.

    python3 tests/host/run.py

One process is one power cycle; the EEPROM lives in a file between them. The
tests send VIA packets to `via_custom_value_command_kb()`, hold keys at a
depth through the ADC, run `he_matrix_scan()` and read the matrix. They cover,
on the Icebreaker HE and the Cleaver HE:

- a fresh board; one key's levels (set, read back, used by the scan at once,
  kept over a power cycle); what the firmware refuses or clamps;
- the key-cancel pairs (eight slots, refused cases, a key in one pair only,
  kept, emptied) and key cancel in the scan: a chosen pair, the default
  pair, no pair, two pairs side by side, a key outside the pair in between;
- per-key levels in the scan in normal mode and in rapid trigger;
- the all-keys commands (they land 2 s later and override per-key values);
  mode and rapid-trigger numbers kept over a power cycle;
- a board coming from the firmware before per-key settings: its levels and
  calibration are carried over (`he_perkey_carry_over`);
- calibration: a press that bottoms out under the default ceiling reads under
  100 % until the board is calibrated.
- calibration as the configurator sees it: which keys moved, one key back,
  leaving without saving, keys not pressed keeping what they had.

To also compare every reply with the configurator's emulated board (the one
its protocol tests run against), bundle its driver and pass it:

    cd ../si-configurator
    npx esbuild tests/he/emu-driver.ts --bundle --platform=node --outfile=/tmp/emu-driver.cjs
    cd ../firmware
    SEEDS=25 python3 tests/host/run.py /tmp/emu-driver.cjs

What this does not prove: anything about the real MCU (flash-backed EEPROM,
ADC timing, USB). A pass here means the logic is right, not that a board
runs it; see the firmware rules in CLAUDE.md.
