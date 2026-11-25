CUSTOM_MATRIX = yes
ANALOG_DRIVER_REQUIRED = yes

SRC += matrix.c he_switch_matrix.c icebreaker_rgb.c he_stream.c he_perkey.c he_synth.c




# The instrument mode sends MIDI: the board is a keyboard and a MIDI device at once. The MCU has three
# endpoints besides the control one; MIDI takes the one the debug console had (keyboard.json: console off).
MIDI_ENABLE = yes
