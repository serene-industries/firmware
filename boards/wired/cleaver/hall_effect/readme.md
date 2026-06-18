# cleaver HE

![cleaver_HE]

cleaver HE

* Keyboard Maintainer: [Smoll](https://github.com/smollchungus)

Make example for this keyboard (after setting up your build environment):

    make cleaver/hall_effect:default

Flashing example for this keyboard:

    make cleaver/hall_effect:default:flash

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 2 ways:

* **Keycode combo**: Hold `FN` (the `MO(1)` key, immediately right of the Up
  arrow) + `Left Shift`, then press `Esc`. This walks the layers to the
  `QK_BOOT` keycode (`FN` -> layer 1; on layer 1 Left Shift is `MO(2)` -> layer
  2; on layer 2 Esc is `QK_BOOT`).
* **Physical Boot0 pins**: Short the Boot0 pins on the back of the PCB while plugging in the keyboard

Note: Bootmagic (hold a key while plugging in) is **disabled** on this board
(`"bootmagic": false` in `keyboard.json`), so that method does not work.

To exit bootloader mode without flashing, just unplug the keyboard and plug it
back in.
