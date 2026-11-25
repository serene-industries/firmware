// See he_perkey.h. This file is listed in SRC (rules.mk); the scan loop calls he_perkey_task().
#include "he_perkey.h"
#include "he_synth.h"
#include "he_switch_matrix.h"
#include "timer.h"
#include "eeconfig.h"
#include <string.h>

#if defined(__has_include)
#    if __has_include("hardware_id.h")
#        include "hardware_id.h"
#        define HE_HAS_HARDWARE_ID
#    endif
#endif

extern const sensor_to_matrix_map_t sensor_to_matrix_map[];
// the board's lights (<board>_rgb.c): a key is red until it is done, then green
extern void end_calibration_visual(void);
extern void update_calibration_rgb(uint8_t sensor_id, uint16_t done);
extern void apply_calibration_changes_rgb(void);

he_cancel_pair_t he_pairs[HE_CANCEL_PAIRS];
uint8_t          he_pair_of[SENSOR_COUNT];

uint8_t he_sensor_at(uint8_t row, uint8_t col) {
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        if (sensor_to_matrix_map[i].row == row && sensor_to_matrix_map[i].col == col) return sensor_to_matrix_map[i].sensor_id;
    }
    return HE_NO_SENSOR;
}

// The per-sensor table from the pairs, and the pairs' memory of what is down cleared.
static void pairs_index(void) {
    memset(he_pair_of, HE_NO_PAIR, sizeof(he_pair_of));
    for (uint8_t p = 0; p < HE_CANCEL_PAIRS; p++) {
        he_pairs[p].a_down = he_pairs[p].b_down = false;
        he_pairs[p].latest = HE_NO_SENSOR;
        uint8_t a = he_pairs[p].a, b = he_pairs[p].b;
        // two different keys, neither already in an earlier pair; anything else is an empty slot
        if (a < SENSOR_COUNT && b < SENSOR_COUNT && a != b && he_pair_of[a] == HE_NO_PAIR && he_pair_of[b] == HE_NO_PAIR) {
            he_pair_of[a] = p;
            he_pair_of[b] = p;
        } else {
            he_pairs[p].a = he_pairs[p].b = HE_NO_SENSOR;
        }
    }
}

__attribute__((unused)) static void pairs_default(void) {
    for (uint8_t p = 0; p < HE_CANCEL_PAIRS; p++) he_pairs[p].a = he_pairs[p].b = HE_NO_SENSOR;
    he_pairs[0].a = HE_DEFAULT_CANCEL_A;
    he_pairs[0].b = HE_DEFAULT_CANCEL_B;
    pairs_index();
}

// ---- calibration: what the board had before, so a key can go back to it
static uint16_t cal_old[SENSOR_COUNT];
static bool     cal_have_old = false;
static bool     cal_done[SENSOR_COUNT];     // the key has bottomed out in this calibration
static bool     cal_still[SENSOR_COUNT];    // the key is sitting still at its deepest point...
static uint16_t cal_anchor[SENSOR_COUNT];   // ...at this reading...
static uint16_t cal_since[SENSOR_COUNT];    // ...since this time

void he_cal_begin(void) {
    if (he_config.he_calibration_mode) return;   // started twice: the first start holds the levels to go back to
    for (int i = 0; i < SENSOR_COUNT; i++) cal_old[i] = he_key_configs[i].noise_ceiling;
    memset(cal_still, 0, sizeof(cal_still));
    memset(cal_done, 0, sizeof(cal_done));
    cal_have_old = true;
}

static uint16_t apart(uint16_t a, uint16_t b) {
    return a > b ? a - b : b - a;
}

static uint16_t cal_depth(uint8_t s) {
    return apart(he_key_configs[s].noise_ceiling, he_key_configs[s].noise_floor);
}

// One key back to "not pressed yet": its light red, its bottom level at the start value.
static void cal_undo(uint8_t s) {
    he_key_configs[s].noise_ceiling        = HE_CAL_START_CEILING;
    eeprom_he_key_configs[s].noise_ceiling = cal_old[s];
    cal_still[s]                           = false;
    cal_done[s]                            = false;
    update_calibration_rgb(s, 0);
}

