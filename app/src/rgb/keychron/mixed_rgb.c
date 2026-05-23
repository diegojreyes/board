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

#include "keychron_rgb_type.h"
#include "../rgb_matrix_types.h"
#include "rgb_matrix_kb_config.h"
#include <zephyr/kernel.h>

extern rgb_effect_func rgb_effect_funcs[];
// PER_KEY_RGB data
extern uint8_t per_key_rgb_type;

// MIXED_RGB data
extern uint8_t rgb_regions[RGB_MATRIX_LED_COUNT];
uint8_t regions[RGB_MATRIX_LED_COUNT] = {0}; //
effect_config_t effect_list[EFFECT_LAYERS][EFFECTS_PER_LAYER];

uint8_t layer_effect_count[EFFECT_LAYERS] = {0};
uint8_t layer_effect_index[EFFECT_LAYERS] = {0};
uint32_t layer_effect_timer[EFFECT_LAYERS] = {0};

// Typing heatmap
uint8_t typingHeatmap = 0;

static bool multiple_rgb_effect_runner(effect_params_t *params);

void mixed_rgb_reset(void) {
    typingHeatmap = 0;
    for (uint8_t i = 0; i < EFFECT_LAYERS; i++) {
        layer_effect_index[i] = 0;
        layer_effect_timer[i] = k_uptime_get();

        if (effect_list[i][0].effect == RGB_EFFECT_TYPING_HEATMAP)
            typingHeatmap |= 0x01 << i;
    }
}

void update_mixed_rgb_effect_count(void) {
    for (int8_t layer = 0; layer < EFFECT_LAYERS; layer++) {
        layer_effect_count[layer] = 0;
        for (uint8_t i = 0; i < EFFECTS_PER_LAYER; i++) {
            if (effect_list[layer][i].effect != 0)
                ++layer_effect_count[layer];
        }
    }

    mixed_rgb_reset();
}

bool mixed_rgb(effect_params_t *params) {

    bool ret;

    extern uint8_t rgb_regions[RGB_MATRIX_LED_COUNT];
    if (params->init) {
        memcpy(rgb_regions, regions, RGB_MATRIX_LED_COUNT);
        memset(layer_effect_index, 0, sizeof(layer_effect_index));

        mixed_rgb_reset();
    }

    for (int8_t i = EFFECT_LAYERS - 1; i >= 0; i--) {
        params->region = i;
        ret = multiple_rgb_effect_runner(params);
    }

    return ret;
}

#define TRANSITION_TIME 1000

bool multiple_rgb_effect_runner(effect_params_t *params) {
    HSV hsv = zmk_rgb_matrix_get_hsv();
    uint8_t backup_value = hsv.v;

    bool transation = false;
    bool rendering = false;
    uint8_t layer = params->region;

    uint8_t effect_index = layer_effect_index[layer];

    if (effect_list[layer][effect_index].effect == RGB_EFFECT_TYPING_HEATMAP)
        typingHeatmap |= 0x01 << layer;
    else
        typingHeatmap &= ~(0x01 << layer);

    uint8_t last_effect = effect_list[layer][layer_effect_index[layer]].effect;

    if (layer_effect_count[layer] > 1) {
        if (k_uptime_delta_32(layer_effect_timer[layer]) > effect_list[layer][effect_index].time) {
            layer_effect_timer[layer] = k_uptime_get_32();
            if (++layer_effect_index[layer] >= EFFECTS_PER_LAYER)
                layer_effect_index[layer] = 0;

            effect_index = layer_effect_index[layer];

            if (effect_list[layer][effect_index].time == 0)
                return true; //
        } else if (k_uptime_delta_32(layer_effect_timer[layer]) >
                   effect_list[layer][effect_index].time - TRANSITION_TIME) {
            hsv.v = backup_value *
                    (effect_list[layer][effect_index].time -
                     k_uptime_delta_32(layer_effect_timer[layer])) /
                    TRANSITION_TIME;
            transation = true;
        }

        if (k_uptime_delta_32(layer_effect_timer[layer]) < TRANSITION_TIME) {
            hsv.v = backup_value * k_uptime_delta_32(layer_effect_timer[layer]) / TRANSITION_TIME;
            transation = true;
        }
    } else if (layer_effect_count[layer] == 1 && effect_list[layer][effect_index].effect == 0) {
        for (uint8_t i = 0; i < EFFECTS_PER_LAYER; i++) {
            if (effect_list[layer][i].effect != 0) {
                effect_index = layer_effect_index[params->region] = i;
                break;
            }
        }
    }

    uint8_t effect = effect_list[layer][effect_index].effect;
    if (effect == 0)
        ++layer_effect_index[layer]; // Skip effect 0
    if (layer_effect_index[layer] >= EFFECTS_PER_LAYER)
        layer_effect_index[layer] = 0;

    effect = effect_list[layer][effect_index].effect;
    hsv.h = effect_list[layer][effect_index].hue;
    hsv.s = effect_list[layer][effect_index].sat;
    zmk_rgb_matrix_sethsv_no_save(hsv.h, hsv.s, hsv.v);

    zmk_rgb_matrix_set_speed_no_save(effect_list[layer][effect_index].speed);

    params->init = last_effect != effect;

    if (effect < 0x30) /// UINT8_MAX)
    {
        rendering = rgb_effect_funcs[effect](params);
    }

    if (transation) {
        zmk_rgb_matrix_sethsv_no_save(hsv.h, hsv.s, backup_value);
    }

    return rendering;
}

void process_rgb_matrix_kb(uint8_t row, uint8_t col, bool pressed) {
    if (pressed) {
        if (rgb_matrix_config.mode == RGB_EFFECT_MIXED_RGB) {
            extern void process_rgb_matrix_typing_heatmap(uint8_t row, uint8_t col);
            if (typingHeatmap)
                process_rgb_matrix_typing_heatmap(row, col);
        }
    }
}
