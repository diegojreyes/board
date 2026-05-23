/* Copyright 2024 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
// #include "color.h"
#ifndef __KEYCHRON_RGB_TYPE_H__
#define __KEYCHRON_RGB_TYPE_H__
#include <stdint.h>
#include "../rgb_matrix.h"
enum {
    PER_KEY_RGB_SOLID,
    PER_KEY_RGB_BREATHING,
    PER_KEY_RGB_REATIVE_SIMPLE,
    PER_KEY_RGB_REATIVE_MULTI_WIDE,
    PER_KEY_RGB_REATIVE_SPLASH,
    PER_KEY_RGB_MAX,
};

typedef struct {
    uint8_t effect;
    uint8_t hue;
    uint8_t sat;
    uint8_t speed;
    uint32_t time;
} __attribute__((packed)) effect_config_t;

typedef union {
    uint8_t raw;
    struct {
        uint8_t num_lock : 1;
        uint8_t caps_lock : 1;
        uint8_t scroll_lock : 1;
        uint8_t compose : 1;
        uint8_t kana : 1;
        uint8_t reserved : 3;
    };
} os_led_t;

// TODO:
// typedef struct PACKED HSV2 {
//     uint8_t h;
//     uint8_t s;
//     uint8_t v;
// } HSV2;

typedef struct {
    os_led_t disable;
    HSV hsv;
} __attribute__((packed)) os_indicator_config_t;

#endif