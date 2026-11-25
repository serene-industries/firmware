// See he_perkey.h. This file is listed in SRC (rules.mk); the scan loop calls he_perkey_task().
#include "he_perkey.h"
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

uint8_t he_cancel_a      = HE_DEFAULT_CANCEL_A;
uint8_t he_cancel_b      = HE_DEFAULT_CANCEL_B;
bool    he_cancel_a_down = false;
bool    he_cancel_b_down = false;

uint8_t he_sensor_at(uint8_t row, uint8_t col) {
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        if (sensor_to_matrix_map[i].row == row && sensor_to_matrix_map[i].col == col) return sensor_to_matrix_map[i].sensor_id;
    }
    return HE_NO_SENSOR;
}

#ifdef VIA_ENABLE

static uint8_t clampu8(uint8_t v, uint8_t lo, uint8_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#define HE_MAGIC_0 0x48   // 'H'
#define HE_MAGIC_1 0xE1   // layout 1 of the keyboard-level block

static bool     kb_dirty    = false;
static uint16_t kb_dirty_at = 0;

static void cancel_reset_state(void) {
    extern uint8_t latest_pressed;
    he_cancel_a_down = false;
    he_cancel_b_down = false;
    latest_pressed   = HE_NO_SENSOR;
}

bool he_perkey_layout_ok(void) {
    return eeprom_he_config.layout_magic[0] == HE_MAGIC_0 && eeprom_he_config.layout_magic[1] == HE_MAGIC_1;
}

void he_perkey_defaults(void) {
    eeprom_he_config.layout_magic[0] = HE_MAGIC_0;
    eeprom_he_config.layout_magic[1] = HE_MAGIC_1;
    eeprom_he_config.he_actuation_mode = ACTUATION_MODE_NORMAL;
    eeprom_he_config.cancel_pair[0]  = HE_DEFAULT_CANCEL_A;
    eeprom_he_config.cancel_pair[1]  = HE_DEFAULT_CANCEL_B;
    for (int i = 0; i < SENSOR_COUNT; i++) {
        eeprom_he_config.rt[i][0] = DEFAULT_DEADZONE_RT;
        eeprom_he_config.rt[i][1] = DEFAULT_RELEASE_DISTANCE_RT;
        eeprom_he_config.rt[i][2] = DEFAULT_RELEASE_DISTANCE_RT;
    }
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
    eeprom_he_config.he_actuation_mode   = ACTUATION_MODE_NORMAL;   // the old firmware always started in normal
    eeprom_he_config.layout_magic[0]     = HE_MAGIC_0;
    eeprom_he_config.layout_magic[1]     = HE_MAGIC_1;
    eeprom_he_config.cancel_pair[0]      = HE_DEFAULT_CANCEL_A;
    eeprom_he_config.cancel_pair[1]      = HE_DEFAULT_CANCEL_B;
    for (int i = 0; i < SENSOR_COUNT; i++) {
        eeprom_he_config.rt[i][0] = DEFAULT_DEADZONE_RT;
        eeprom_he_config.rt[i][1] = DEFAULT_RELEASE_DISTANCE_RT;
        eeprom_he_config.rt[i][2] = DEFAULT_RELEASE_DISTANCE_RT;
    }
    return true;
}

void he_perkey_load(void) {
    for (int i = 0; i < SENSOR_COUNT; i++) {
        he_key_rapid_trigger_configs[i].deadzone           = clampu8(eeprom_he_config.rt[i][0], HE_MIN_DEADZONE, HE_MAX_DEADZONE);
        he_key_rapid_trigger_configs[i].engage_distance    = clampu8(eeprom_he_config.rt[i][1], 2, 50);
        he_key_rapid_trigger_configs[i].disengage_distance = clampu8(eeprom_he_config.rt[i][2], 2, 50);
        he_key_rapid_trigger_configs[i].boundary_value     = he_key_rapid_trigger_configs[i].deadzone;
    }
    uint8_t a = eeprom_he_config.cancel_pair[0], b = eeprom_he_config.cancel_pair[1];
    bool pair_ok = a < SENSOR_COUNT && b < SENSOR_COUNT && a != b;   // a pair is two different keys, or none
    he_cancel_a  = pair_ok ? a : HE_NO_SENSOR;
    he_cancel_b  = pair_ok ? b : HE_NO_SENSOR;
    if (eeprom_he_config.he_actuation_mode <= ACTUATION_MODE_KEY_CANCEL) he_config.he_actuation_mode = eeprom_he_config.he_actuation_mode;
    cancel_reset_state();
}

void he_perkey_mark_dirty(void) {
    kb_dirty    = true;
    kb_dirty_at = timer_read();
}

void he_perkey_task(void) {
    if (!kb_dirty || timer_elapsed(kb_dirty_at) < EEPROM_SAVE_DELAY) return;
    kb_dirty = false;
    eeprom_he_config.he_actuation_mode = he_config.he_actuation_mode;
    eeprom_he_config.cancel_pair[0]    = he_cancel_a;
    eeprom_he_config.cancel_pair[1]    = he_cancel_b;
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

void he_perkey_get(uint8_t *d) {
    switch (d[0]) {
        case HE_VAL_INFO:
            d[1] = HE_PERKEY_PROTOCOL;
            d[2] = SENSOR_COUNT;
            d[3] = MATRIX_ROWS;
            d[4] = MATRIX_COLS;
            d[5] = HE_FEATURE_PER_KEY | HE_FEATURE_CANCEL_PAIR | HE_FEATURE_PERSISTED
#ifdef HE_HAS_HARDWARE_ID
                   | HE_FEATURE_BOARD_UID
#endif
                ;
            d[6] = 1;
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
            if (d[1] != 0) {
                d[2] = 0;
                break;
            }
            d[2] = 1;
            put_position(&d[3], he_cancel_a);
            put_position(&d[5], he_cancel_b);
            break;
        case HE_VAL_UID: {
            memset(&d[1], 0, 12);
#ifdef HE_HAS_HARDWARE_ID
            hardware_id_t id = get_hardware_id();
            memcpy(&d[1], &id, sizeof(id) < 12 ? sizeof(id) : 12);
#endif
            break;
        }
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
            if (d[1] != 0) return;
            uint8_t a = (d[2] == 0xFF) ? HE_NO_SENSOR : he_sensor_at(d[2], d[3]);
            uint8_t b = (d[4] == 0xFF) ? HE_NO_SENSOR : he_sensor_at(d[4], d[5]);
            if ((a == HE_NO_SENSOR) != (b == HE_NO_SENSOR)) return;   // two keys, or none
            if (a != HE_NO_SENSOR && a == b) return;                    // a key cannot cancel itself
            he_cancel_a = a;
            he_cancel_b = b;
            cancel_reset_state();
            he_perkey_mark_dirty();
            break;
        }
    }
}

#else   // no VIA: the pair keeps its defaults and nothing is stored

bool he_perkey_layout_ok(void) { return true; }
bool he_perkey_carry_over(void) { return false; }
void he_perkey_defaults(void) {}
void he_perkey_load(void) {}
void he_perkey_get(uint8_t *d) { (void)d; }
void he_perkey_set(uint8_t *d) { (void)d; }
void he_perkey_mark_dirty(void) {}
void he_perkey_task(void) {}

#endif
