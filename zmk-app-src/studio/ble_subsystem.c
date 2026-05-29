/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk_studio, CONFIG_ZMK_STUDIO_LOG_LEVEL);

#include <zmk/studio/rpc.h>
#include <zmk/ble.h>

/* External function from bt_indicator.c for LED flash on clear */
extern void bt_indicator_show_clear(void);

ZMK_RPC_SUBSYSTEM(ble)

#define BLE_RESPONSE(type, ...) ZMK_RPC_RESPONSE(ble, type, __VA_ARGS__)

zmk_studio_Response get_ble_state(const zmk_studio_Request *req) {
    LOG_DBG("get_ble_state called");

    zmk_ble_BleState resp = zmk_ble_BleState_init_zero;

    // Get active profile index
    int active_idx = zmk_ble_active_profile_index();
    resp.active_profile = (active_idx >= 0) ? active_idx : 0;

    // Get active profile connection status
    resp.connected = zmk_ble_active_profile_is_connected();

    // Get profile count
    resp.profile_count = ZMK_BLE_PROFILE_COUNT;

    // Populate per-profile status
    // Profile 0
    if (ZMK_BLE_PROFILE_COUNT > 0) {
        resp.profile0_connected = zmk_ble_profile_is_connected(0);
        resp.profile0_open = zmk_ble_profile_is_open(0);
    }
    // Profile 1
    if (ZMK_BLE_PROFILE_COUNT > 1) {
        resp.profile1_connected = zmk_ble_profile_is_connected(1);
        resp.profile1_open = zmk_ble_profile_is_open(1);
    }
    // Profile 2
    if (ZMK_BLE_PROFILE_COUNT > 2) {
        resp.profile2_connected = zmk_ble_profile_is_connected(2);
        resp.profile2_open = zmk_ble_profile_is_open(2);
    }
    // Profile 3
    if (ZMK_BLE_PROFILE_COUNT > 3) {
        resp.profile3_connected = zmk_ble_profile_is_connected(3);
        resp.profile3_open = zmk_ble_profile_is_open(3);
    }
    // Profile 4
    if (ZMK_BLE_PROFILE_COUNT > 4) {
        resp.profile4_connected = zmk_ble_profile_is_connected(4);
        resp.profile4_open = zmk_ble_profile_is_open(4);
    }

    return BLE_RESPONSE(get_ble_state, resp);
}

ZMK_RPC_SUBSYSTEM_HANDLER(ble, get_ble_state, ZMK_STUDIO_RPC_HANDLER_SECURED);

/**
 * Select a BLE profile (0-4)
 * This will trigger LED indication via bt_indicator.c
 */
zmk_studio_Response select_profile(const zmk_studio_Request *req) {
    uint32_t profile_idx = req->subsystem.ble.request_type.select_profile;

    LOG_INF("select_profile called: %d", profile_idx);

    // Validate profile index
    if (profile_idx >= ZMK_BLE_PROFILE_COUNT) {
        LOG_WRN("Invalid profile index: %d (max %d)", profile_idx, ZMK_BLE_PROFILE_COUNT - 1);
        return BLE_RESPONSE(select_profile, false);
    }

    int ret = zmk_ble_prof_select(profile_idx);
    if (ret < 0) {
        LOG_ERR("Failed to select profile %d: %d", profile_idx, ret);
        return BLE_RESPONSE(select_profile, false);
    }

    LOG_INF("Successfully selected profile %d", profile_idx);
    return BLE_RESPONSE(select_profile, true);
}

ZMK_RPC_SUBSYSTEM_HANDLER(ble, select_profile, ZMK_STUDIO_RPC_HANDLER_SECURED);

/**
 * Clear the current BLE profile pairing
 * This triggers LED indication via bt_indicator.c (bt_indicator_show_clear)
 */
zmk_studio_Response clear_profile(const zmk_studio_Request *req) {
    LOG_INF("clear_profile called");

    // Clear bonds for current profile
    zmk_ble_clear_bonds();

    // Trigger LED flash pattern to indicate clear
    bt_indicator_show_clear();

    LOG_INF("Successfully cleared current profile");
    return BLE_RESPONSE(clear_profile, true);
}

ZMK_RPC_SUBSYSTEM_HANDLER(ble, clear_profile, ZMK_STUDIO_RPC_HANDLER_SECURED);

/**
 * Clear ALL BLE profile pairings - DANGEROUS operation
 * This will unpair all connected devices
 */
zmk_studio_Response clear_all_profiles(const zmk_studio_Request *req) {
    LOG_WRN("clear_all_profiles called - clearing ALL BLE pairings!");

    // Clear all bonds
    zmk_ble_clear_all_bonds();

    // Trigger LED flash pattern to indicate clear
    bt_indicator_show_clear();

    LOG_INF("Successfully cleared all profiles");
    return BLE_RESPONSE(clear_all_profiles, true);
}

ZMK_RPC_SUBSYSTEM_HANDLER(ble, clear_all_profiles, ZMK_STUDIO_RPC_HANDLER_SECURED);

static int ble_event_mapper(const zmk_event_t *eh, zmk_studio_Notification *n) {
    return -ENOTSUP;
}

ZMK_RPC_EVENT_MAPPER(ble, ble_event_mapper);
