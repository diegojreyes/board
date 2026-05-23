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

#include <string.h>

#include "retail_demo.h"
#include "../rgb_matrix_types.h"
#include <zmk/behavior.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <dt-bindings/zmk/rgb.h>
#include "../rgb_matrix.h"

LOG_MODULE_REGISTER(demo,4);

#define RGB_MATRIX_ENABLE
#ifndef RETAIL_DEMO_KEY_1
#    ifdef RGB_MATRIX_ENABLE
#        define RETAIL_DEMO_KEY_1 RGB_HUI_CMD
#    else
#        define RETAIL_DEMO_KEY_1 KC_D
#    endif
#endif

#ifndef RETAIL_DEMO_KEY_2
#    ifdef RGB_MATRIX_ENABLE
#        define RETAIL_DEMO_KEY_2 RGB_HUD_CMD
#    else
#        define RETAIL_DEMO_KEY_2 KC_E
#    endif
#endif

#ifndef EFFECT_DURATION
#    define EFFECT_DURATION 10000
#endif

enum {
    KEY_PRESS_FN          = 0x01 << 0,
    KEY_PRESS_D           = 0x01 << 1,
    KEY_PRESS_E           = 0x01 << 2,
    KEY_PRESS_RETAIL_DEMO = KEY_PRESS_FN | KEY_PRESS_D | KEY_PRESS_E,
};
void cancel_save_rgb_work(void);

extern uint8_t  retail_demo_enable ;
static uint8_t  retail_demo_combo  = 0;
// static uint32_t retail_demo_timer  = 0;

extern void rgb_save_retail_demo(void);

void check_timer_cb(struct k_work *work)
{
    if (retail_demo_combo == KEY_PRESS_RETAIL_DEMO) {
        retail_demo_combo  = 0;
        retail_demo_enable = !retail_demo_enable;
        LOG_ERR("retail_demo_enable:%d",retail_demo_enable);
        if (retail_demo_enable) {
            if (zmk_rgb_matrix_get_mode() != RGB_EFFECT_MIXED_RGB) retail_demo_start();
        } else {
            settings_load_subtree("rgb");
            LOG_ERR("reset rgb mode:%d",zmk_rgb_matrix_get_mode());
        }
        rgb_save_retail_demo();
    }
}
K_WORK_DELAYABLE_DEFINE(check_timer_work,check_timer_cb);

