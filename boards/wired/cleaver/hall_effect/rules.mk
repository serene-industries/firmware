CUSTOM_MATRIX = yes
ANALOG_DRIVER_REQUIRED = yes

SRC += matrix.c he_switch_matrix.c cleaver_rgb.c he_stream.c he_perkey.c he_synth.c

# Alternate LED chain order for the batch whose calibration lights the wrong
# keys. Build with: qmk compile -kb cleaver/hall_effect -km via -e CLEAVER_LED_ORDER_REVERSED=yes
ifeq ($(strip $(CLEAVER_LED_ORDER_REVERSED)), yes)
    OPT_DEFS += -DCLEAVER_LED_ORDER_REVERSED
endif

# The instrument mode sends MIDI: the board is a keyboard and a MIDI device at once. The MCU has three
# endpoints besides the control one; MIDI takes the one the debug console had (keyboard.json: console off).
MIDI_ENABLE = yes
