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
