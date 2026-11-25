// Per-key Hall-effect settings over VIA: one key's levels, the key-cancel pair,
// a board ID, and what this firmware supports. Shared by every HE keymap.
//
// VIA custom channel, value ids (get = id_custom_get_value, set = id_custom_set_value).
// d[] below starts at the value id, i.e. d[0] is the id itself.
//
//   HE_VAL_INFO (22) get   -> d[1] protocol version, d[2] sensor count, d[3] rows, d[4] cols,
//                              d[5] feature bits, d[6] key-cancel pairs supported
//   HE_VAL_KEY (23)  get   d[1] row, d[2] col -> d[3] 1 if a sensor sits there,
//                              d[4] actuate, d[5] release, d[6] deadzone, d[7] down, d[8] up
//                    set   d[1] row, d[2] col, d[3] actuate, d[4] release, d[5] deadzone,
//                              d[6] down, d[7] up   (0xFF leaves a value as it is)
//   HE_VAL_CANCEL (24) get d[1] pair index (0) -> d[2] 1 if valid, d[3] row A, d[4] col A,
//                              d[5] row B, d[6] col B   (0xFF 0xFF = no key)
//                      set d[1] pair index (0), d[2] row A, d[3] col A, d[4] row B, d[5] col B
//   HE_VAL_UID (25)  get   -> d[1..12] the MCU's unique ID (zeros if unavailable)
//
// Keys are named by matrix row and column, the same as the keymap commands.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define HE_VAL_INFO   22
#define HE_VAL_KEY    23
#define HE_VAL_CANCEL 24
#define HE_VAL_UID    25

#define HE_PERKEY_PROTOCOL 1
#define HE_FEATURE_PER_KEY     0x01
#define HE_FEATURE_CANCEL_PAIR 0x02
#define HE_FEATURE_BOARD_UID   0x04
#define HE_FEATURE_PERSISTED   0x08   // mode, rapid-trigger numbers and the pair survive a power cycle

#define HE_NO_SENSOR 0xFF

// The key-cancel pair as sensor ids (HE_NO_SENSOR = none). Read by the scan.
extern uint8_t he_cancel_a;
extern uint8_t he_cancel_b;
extern bool    he_cancel_a_down;
extern bool    he_cancel_b_down;

uint8_t he_sensor_at(uint8_t row, uint8_t col);
bool    he_perkey_layout_ok(void);   // the stored block is in this firmware's layout
bool    he_perkey_carry_over(void);  // a board from firmware before this layout: keep its levels and calibration
void    he_perkey_defaults(void);    // fresh board, or a block in another layout
void    he_perkey_load(void);        // stored block -> runtime
void    he_perkey_get(uint8_t *d);
void    he_perkey_set(uint8_t *d);
void    he_perkey_mark_dirty(void);  // mode, rapid-trigger numbers or the pair changed: save soon
void    he_perkey_task(void);        // call from the scan loop