// The middle travel of the keys that are done. Called when a key completes, not in every scan.
static uint16_t cal_middle(void) {
    uint16_t v[SENSOR_COUNT];
    uint8_t  n = 0;
    for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
        if (!cal_done[s]) continue;
        uint16_t x = cal_depth(s);
        uint8_t  k = n++;
        while (k > 0 && v[k - 1] > x) {
            v[k] = v[k - 1];
            k--;
        }
        v[k] = x;
    }
    return n ? v[n / 2] : 0;
}

// A switch has bottomed out when its reading stops at its deepest point: no fixed level says where
// that is, so the key is done wherever it stops, provided it is a press and not a tap (the header
// lists the three conditions). Returns true when a key's light changed.
bool he_cal_note(uint8_t s, uint16_t raw) {
    if (!he_config.he_calibration_mode || !cal_have_old || cal_done[s]) return false;
    uint16_t ce = he_key_configs[s].noise_ceiling, depth = cal_depth(s), tol = depth / 16 + 2;
    if (ce == HE_CAL_START_CEILING || depth < HE_CAL_MIN_TRAVEL || apart(raw, ce) > tol) {
        cal_still[s] = false;   // not pressed, a brush, or no longer at its deepest point
        return false;
    }
    if (!cal_still[s] || apart(raw, cal_anchor[s]) > tol) {   // arrived, or still creeping: the wait starts here
        cal_still[s]  = true;
        cal_anchor[s] = raw;
        cal_since[s]  = timer_read();
        return false;
    }
    if (timer_elapsed(cal_since[s]) < HE_CAL_STILL_MS) return false;
    // A key far shallower than the ones already done is being held lightly, not bottomed out: it
    // does not count, and its light stays as it is.
    if ((uint32_t)depth * 2 < cal_middle()) {
        cal_still[s] = false;
        return false;
    }
    cal_done[s] = true;
    update_calibration_rgb(s, 1);
    // And the other way round: a key taken earlier, before the board had seen a real bottom, that
    // now turns out far shallower than the rest goes back to "not pressed yet".
    uint16_t middle = cal_middle();
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        if (cal_done[i] && (uint32_t)cal_depth(i) * 2 < middle) cal_undo(i);
    }
    return true;
}

// The start command sets every bottom level to HE_CAL_START_CEILING; a key whose level is still
// there has not been pressed in this calibration.
static bool cal_moved(uint8_t s) {
    return he_config.he_calibration_mode && cal_have_old && he_key_configs[s].noise_ceiling != HE_CAL_START_CEILING;
}

void he_cal_keep_untouched(void) {
    if (!cal_have_old) return;
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (!cal_done[i]) he_key_configs[i].noise_ceiling = cal_old[i];   // not pressed, or only brushed
    }
    cal_have_old = false;
}

uint8_t he_cal_travel(uint8_t s, uint16_t raw) {
    uint16_t fl = he_key_configs[s].noise_floor, ce = cal_have_old ? cal_old[s] : he_key_configs[s].noise_ceiling;
    if (ce > fl) {   // a sensor that reads higher when pressed
        if (raw <= fl) return 0;
        if (raw >= ce) return 100;
        return (uint8_t)(((uint32_t)(raw - fl) * 100u) / (ce - fl));
    }
    if (ce < fl) {   // one that reads lower
        if (raw >= fl) return 0;
        if (raw <= ce) return 100;
        return (uint8_t)(((uint32_t)(fl - raw) * 100u) / (fl - ce));
    }
    return 0;
}

#ifdef VIA_ENABLE

