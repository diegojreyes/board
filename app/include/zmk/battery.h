/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

uint8_t zmk_battery_state_of_charge(void);
bool bat_is_shutdown(void);
bool bat_is_low(void);
void clear_bat_shutdown(void);
uint8_t get_battery_level(void);