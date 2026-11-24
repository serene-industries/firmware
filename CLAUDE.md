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