static uint8_t clampu8(uint8_t v, uint8_t lo, uint8_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#define HE_MAGIC_0 0x48   // 'H'
#define HE_MAGIC_1 0xE2   // layout 2 of the keyboard-level block (layout 1, one pair, was never released)

static bool     kb_dirty    = false;
static uint16_t kb_dirty_at = 0;

bool he_perkey_layout_ok(void) {
    return eeprom_he_config.layout_magic[0] == HE_MAGIC_0 && eeprom_he_config.layout_magic[1] == HE_MAGIC_1;
}

static void block_defaults(void) {
    eeprom_he_config.layout_magic[0] = HE_MAGIC_0;
    eeprom_he_config.layout_magic[1] = HE_MAGIC_1;
    eeprom_he_config.he_actuation_mode = ACTUATION_MODE_NORMAL;
    memset(eeprom_he_config.cancel_pairs, HE_NO_SENSOR, sizeof(eeprom_he_config.cancel_pairs));
    eeprom_he_config.cancel_pairs[0][0] = HE_DEFAULT_CANCEL_A;
    eeprom_he_config.cancel_pairs[0][1] = HE_DEFAULT_CANCEL_B;
    for (int i = 0; i < SENSOR_COUNT; i++) {
        eeprom_he_config.rt[i][0] = DEFAULT_DEADZONE_RT;
        eeprom_he_config.rt[i][1] = DEFAULT_RELEASE_DISTANCE_RT;
        eeprom_he_config.rt[i][2] = DEFAULT_RELEASE_DISTANCE_RT;
    }
}

void he_perkey_defaults(void) {
    block_defaults();
    he_perkey_load();
}

// A board coming from firmware before per-key settings has a 3-byte keyboard block with the
// per-key block (levels and calibration) straight after it. This firmware's keyboard block is
// longer, so the core reports the old one as invalid and the per-key block has moved. Read the
// old per-key block from where it was, before anything is written, so the update does not cost
// the calibration. he_init() calls this once, right after it has read both blocks.
#define HE_OLD_KB_DATA_SIZE 3
bool he_perkey_carry_over(void) {
    if (eeprom_read_dword(EECONFIG_KEYBOARD) != HE_OLD_KB_DATA_SIZE) return false;
    if (eeprom_read_dword(EECONFIG_USER) != (EECONFIG_USER_DATA_SIZE)) return false;
    uint8_t old_kb[HE_OLD_KB_DATA_SIZE];
    eeprom_read_block(old_kb, EECONFIG_KB_DATABLOCK, HE_OLD_KB_DATA_SIZE);
    if (old_kb[1] != 1) return false;   // he_post_flash: the old firmware never finished a first boot
    eeprom_read_block(&eeprom_he_key_configs, EECONFIG_KB_DATABLOCK + HE_OLD_KB_DATA_SIZE, sizeof(eeprom_he_key_configs));
    for (int i = 0; i < SENSOR_COUNT; i++) {
        uint8_t act = eeprom_he_key_configs[i].he_actuation_threshold, rel = eeprom_he_key_configs[i].he_release_threshold;
        if (!(act >= 10 && act <= 90 && rel >= 10 && rel <= 90 && rel < act)) {
            eeprom_he_key_configs[i].he_actuation_threshold = DEFAULT_ACTUATION_LEVEL;
            eeprom_he_key_configs[i].he_release_threshold   = DEFAULT_RELEASE_LEVEL;
        }
        if (eeprom_he_key_configs[i].noise_ceiling == 0 || eeprom_he_key_configs[i].noise_ceiling == 0xFFFF) {
            eeprom_he_key_configs[i].noise_ceiling = EXPECTED_NOISE_CEILING;
        }
    }
    eeprom_he_config.he_calibration_mode = false;
    eeprom_he_config.he_post_flash       = true;
    block_defaults();   // the old firmware always started in normal, with A and D as the pair
    return true;
}

void he_perkey_load(void) {
    for (int i = 0; i < SENSOR_COUNT; i++) {
        he_key_rapid_trigger_configs[i].deadzone           = clampu8(eeprom_he_config.rt[i][0], HE_MIN_DEADZONE, HE_MAX_DEADZONE);
        he_key_rapid_trigger_configs[i].engage_distance    = clampu8(eeprom_he_config.rt[i][1], 2, 50);
        he_key_rapid_trigger_configs[i].disengage_distance = clampu8(eeprom_he_config.rt[i][2], 2, 50);
        he_key_rapid_trigger_configs[i].boundary_value     = he_key_rapid_trigger_configs[i].deadzone;
    }
    for (uint8_t p = 0; p < HE_CANCEL_PAIRS; p++) {
        he_pairs[p].a = eeprom_he_config.cancel_pairs[p][0];
        he_pairs[p].b = eeprom_he_config.cancel_pairs[p][1];
    }
    pairs_index();   // drops anything that is not two different keys, each in one pair only
    if (eeprom_he_config.he_actuation_mode <= ACTUATION_MODE_KEY_CANCEL) he_config.he_actuation_mode = eeprom_he_config.he_actuation_mode;
}

void he_perkey_mark_dirty(void) {
    kb_dirty    = true;
    kb_dirty_at = timer_read();
}

void he_perkey_task(void) {
    if (!kb_dirty || timer_elapsed(kb_dirty_at) < EEPROM_SAVE_DELAY) return;
    kb_dirty = false;
    eeprom_he_config.he_actuation_mode = he_config.he_actuation_mode;
    for (uint8_t p = 0; p < HE_CANCEL_PAIRS; p++) {
        eeprom_he_config.cancel_pairs[p][0] = he_pairs[p].a;
        eeprom_he_config.cancel_pairs[p][1] = he_pairs[p].b;
    }
    for (int i = 0; i < SENSOR_COUNT; i++) {
        eeprom_he_config.rt[i][0] = he_key_rapid_trigger_configs[i].deadzone;
        eeprom_he_config.rt[i][1] = he_key_rapid_trigger_configs[i].engage_distance;
        eeprom_he_config.rt[i][2] = he_key_rapid_trigger_configs[i].disengage_distance;
    }
    eeconfig_update_kb_datablock(&eeprom_he_config);
}

static void put_position(uint8_t *out, uint8_t sensor) {
    if (sensor < SENSOR_COUNT) {
        out[0] = sensor_to_matrix_map[sensor].row;
        out[1] = sensor_to_matrix_map[sensor].col;
    } else {
        out[0] = 0xFF;
        out[1] = 0xFF;
    }
}

static void put_u16(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v >> 8);
    out[1] = (uint8_t)(v & 0xFF);
}