bool process_record_retail_demo(struct zmk_behavior_binding *binding, bool pressed)
{
    if(memcmp(binding->behavior_dev,"momentary_layer",15)==0)
    {
        LOG_ERR("mo:%d",binding->param1);
        if (pressed)
            retail_demo_combo |= KEY_PRESS_FN;
        else
            retail_demo_combo &= ~KEY_PRESS_FN;
    }
#ifdef RGB_MATRIX_ENABLE
    else if(memcmp(binding->behavior_dev,"rgb_ug",6)==0)
#else    
    else if(memcmp(binding->behavior_dev,"key_press",9)==0)
#endif         
    {
        LOG_ERR("binding:%s,code:%d",binding->behavior_dev,binding->param1);
        if(binding->param1 ==RETAIL_DEMO_KEY_1 )
        {

            if (pressed) {
                retail_demo_combo |= KEY_PRESS_D;
                if (retail_demo_combo == KEY_PRESS_RETAIL_DEMO) 
                {
                    
                    cancel_save_rgb_work();
                    k_work_reschedule(&check_timer_work,K_MSEC(5000));
                    return false;
                }
            } else {
                retail_demo_combo &= ~KEY_PRESS_D;
                k_work_cancel_delayable(&check_timer_work);
            }
        }
        else if(binding->param1 ==RETAIL_DEMO_KEY_2 )
        {

            if (pressed) {
                retail_demo_combo |= KEY_PRESS_E;
                if (retail_demo_combo == KEY_PRESS_RETAIL_DEMO) {
                    cancel_save_rgb_work();
                    k_work_reschedule(&check_timer_work,K_MSEC(5000));
                    return false;
                }
            } else {
                retail_demo_combo &= ~KEY_PRESS_E;
                k_work_cancel_delayable(&check_timer_work);
            }
        }
        if (retail_demo_enable && binding->param1 >= RGB_TOG_CMD && binding->param1 <= RGB_SPD_CMD) return false;

    
    }
    return true;

}
// bool process_record_retail_demo(struct zmk_behavior_binding *binding, bool pressed)
// {
//     uint8_t key =0;
//     if(memcmp(binding->behavior_dev,"momentary_layer",15)==0)
//     {
//         LOG_ERR("mo:%d",binding->param1);
//         key = KEY_PRESS_FN
//     }
// #ifdef RGB_MATRIX_ENABLE
//     else if(memcmp(binding->behavior_dev,"rgb_ug",6)==0)
// #else    
//     else if(memcmp(binding->behavior_dev,"key_press",9)==0)
// #endif         
//     {
//         LOG_ERR("binding:%s,code:%d",binding->behavior_dev,binding->param1);
//         if(binding->param1 ==RETAIL_DEMO_KEY_1 )
//         {
//             key = KEY_PRESS_D;            
//         }
//         else if(binding->param1 ==RETAIL_DEMO_KEY_2 )
//         {
//             key = KEY_PRESS_E;            
//         }    
//         if (retail_demo_enable && binding->param1 >= RGB_TOG_CMD && binding->param1 <= RGB_SPD_CMD) return false;
//     }
//     if(key == 0) return true;
//     if(pressed)
//     {
//         if (key == KEY_PRESS_FN )
//             retail_demo_combo |= KEY_PRESS_FN;
//         else if(key == KEY_PRESS_D)
//             retail_demo_combo |= KEY_PRESS_D;
//         else if(key == KEY_PRESS_E)
//             retail_demo_combo |= KEY_PRESS_E;
//         if (retail_demo_combo == KEY_PRESS_RETAIL_DEMO) 
//         {
//             cancel_save_rgb_work();
//             k_work_reschedule(&check_timer_work,K_MSEC(5000));
//             return false;
//         }
//     }
//     else {
//         if (key == KEY_PRESS_FN )
//             retail_demo_combo &= ~KEY_PRESS_FN;
//         else if(key == KEY_PRESS_D)
//             retail_demo_combo &= ~KEY_PRESS_D;
//         else if(key == KEY_PRESS_E)
//             retail_demo_combo &= ~KEY_PRESS_E;
//         k_work_cancel_delayable(&check_timer_work);
//     }
//     return true;
// }

void retail_demo_start(void) {
    extern bool mixed_rgb_set_regions(uint8_t * data);
    extern bool mixed_rgb_set_effect_list(uint8_t * data);

    uint8_t index      = 0;
    uint8_t this_count = 28;
    uint8_t data[31]   = {0};

    // Set all LED to region 0
    while (index < RGB_MATRIX_LED_COUNT - 1) {
        memset(data, 0, 31);

        if ((index + this_count) >= RGB_MATRIX_LED_COUNT)
            this_count = RGB_MATRIX_LED_COUNT - 1 - index;
        else
            this_count = 28;

        data[0] = index;
        data[1] = this_count;
        mixed_rgb_set_regions(data);

        index += this_count;
    }

    uint8_t effect_list[5] = {4, 7, 8, 11, 14};
    // Set effect list
    for (uint8_t i = 0; i < 5; i++) {
        data[0] = 0;              // regsion
        data[1] = i;              // start
        data[2] = 1;              // count
        data[3] = effect_list[i]; // effect
        data[4] = 0;              // hue
        data[5] = 255;            // sat
        data[6] = 127;            // speed;
        data[7] = EFFECT_DURATION & 0xFF;
        data[8] = (EFFECT_DURATION >> 8) & 0xFF;
        data[9] = (EFFECT_DURATION >> 16) & 0xFF;
        data[10] = (EFFECT_DURATION >> 24) & 0xFF;

        mixed_rgb_set_effect_list(data);
    }

    HSV hsv = zmk_rgb_matrix_get_hsv();
    hsv.v = hsv.s = UINT8_MAX;
    zmk_rgb_matrix_sethsv_no_save(hsv.h, hsv.s, hsv.v);
    zmk_rgb_matrix_set_speed_no_save(RGB_MATRIX_DEFAULT_SPD);
    zmk_rgb_matrix_mode_no_save(RGB_EFFECT_MIXED_RGB);
    LOG_ERR("retail demo start");
}

void retail_demo_stop(void) {
    retail_demo_enable = false;
    rgb_save_retail_demo();
    settings_load_subtree("rgb");
}
