/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * BT Profile Indicator - Uses dedicated status LEDs for BT indication
 * LED_CON (P1.02) - Blinks to show profile number (1-5 blinks)
 * LED_BOOT (P1.00) - Solid when connected
 *
 * Also shows rapid flash pattern when BT profile is cleared.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* GPIO pins for status LEDs - accent LED active-low */
#define LED_CON_NODE DT_NODELABEL(gpio1)
#define LED_CON_PIN 2   /* P1.02 - active low */
#define LED_BOOT_PIN 0  /* P1.00 - active low */

/* Try both polarities - set to 1 if LEDs are active-low (common anode) */
#define LEDS_ACTIVE_LOW 1

#define BLINK_ON_MS 200
#define BLINK_OFF_MS 200
#define BLINK_END_PAUSE_MS 800

/* Fast flash for clear indication - quick burst pattern */
#define CLEAR_FLASH_ON_MS 80
#define CLEAR_FLASH_OFF_MS 80
#define CLEAR_FLASH_COUNT 5

static const struct device *gpio_dev;
static struct k_work_delayable indicator_work;
static int blink_count;
static int current_blink;
static bool led_is_on;
static bool is_connected;
static bool is_clear_mode;  /* true = fast flash for clear, false = normal profile blink */

static void set_led_con(bool on) {
    if (gpio_dev) {
#if LEDS_ACTIVE_LOW
        gpio_pin_set(gpio_dev, LED_CON_PIN, on ? 0 : 1);
#else
        gpio_pin_set(gpio_dev, LED_CON_PIN, on ? 1 : 0);
#endif
    }
}

static void set_led_boot(bool on) {
    if (gpio_dev) {
#if LEDS_ACTIVE_LOW
        gpio_pin_set(gpio_dev, LED_BOOT_PIN, on ? 0 : 1);
#else
        gpio_pin_set(gpio_dev, LED_BOOT_PIN, on ? 1 : 0);
#endif
    }
}

static void indicator_work_handler(struct k_work *work) {
    int on_time = is_clear_mode ? CLEAR_FLASH_ON_MS : BLINK_ON_MS;
    int off_time = is_clear_mode ? CLEAR_FLASH_OFF_MS : BLINK_OFF_MS;

    if (current_blink >= blink_count) {
        /* Done blinking - turn off LED_CON, keep LED_BOOT showing connection state */
        set_led_con(false);
        set_led_boot(is_connected);
        is_clear_mode = false;
        return;
    }

    if (led_is_on) {
        /* Turn off LEDs */
        set_led_con(false);
        if (is_clear_mode) {
            set_led_boot(false);
        }
        led_is_on = false;
        current_blink++;

        if (current_blink >= blink_count) {
            /* All blinks done, pause then finish */
            k_work_schedule(&indicator_work, K_MSEC(BLINK_END_PAUSE_MS));
        } else {
            /* Schedule next on */
            k_work_schedule(&indicator_work, K_MSEC(off_time));
        }
    } else {
        /* Turn on LEDs */
        set_led_con(true);
        if (is_clear_mode) {
            set_led_boot(true);  /* Both LEDs flash for clear */
        }
        led_is_on = true;
        /* Schedule off */
        k_work_schedule(&indicator_work, K_MSEC(on_time));
    }
}

static void start_bt_indicator(int profile_index, bool connected) {
    if (!gpio_dev) {
        return;
    }

    /* Cancel any ongoing indication */
    k_work_cancel_delayable(&indicator_work);

    /* Update connection state */
    is_connected = connected;
    is_clear_mode = false;

    /* Show connection state on boot LED */
    set_led_boot(connected);

    /* Profile index is 0-based, blink (index + 1) times */
    blink_count = profile_index + 1;
    current_blink = 0;
    led_is_on = false;

    LOG_INF("BT indicator: profile %d, connected=%d, blinks=%d",
            profile_index, connected, blink_count);

    /* Start blinking LED_CON */
    k_work_schedule(&indicator_work, K_MSEC(50));
}

/* Called when BT profile is cleared - rapid flash both LEDs
 * This function overrides the weak symbol in behavior_bt.c */
void __attribute__((used)) bt_indicator_show_clear(void) {
    if (!gpio_dev) {
        return;
    }

    /* Cancel any ongoing indication */
    k_work_cancel_delayable(&indicator_work);

    is_clear_mode = true;
    is_connected = false;
    blink_count = CLEAR_FLASH_COUNT;
    current_blink = 0;
    led_is_on = false;

    LOG_INF("BT indicator: showing clear pattern");

    /* Start rapid flash */
    k_work_schedule(&indicator_work, K_MSEC(50));
}

static uint8_t last_profile_index = 0xFF;
static bool last_connected_state = false;

static int bt_indicator_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev = as_zmk_ble_active_profile_changed(eh);

    if (ev) {
        bool connected = zmk_ble_active_profile_is_connected();

        /* Detect if profile was cleared: same profile, was connected, now not connected */
        if (ev->index == last_profile_index && last_connected_state && !connected) {
            /* Profile was likely cleared - show clear pattern */
            bt_indicator_show_clear();
        } else {
            /* Normal profile change */
            start_bt_indicator(ev->index, connected);
        }

        last_profile_index = ev->index;
        last_connected_state = connected;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bt_indicator, bt_indicator_listener);
ZMK_SUBSCRIPTION(bt_indicator, zmk_ble_active_profile_changed);

static int bt_indicator_init(void) {
    gpio_dev = DEVICE_DT_GET(LED_CON_NODE);

    if (!device_is_ready(gpio_dev)) {
        LOG_WRN("GPIO1 device not ready for BT indicator");
        gpio_dev = NULL;
        return 0;
    }

    /* Configure LED pins as outputs, start inactive */
    gpio_pin_configure(gpio_dev, LED_CON_PIN, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(gpio_dev, LED_BOOT_PIN, GPIO_OUTPUT_INACTIVE);

    /* Turn off LEDs initially (respecting polarity) */
    set_led_con(false);
    set_led_boot(false);

    k_work_init_delayable(&indicator_work, indicator_work_handler);

    LOG_INF("BT indicator initialized (LED_CON=P1.%02d, LED_BOOT=P1.%02d, active_low=%d)",
            LED_CON_PIN, LED_BOOT_PIN, LEDS_ACTIVE_LOW);

    /* Do a quick test blink on startup */
    set_led_con(true);
    set_led_boot(true);
    k_msleep(100);
    set_led_con(false);
    set_led_boot(false);

    return 0;
}

SYS_INIT(bt_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
