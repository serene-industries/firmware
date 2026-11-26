
//matrix.c
#include "he_switch_matrix.h" //instead of instead of #include "matrix.h"
#include "wait.h"
#include "print.h"
#include "rgblight.h"
#include "encoder.h"
#include "gpio.h"
#include "he_stream.h"
#include "he_midi.h"
#include "he_synth.h"
#include "he_perkey.h"
#include <string.h>

/* matrix state(1:on, 0:off) */
matrix_row_t raw_matrix[MATRIX_ROWS]; // raw values
matrix_row_t matrix[MATRIX_ROWS];     // debounced values

__attribute__((weak)) void matrix_init_kb(void) { matrix_init_user(); }
__attribute__((weak)) void matrix_scan_kb(void) { matrix_scan_user(); }
__attribute__((weak)) void matrix_init_user(void) {}
__attribute__((weak)) void matrix_scan_user(void) {}

uint8_t console_output = 0; //empty, turn on for debugging

void matrix_print(void) {
    he_matrix_print();
}

void matrix_init(void) {
    memset(he_live_travel, 0xFF, sizeof(he_live_travel)); // no sensor until the scan fills it
    he_init(he_key_configs, SENSOR_COUNT);

    matrix_init_kb(); //dummy call

    wait_ms(1); //can likely ditch this

    noise_floor_calibration_init();

    rgblight_init();

    encoder_driver_init(); // Initialize the rotary encoder driver

    setPinOutput(ENCODER_CLICK_PIN_A);
    writePinHigh(ENCODER_CLICK_PIN_A);

    palSetPadMode(GPIOB, 12, PAL_MODE_OUTPUT_PUSHPULL);
    palSetPad(GPIOB, 12); // writePinHigh

    // Configure PB15 as input with pull-down
    palSetPadMode(GPIOB, 15, PAL_MODE_INPUT_PULLDOWN);
    matrix_scan_kb(); //
}

uint8_t matrix_scan(void) {
    int updated = he_matrix_scan();  // Updated function call and type

    if (console_output == 0) {
        /*do nothing*/
    }
    else if (console_output == 1) { //for web app
        static int cnt = 0;
        if (cnt++ == 5) {
            cnt = 0;
            he_matrix_print();
        }
    }
    else if (console_output == 2) { //for debugging
        static int cnt2 = 0;
        if (cnt2++ == 5000) {
            cnt2 = 0;
            he_matrix_print_extended();
        }
    }
    else if (console_output == 3) { //todo REMOVE // both but slow
        static int cnt = 0;
        static int cnt2 = 0;
        if (cnt++ == 500) {
            cnt = 0;
            he_matrix_print();
        }
        if (cnt2++ == 1000) {
            cnt2 = 0;
            he_matrix_print_extended();
        }
    }
    else if (console_output == 4) { //rapid trigger web app
        static int cnt2 = 0;
        if(cnt2++ == 2000) {
            cnt2 = 0;
            he_matrix_print_rapid_trigger();
        }
    }
    else if (console_output == 5) { //0,0 escape key algorithm debug
        he_matrix_print_rapid_trigger_debug();
    }

    encoder_driver_task(); // Process encoder events

    matrix_scan_kb(); //to call matrix_scan_user i suppose

    // Real-time travel stream for the configurator (inert until the host asks).
    // Lives here because this file is in SRC; the <board>_he.c file is NOT
    // compiled (QMK only auto-adds <folder>.c), so hooks placed there never run.
    he_stream_task();
    he_midi_task();     // the DAW bridge: events whose millisecond has come, and the clock report
    he_synth_lights_task();   // the instrument's lights, when a key went down or came up
    he_perkey_task();   // saves mode, key-cancel pair and rapid-trigger numbers shortly after a change

    return updated ? 1 : 0;
}
