// Per-key Hall-effect settings over VIA: one key's levels, the key-cancel pairs, calibration as
// the board sees it, a board ID, and what this firmware supports. Shared by every HE keymap.
//
// VIA custom channel, value ids (get = id_custom_get_value, set = id_custom_set_value).
// d[] below starts at the value id, i.e. d[0] is the id itself.
//
//   HE_VAL_INFO (22) get   -> d[1] protocol version, d[2] sensor count, d[3] rows, d[4] cols,
//                              d[5] feature bits, d[6] key-cancel pairs the board holds,
//                              d[7..8] scans per second as measured (high byte first)
//   HE_VAL_KEY (23)  get   d[1] row, d[2] col -> d[3] 1 if a sensor sits there,
//                              d[4] actuate, d[5] release, d[6] deadzone, d[7] down, d[8] up
//                    set   d[1] row, d[2] col, d[3] actuate, d[4] release, d[5] deadzone,
//                              d[6] down, d[7] up   (0xFF leaves a value as it is;
//                              deadzone is kept within 15 to 60, down and up within 2 to 50)
//   HE_VAL_CANCEL (24) get d[1] pair index -> d[2] 1 if the index exists, d[3] row A, d[4] col A,
//                              d[5] row B, d[6] col B   (0xFF 0xFF = no key: the slot is empty)
//                      set d[1] pair index, d[2] row A, d[3] col A, d[4] row B, d[5] col B
//                              (two different keys, neither in another pair; four 0xFF empty the slot)
//   HE_VAL_UID (25)  get   -> d[1..12] the MCU's unique ID (zeros if unavailable)
//   HE_VAL_CAL (26)  get   d[1] 0 -> d[2] 1 while calibrating, then two bitmaps of two bytes per
//                              matrix row, one bit per column (low byte = columns 0 to 7): from d[3]
//                              the keys whose bottom has moved in this calibration, after it the
//                              keys that are done (pressed until they stopped: the key's light is green)
//                    get   d[1] 1, d[2] row, d[3] col -> d[4] 1 if a sensor sits there,
//                              d[5..6] rest level, d[7..8] bottom level now, d[9..10] bottom level
//                              stored before this calibration (raw ADC, high byte first),
//                              d[11] bit 0 moved, bit 1 done
//                    set   d[1] 0                  leave calibration, nothing kept
//                    set   d[1] 1, d[2] row, d[3] col   that key back to "not pressed yet"
//   Calibration starts and is saved with the older ids 4 and 5. A key is done when its press has
//   bottomed out, wherever that is for this switch: see he_cal_note() in he_perkey.c for what
//   counts and how a light tap is kept out. On save only the keys that are done get a new bottom
//   level; every other key keeps the one it had.
//
// Keys are named by matrix row and column, the same as the keymap commands.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HE_VAL_INFO   22
#define HE_VAL_KEY    23
#define HE_VAL_CANCEL 24
#define HE_VAL_UID    25
#define HE_VAL_CAL    26

#define HE_PERKEY_PROTOCOL 1
#define HE_FEATURE_PER_KEY     0x01
#define HE_FEATURE_CANCEL_PAIR 0x02
#define HE_FEATURE_BOARD_UID   0x04
#define HE_FEATURE_PERSISTED   0x08   // mode, rapid-trigger numbers and the pairs survive a power cycle
#define HE_FEATURE_CALIBRATION 0x10   // HE_VAL_CAL

#define HE_NO_SENSOR 0xFF
#define HE_NO_PAIR   0xFF

// Rapid-trigger deadzone limits. The floor is a safety limit: the scan lets a key go only once it is
// back at deadzone minus 8 (he_update_key_rapid_trigger), so a deadzone near the rest level leaves the
// key held down after it is released. 15 is also the lowest value the configurator's slider offers.
#define HE_MIN_DEADZONE 15
#define HE_MAX_DEADZONE 60

// The key-cancel pairs as sensor ids (HE_NO_SENSOR in both = empty slot), with what the scan
// remembers about each: which of the two is physically down, and which was pressed last.
typedef struct {
    uint8_t a, b;
    uint8_t latest;
    bool    a_down, b_down;
} he_cancel_pair_t;
extern he_cancel_pair_t he_pairs[HE_CANCEL_PAIRS];
extern uint8_t          he_pair_of[SENSOR_COUNT];   // a sensor's pair index, HE_NO_PAIR if it has none

// The pair a key belongs to, or NULL. Checks the tables against each other, so a table that was
// never filled gives NULL and not a wrong pair.
static inline he_cancel_pair_t *he_pair_for(uint8_t sensor) {
    uint8_t p = sensor < SENSOR_COUNT ? he_pair_of[sensor] : HE_NO_PAIR;
    if (p >= HE_CANCEL_PAIRS) return NULL;
    he_cancel_pair_t *q = &he_pairs[p];
    return (q->a != q->b && (q->a == sensor || q->b == sensor)) ? q : NULL;
}

uint8_t he_sensor_at(uint8_t row, uint8_t col);
bool    he_perkey_layout_ok(void);   // the stored block is in this firmware's layout
bool    he_perkey_carry_over(void);  // a board from firmware before this layout: keep its levels and calibration
void    he_perkey_defaults(void);    // fresh board, or a block in another layout
void    he_perkey_load(void);        // stored block -> runtime
void    he_perkey_get(uint8_t *d);
void    he_perkey_set(uint8_t *d);
void    he_perkey_mark_dirty(void);  // mode, rapid-trigger numbers or a pair changed: save soon
void    he_perkey_task(void);        // call from the scan loop

// What makes a key done in a calibration (he_cal_note):
//   it has sat still at its deepest point for HE_CAL_STILL_MS (a tap turns round at once, a key on
//   its stop does not), at least HE_CAL_MIN_TRAVEL raw ADC from the rest level (a quarter of the
//   travel the board expects), and its travel is at least half the middle travel of the keys done
//   so far (a key held lightly part of the way stands out against the others and goes back to
//   "not pressed yet"). A later, deeper press always replaces a shallower level.
#define HE_CAL_STILL_MS 100
#define HE_CAL_MIN_TRAVEL ((EXPECTED_NOISE_CEILING > EXPECTED_NOISE_FLOOR ? EXPECTED_NOISE_CEILING - EXPECTED_NOISE_FLOOR : EXPECTED_NOISE_FLOOR - EXPECTED_NOISE_CEILING) / 4)

// Calibration, as the keymap's start and save commands and the calibration scan use it.
void    he_cal_begin(void);             // call before calibration mode is switched on: remembers the bottom levels
void    he_cal_keep_untouched(void);    // call first when saving: only keys that are done get a new level
uint8_t he_cal_travel(uint8_t sensor, uint16_t raw);   // travel against the level stored before this calibration
bool    he_cal_note(uint8_t sensor, uint16_t raw);     // call for every key in every calibration scan; true when a key's light changed