void he_perkey_get(uint8_t *d) {
    switch (d[0]) {
        case HE_VAL_INFO:
            d[1] = HE_PERKEY_PROTOCOL;
            d[2] = SENSOR_COUNT;
            d[3] = MATRIX_ROWS;
            d[4] = MATRIX_COLS;
            d[5] = HE_FEATURE_PER_KEY | HE_FEATURE_CANCEL_PAIR | HE_FEATURE_PERSISTED | HE_FEATURE_CALIBRATION | HE_FEATURE_SYNTH | HE_FEATURE_FAST_STREAM
#ifdef HE_HAS_HARDWARE_ID
                   | HE_FEATURE_BOARD_UID
#endif
                ;
            d[6] = HE_CANCEL_PAIRS;
            d[7] = he_scan_rate() >> 8;   // scans per second, measured (0 in the first half second)
            d[8] = he_scan_rate() & 0xFF;
            break;
        case HE_VAL_KEY: {
            uint8_t s = he_sensor_at(d[1], d[2]);
            if (s == HE_NO_SENSOR) {
                d[3] = 0;
                break;
            }
            d[3] = 1;
            d[4] = via_he_key_configs[s].he_actuation_threshold;
            d[5] = via_he_key_configs[s].he_release_threshold;
            d[6] = he_key_rapid_trigger_configs[s].deadzone;
            d[7] = he_key_rapid_trigger_configs[s].engage_distance;
            d[8] = he_key_rapid_trigger_configs[s].disengage_distance;
            break;
        }
        case HE_VAL_CANCEL:
            if (d[1] >= HE_CANCEL_PAIRS) {
                d[2] = 0;
                break;
            }
            d[2] = 1;
            put_position(&d[3], he_pairs[d[1]].a);
            put_position(&d[5], he_pairs[d[1]].b);
            break;
        case HE_VAL_UID: {
            memset(&d[1], 0, 12);
#ifdef HE_HAS_HARDWARE_ID
            hardware_id_t id = get_hardware_id();
            memcpy(&d[1], &id, sizeof(id) < 12 ? sizeof(id) : 12);
#endif
            break;
        }
        case HE_VAL_CAL:
            if (d[1] == 0) {
                d[2] = he_config.he_calibration_mode ? 1 : 0;
                memset(&d[3], 0, 4 * MATRIX_ROWS);
                for (uint8_t s = 0; s < SENSOR_COUNT; s++) {
                    if (!cal_moved(s)) continue;
                    uint8_t r = sensor_to_matrix_map[s].row, c = sensor_to_matrix_map[s].col;
                    d[3 + 2 * r + (c >> 3)] |= (uint8_t)(1u << (c & 7));
                    if (cal_done[s]) d[3 + 2 * MATRIX_ROWS + 2 * r + (c >> 3)] |= (uint8_t)(1u << (c & 7));
                }
            } else if (d[1] == 1) {
                uint8_t s = he_sensor_at(d[2], d[3]);
                if (s == HE_NO_SENSOR) {
                    d[4] = 0;
                    break;
                }
                d[4] = 1;
                put_u16(&d[5], he_key_configs[s].noise_floor);
                put_u16(&d[7], he_key_configs[s].noise_ceiling);
                put_u16(&d[9], (he_config.he_calibration_mode && cal_have_old) ? cal_old[s] : he_key_configs[s].noise_ceiling);
                d[11] = (cal_moved(s) ? 1 : 0) | (cal_moved(s) && cal_done[s] ? 2 : 0);
            }
            break;
    }
}

