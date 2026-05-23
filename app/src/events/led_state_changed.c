/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zmk/events/led_state_changed.h>

ZMK_EVENT_IMPL(zmk_led_state_changed);