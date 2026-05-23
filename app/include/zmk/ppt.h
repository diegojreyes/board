/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

enum zmk_ppt_conn_state {
    ZMK_PPT_CONN_NONE,
    ZMK_PPT_CONN,
};

// int zmk_ppt_send_keyboard_report(void);
// int zmk_ppt_send_consumer_report(void);
int zmk_ppt_send_keyboard_report(uint8_t *report, uint8_t len);
int zmk_ppt_send_consumer_report(uint8_t *report, uint8_t len);
int zmk_ppt_send_mouse_report(uint8_t *report, uint8_t len);
