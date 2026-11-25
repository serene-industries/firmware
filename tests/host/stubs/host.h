// Host stand-ins for the QMK headers the HE board files include. Only what those files use.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---- eeprom: a byte array, same calls as QMK
extern uint8_t EE[4096];
uint8_t  eeprom_read_byte(const uint8_t *addr);
uint32_t eeprom_read_dword(const uint32_t *addr);
void     eeprom_read_block(void *buf, const void *addr, size_t len);
void     eeprom_update_dword(uint32_t *addr, uint32_t v);
void     eeprom_update_block(const void *buf, void *addr, size_t len);

// ---- eeconfig: the layout of quantum/eeconfig.h (QMK 0.25)
typedef struct __attribute__((packed)) {
    uint16_t magic; uint8_t debug; uint8_t default_layer; uint16_t keymap; uint8_t backlight; uint8_t audio;
    uint32_t rgblight; uint8_t unicode; uint8_t steno; uint8_t handedness; uint32_t keyboard; uint32_t user;
    union { uint32_t led_matrix; uint64_t rgb_matrix; }; uint32_t haptic; uint8_t rgblight_ext;
} eeprom_core_t;
#define EECONFIG_KEYBOARD (uint32_t *)(offsetof(eeprom_core_t, keyboard))
#define EECONFIG_USER (uint32_t *)(offsetof(eeprom_core_t, user))
#define EECONFIG_BASE_SIZE ((uint8_t)sizeof(eeprom_core_t))
#define EECONFIG_KB_DATA_VERSION (EECONFIG_KB_DATA_SIZE)
#define EECONFIG_USER_DATA_VERSION (EECONFIG_USER_DATA_SIZE)
#define EECONFIG_KB_DATABLOCK ((uint8_t *)(EECONFIG_BASE_SIZE))
#define EECONFIG_USER_DATABLOCK ((uint8_t *)((EECONFIG_BASE_SIZE) + (EECONFIG_KB_DATA_SIZE)))
#define EECONFIG_SIZE ((EECONFIG_BASE_SIZE) + (EECONFIG_KB_DATA_SIZE) + (EECONFIG_USER_DATA_SIZE))
void eeconfig_read_kb_datablock(void *data);
void eeconfig_update_kb_datablock(const void *data);
void eeconfig_read_user_datablock(void *data);
void eeconfig_update_user_datablock(const void *data);

// ---- timer: 16-bit milliseconds, as on the board
extern uint32_t host_now;
uint16_t timer_read(void);
uint32_t timer_read32(void);
uint16_t timer_elapsed(uint16_t last);
uint32_t timer_elapsed32(uint32_t last);

// ---- everything the handlers call that has no meaning on a host
#define uprintf(...) ((void)0)
#define print(s) ((void)0)
#define uprint(s) ((void)0)
#define dprintf(...) ((void)0)
#define wait_ms(x) ((void)0)
#define wait_us(x) ((void)0)
#define printf(...) ((void)0)   // the board files print debug text; the harness writes with fprintf

// ---- pins, ADC, lights: recorded or ignored
typedef int pin_t;
enum { A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15, B0, B1, B2, B3, B4, B5, B6, B7, B8, B9, B10, B11, B12, B13, B14, B15, HOST_PINS };
typedef int adc_mux;
#define PAL_MODE_INPUT_ANALOG 0
extern uint8_t host_pin[HOST_PINS];
#define palSetLineMode(p, m) ((void)0)
#define setPinOutput(p) ((void)0)
#define writePinHigh(p) (host_pin[(p)] = 1)
#define writePinLow(p) (host_pin[(p)] = 0)
#define writePin(p, v) (host_pin[(p)] = (v) ? 1 : 0)
#define readPin(p) (host_pin[(p)])
adc_mux  pinToMux(pin_t pin);
int16_t  adc_read(adc_mux mux);
void     raw_hid_send(uint8_t *data, uint8_t length);
void     bootloader_jump(void);

// lights and sliders (the *_rgb.h headers of the boards)
enum { SLIDER_TYPE_NONE, SLIDER_TYPE_ACTUATION, SLIDER_TYPE_RELEASE, SLIDER_TYPE_RTP_DEADZONE, SLIDER_TYPE_RTP_ENGAGE, SLIDER_TYPE_RTP_DISENGAGE };
extern uint8_t last_moved_slider, current_slider_type, latest_slider_value;
extern bool    slider_active;
void start_slider_visualization(uint8_t value);
void start_calibration_rgb(void);
void end_calibration_visual(void);
void calibration_warning(void);
void update_calibration_rgb(uint8_t sensor, uint16_t value);
void apply_calibration_changes_rgb(void);

// VIA ids (quantum/via.h)
enum { id_custom_set_value = 0x07, id_custom_get_value = 0x08, id_custom_save = 0x09, id_bootloader_jump = 0x0B, id_unhandled = 0xFF };
enum { id_custom_channel = 0 };
void suspend_power_down_user(void);
void suspend_wakeup_init_user(void);
