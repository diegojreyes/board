/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

#include <zmk/event_manager.h>


struct zmk_led_state_changed {
    uint8_t led_state;
    uint8_t transport;
    uint8_t index;
};

ZMK_EVENT_DECLARE(zmk_led_state_changed);