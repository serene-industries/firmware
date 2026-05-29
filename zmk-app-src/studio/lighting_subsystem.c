/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk_studio, CONFIG_ZMK_STUDIO_LOG_LEVEL);

#include <zmk/studio/rpc.h>
#include <zmk/rgb_underglow.h>

ZMK_RPC_SUBSYSTEM(lighting)

#define LIGHTING_RESPONSE(type, ...) ZMK_RPC_RESPONSE(lighting, type, __VA_ARGS__)

// External declarations for RGB underglow state access
// These are defined in rgb_underglow.c but not exposed in header
extern int zmk_rgb_underglow_get_state(bool *state);

// We need to access the internal state - add a getter function
// For now, we'll use the public API to get what we can

zmk_studio_Response get_rgb_state(const zmk_studio_Request *req) {
    LOG_DBG("get_rgb_state called");

    zmk_lighting_RgbState resp = zmk_lighting_RgbState_init_zero;

    // Get on/off state
    bool on_state = false;
    zmk_rgb_underglow_get_state(&on_state);
    resp.on = on_state;

    // Unfortunately the current RGB API doesn't expose getters for HSB values
    // We would need to add those to the firmware
    // For now, return default values - the actual state will be read when we add proper getters
    resp.hue = 0;
    resp.saturation = 100;
    resp.brightness = 100;
    resp.effect = 0;
    resp.speed = 50;

    return LIGHTING_RESPONSE(get_rgb_state, resp);
}

zmk_studio_Response set_rgb_state(const zmk_studio_Request *req) {
    LOG_DBG("set_rgb_state called");

    const zmk_lighting_RgbState *state = &req->subsystem.lighting.request_type.set_rgb_state;

    // Set HSB color
    struct zmk_led_hsb color = {
        .h = (uint16_t)state->hue,
        .s = (uint8_t)state->saturation,
        .b = (uint8_t)state->brightness,
    };

    int ret = zmk_rgb_underglow_set_hsb(color);
    if (ret < 0) {
        LOG_ERR("Failed to set RGB HSB: %d", ret);
        return LIGHTING_RESPONSE(set_rgb_state, false);
    }

    // Set effect
    ret = zmk_rgb_underglow_select_effect(state->effect);
    if (ret < 0) {
        LOG_ERR("Failed to set RGB effect: %d", ret);
    }

    // Set on/off state
    if (state->on) {
        zmk_rgb_underglow_on();
    } else {
        zmk_rgb_underglow_off();
    }

    return LIGHTING_RESPONSE(set_rgb_state, true);
}

ZMK_RPC_SUBSYSTEM_HANDLER(lighting, get_rgb_state, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(lighting, set_rgb_state, ZMK_STUDIO_RPC_HANDLER_SECURED);

static int lighting_event_mapper(const zmk_event_t *eh, zmk_studio_Notification *n) {
    return -ENOTSUP;
}

ZMK_RPC_EVENT_MAPPER(lighting, lighting_event_mapper);
