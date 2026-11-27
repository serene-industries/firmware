/*
Copyright 2024 Matthijs Muller

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include QMK_KEYBOARD_H
#include "via.h"
#include "raw_hid.h"
#include "wait.h"
#include "bootloader.h"
#include <string.h>

// The fork's quantum/via.c dropped the standard id_bootloader_jump (0x0B)
// handler; restore it at the keyboard level so the configurator can reboot
// the board into its UF2 bootloader (same fix as the HE boards).
bool via_command_kb(uint8_t *data, uint8_t length) {
    if (data[0] == id_bootloader_jump) {
        raw_hid_send(data, length);
        wait_ms(100);
        bootloader_jump();
        return true;
    }
    return false;
}

// ---- board id: begin (tests/host/run.py compiles this block as it stands)
// What the board is, and which one it is, for the configurator: the same two questions the Hall-effect boards
// answer (he_perkey.h there), on VIA's custom channel, so one piece of the configurator reads every wired board.
//   get, value id 22 -> d[1] protocol 1, d[2] 0 (no Hall sensors), d[3] rows, d[4] columns,
//                       d[5] features: 0x04 this build can give the board's ID
//   get, value id 25 -> d[1..12] the MCU's unique ID (the RP2040's flash ID; zeros if this build has none)
// With its ID the configurator knows this very keyboard among others of the model, keeps its backups under it,
// and after a firmware update writes the keymap, the macros and the lighting back without asking. Anything else
// on this channel is answered "unhandled", as the core does for a board with no custom values.
#if defined(__has_include)
#    if __has_include("hardware_id.h")
#        include "hardware_id.h"
#        define HS_HAS_HARDWARE_ID
#    endif
#endif
#define HS_VAL_INFO 22
#define HS_VAL_UID 25
#define HS_FEATURE_BOARD_UID 0x04

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *d = &data[2];
    if (data[1] == id_custom_channel && data[0] == id_custom_get_value && length >= 16) {
        if (d[0] == HS_VAL_INFO) {
            d[1] = 1;
            d[2] = 0;
            d[3] = MATRIX_ROWS;
            d[4] = MATRIX_COLS;
#ifdef HS_HAS_HARDWARE_ID
            d[5] = HS_FEATURE_BOARD_UID;
#else
            d[5] = 0;
#endif
            return;
        }
        if (d[0] == HS_VAL_UID) {
            memset(&d[1], 0, 12);
#ifdef HS_HAS_HARDWARE_ID
            hardware_id_t id = get_hardware_id();
            memcpy(&d[1], &id, sizeof(id) < 12 ? sizeof(id) : 12);
#endif
            return;
        }
    }
    data[0] = id_unhandled;
}
// ---- board id: end

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

  [0] = LAYOUT(
    KC_ESC,  KC_1,    KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSLS, KC_DEL, KC_GRV, KC_MUTE,
      KC_TAB,  KC_Q,    KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSPC, KC_PGUP,
        KC_CAPS, KC_A,    KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L,    KC_SCLN, KC_QUOT, KC_ENT,           KC_PGDN,
        KC_LSFT,          KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT, KC_UP,   MO(1),
    KC_LCTL, KC_LGUI, KC_LALT,                KC_SPC,                    KC_RGUI, MO(1),   KC_LEFT, KC_DOWN, KC_RGHT
  ),

  [1] = LAYOUT(
    _______, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, _______, _______, RM_TOGG, _______,
    _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_HOME,
    _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_END,
    _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_MPLY, KC_VOLU, _______,
    _______, _______, _______, _______, _______, _______, KC_MPRV, KC_VOLD, KC_MNXT
  )
};

#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [1] = { ENCODER_CCW_CW(RM_SATD, RM_SATU) },
};
#endif