void he_perkey_set(uint8_t *d) {
    switch (d[0]) {
        case HE_VAL_KEY: {
            uint8_t s = he_sensor_at(d[1], d[2]);
            if (s == HE_NO_SENSOR) return;
            uint8_t act = d[3] == 0xFF ? via_he_key_configs[s].he_actuation_threshold : d[3];
            uint8_t rel = d[4] == 0xFF ? via_he_key_configs[s].he_release_threshold : d[4];
            // the same limits the all-keys commands apply: 10 to 90, release under actuate
            if (act >= 10 && act <= 90 && rel >= 10 && rel <= 90 && rel < act) {
                via_he_key_configs[s].he_actuation_threshold = act;
                via_he_key_configs[s].he_release_threshold   = rel;
                he_key_configs[s].he_actuation_threshold     = act;
                he_key_configs[s].he_release_threshold       = rel;
                eeprom_save_timer   = timer_read();   // the keymap's housekeeping writes the per-key block
                eeprom_save_pending = true;
            }
            if (d[5] != 0xFF) he_key_rapid_trigger_configs[s].deadzone = clampu8(d[5], HE_MIN_DEADZONE, HE_MAX_DEADZONE);
            if (d[6] != 0xFF) he_key_rapid_trigger_configs[s].engage_distance = clampu8(d[6], 2, 50);
            if (d[7] != 0xFF) he_key_rapid_trigger_configs[s].disengage_distance = clampu8(d[7], 2, 50);
            if (d[5] != 0xFF || d[6] != 0xFF || d[7] != 0xFF) he_perkey_mark_dirty();
            break;
        }
        case HE_VAL_CANCEL: {
            uint8_t p = d[1];
            if (p >= HE_CANCEL_PAIRS) return;
            uint8_t a = (d[2] == 0xFF) ? HE_NO_SENSOR : he_sensor_at(d[2], d[3]);
            uint8_t b = (d[4] == 0xFF) ? HE_NO_SENSOR : he_sensor_at(d[4], d[5]);
            if ((a == HE_NO_SENSOR) != (b == HE_NO_SENSOR)) return;   // two keys, or none
            if (a != HE_NO_SENSOR) {
                if (a == b) return;                                   // a key cannot cancel itself
                if ((he_pair_of[a] != HE_NO_PAIR && he_pair_of[a] != p) || (he_pair_of[b] != HE_NO_PAIR && he_pair_of[b] != p)) return;   // a key is in one pair only
            }
            he_pairs[p].a = a;
            he_pairs[p].b = b;
            pairs_index();
            he_perkey_mark_dirty();
            break;
        }
        case HE_VAL_CAL: {
            if (!he_config.he_calibration_mode || !cal_have_old) return;
            if (d[1] == 0) {          // leave calibration, nothing kept
                for (int i = 0; i < SENSOR_COUNT; i++) {
                    he_key_configs[i].noise_ceiling        = cal_old[i];
                    eeprom_he_key_configs[i].noise_ceiling = cal_old[i];
                }
                he_config.he_calibration_mode = false;
                cal_have_old                  = false;
                end_calibration_visual();
            } else if (d[1] == 1) {   // one key back to where it has not been pressed
                uint8_t s = he_sensor_at(d[2], d[3]);
                if (s == HE_NO_SENSOR) return;
                cal_undo(s);
                apply_calibration_changes_rgb();
            }
            break;
        }
    }
}

#else   // no VIA: one pair at its default, nothing stored

bool he_perkey_layout_ok(void) { return true; }
bool he_perkey_carry_over(void) { return false; }
void he_perkey_defaults(void) { pairs_default(); }
void he_perkey_load(void) { pairs_default(); }
void he_perkey_get(uint8_t *d) { (void)d; }
void he_perkey_set(uint8_t *d) { (void)d; }
void he_perkey_mark_dirty(void) {}
void he_perkey_task(void) {}

#endif
