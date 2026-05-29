/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Appends a unique suffix (last 4 hex chars of device ID) to the BLE name
 * so multiple Icebreaker keyboards can be distinguished during pairing.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define BASE_NAME CONFIG_ZMK_KEYBOARD_NAME
#define MAX_NAME_LEN CONFIG_BT_DEVICE_NAME_MAX

static void set_unique_ble_name(void) {
    uint8_t device_id[16];
    ssize_t id_len;
    char new_name[MAX_NAME_LEN + 1];

    /* Get the hardware device ID */
    id_len = hwinfo_get_device_id(device_id, sizeof(device_id));
    if (id_len <= 0) {
        LOG_WRN("Failed to get device ID, using default name");
        return;
    }

    /* Build new name with last 4 hex chars of device ID */
    /* Format: "Icebreaker Wireless XXXX" */
    int base_len = strlen(BASE_NAME);
    if (base_len + 5 > MAX_NAME_LEN) {
        /* Name too long, truncate base name */
        base_len = MAX_NAME_LEN - 5;
    }

    memcpy(new_name, BASE_NAME, base_len);
    new_name[base_len] = ' ';

    /* Use last 2 bytes (4 hex chars) of device ID */
    uint8_t b1 = device_id[id_len - 2];
    uint8_t b2 = device_id[id_len - 1];

    snprintf(&new_name[base_len + 1], 5, "%02X%02X", b1, b2);
    new_name[base_len + 5] = '\0';

    LOG_INF("Setting BLE name to: %s", new_name);

    /* Set the BLE device name */
    int err = bt_set_name(new_name);
    if (err) {
        LOG_ERR("Failed to set BLE name (err %d)", err);
    }
}

/* Settings handler to set name after BLE settings are loaded */
static int icebreaker_name_settings_set(const char *name, size_t len,
                                        settings_read_cb read_cb, void *cb_arg) {
    return 0;
}

static int icebreaker_name_settings_commit(void) {
    /* Called after all settings are loaded - BLE should be ready */
    set_unique_ble_name();
    return 0;
}

/* Register settings handler with name that sorts after "ble" */
static struct settings_handler icebreaker_name_handler = {
    .name = "icebreaker",
    .h_set = icebreaker_name_settings_set,
    .h_commit = icebreaker_name_settings_commit,
};

static int icebreaker_ble_name_init(void) {
    int err = settings_register(&icebreaker_name_handler);
    if (err) {
        LOG_ERR("Failed to register settings handler (err %d)", err);
        return err;
    }
    return 0;
}

/* Run early to register settings handler before settings_load() */
SYS_INIT(icebreaker_ble_name_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
