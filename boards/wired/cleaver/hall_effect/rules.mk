CUSTOM_MATRIX = yes
ANALOG_DRIVER_REQUIRED = yes

SRC += matrix.c he_switch_matrix.c cleaver_rgb.c

# Alternate LED chain order for the batch whose calibration lights the wrong
# keys. Build with: qmk compile -kb cleaver/hall_effect -km via -e CLEAVER_LED_ORDER_REVERSED=yes
ifeq ($(strip $(CLEAVER_LED_ORDER_REVERSED)), yes)
    OPT_DEFS += -DCLEAVER_LED_ORDER_REVERSED
endif
