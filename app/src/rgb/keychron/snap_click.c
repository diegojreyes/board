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

#include "snap_click.h"
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>

LOG_MODULE_REGISTER(snap,0);

extern int hid_listener_keycode_pressed(const struct zmk_keycode_state_changed *ev);
extern int hid_listener_keycode_released(const struct zmk_keycode_state_changed *ev);
int hid_listener_keycode_pressed_fix(const struct zmk_keycode_state_changed *ev)
{
    if (ev->keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL && ev->keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI)
    {
        uint8_t mod = ev->keycode - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL;
        if(!zmk_hid_mod_is_pressed(mod))
        {
            hid_listener_keycode_pressed(ev);
        }
    }
    else
    {
        hid_listener_keycode_pressed(ev);
    }
    return 0;
}

enum {
    SNAP_CLICK_TYPE_NONE = 0,
    SNAP_CLICK_TYPE_REGULAR,
    SNAP_CLICK_TYPE_LAST_INPUT,
    SNAP_CLICK_TYPE_FIRST_KEY,
    SNAP_CLICK_TYPE_SECOND_KEY,
    SNAP_CLICK_TYPE_NEUTRAL,
    SNAP_CLICK_TYPE_MAX,
};

snap_click_config_t snap_click_pair[SNAP_CLICK_COUNT];
static snap_click_state_t snap_click_state[SNAP_CLICK_COUNT];

void snap_click_config_reset(void) {
    memset(snap_click_pair, 0, sizeof(snap_click_pair));
    // eeprom_update_block(snap_click_pair, (uint8_t *)(EECONFIG_BASE_SNAP_CLICK), sizeof(snap_click_pair));
}

void snap_click_init(void) {
    // eeprom_read_block(snap_click_pair, (uint8_t *)(EECONFIG_BASE_SNAP_CLICK), sizeof(snap_click_pair));
    memset(snap_click_state, 0, sizeof(snap_click_state));
}

bool process_record_snap_click(const struct zmk_keycode_state_changed *ev)
{
    uint16_t keycode  = ev->keycode;
    struct zmk_keycode_state_changed event;
    memcpy(&event ,ev ,sizeof(event));
    for (uint8_t i=0; i<SNAP_CLICK_COUNT; i++)
    {
        snap_click_config_t *p = &snap_click_pair[i];
        
        if (p->type && (keycode == p->key[0] || keycode == p->key[1]))
        {
            snap_click_state_t *pState = &snap_click_state[i];
            uint8_t index = keycode == p->key[1];
            
            if (ev->state) {
                uint8_t state = 0x01 << index;
                
                if (pState->state == 0) {
                    // Single key down
                    pState->state_keys = pState->last_single_key = state;
                } else if ((state & pState->state_keys) == 0) {  // TODO: do we need checking?
                    pState->state_keys = 0x03;

                    switch (p->type) {
                        case SNAP_CLICK_TYPE_REGULAR:
                        case SNAP_CLICK_TYPE_LAST_INPUT:
                            // unregister_code(p->key[1-index]);
                            // register_code(p->key[index]);
                            event.keycode = p->key[1-index];
                            hid_listener_keycode_released(&event);
                            event.keycode = p->key[index];
                            hid_listener_keycode_pressed_fix(&event);
                            LOG_WRN("press,un:%x,set:%x",p->key[1-index],p->key[index]);

                            break;
                        case SNAP_CLICK_TYPE_FIRST_KEY:
                            // unregister_code(p->key[1]);
                            // register_code(p->key[0]);
                            event.keycode = p->key[1];
                            hid_listener_keycode_released(&event);
                            event.keycode =p->key[0];
                            hid_listener_keycode_pressed_fix(&event);
                            LOG_WRN("un:%x,set:%x",p->key[1],p->key[0]);
                            break;
                        case SNAP_CLICK_TYPE_SECOND_KEY:
                            // unregister_code(p->key[0]);
                            // register_code(p->key[1]);
                            event.keycode = p->key[0];
                            hid_listener_keycode_released(&event);
                            event.keycode = p->key[1];
                            hid_listener_keycode_pressed_fix(&event);
                            LOG_WRN("un:%x,set:%x",p->key[0],p->key[1]);
                            break;
                        case SNAP_CLICK_TYPE_NEUTRAL:
                            // unregister_code(p->key[1-index]);
                            event.keycode =p->key[1-index];
                            hid_listener_keycode_released(&event);
                            LOG_WRN("un:%x ",p->key[1-index]);
                
                            break;
                    }
                    return false;
                }
            } else {
                if (pState->state_keys == 0x03) {
                    // snap click active
                    uint8_t state = 0x01 << (1-index);
                    pState->state_keys = pState->last_single_key = state;
                
                    switch (p->type) {
                        case SNAP_CLICK_TYPE_REGULAR:
                            // unregister_code(p->key[index]);
                            event.keycode =p->key[index];
                            hid_listener_keycode_released(&event);
                            LOG_WRN("un:%x ",p->key[index]);
                            break;
                        case SNAP_CLICK_TYPE_LAST_INPUT:
  
                        case SNAP_CLICK_TYPE_FIRST_KEY:

                        case SNAP_CLICK_TYPE_SECOND_KEY:
                            // unregister_code(p->key[index]);
                            // register_code(p->key[1-index]);
                            event.keycode =p->key[index];
                            if(zmk_hid_keyboard_is_pressed(event.keycode))
                                hid_listener_keycode_released(&event);
                            event.keycode =p->key[1-index];
                            if(!zmk_hid_keyboard_is_pressed(event.keycode))
                                hid_listener_keycode_pressed_fix(&event);
                            LOG_WRN("release,un:%x,set:%x",p->key[index],p->key[1-index]);
                            break;
                        case SNAP_CLICK_TYPE_NEUTRAL:
                            // register_code(p->key[1-index]);
                            event.keycode =p->key[1-index];
                            hid_listener_keycode_pressed_fix(&event);
                            LOG_WRN("set:%x",p->key[1-index]);
                            break;
                    }

                    return false;
                } else {
                    pState->state = 0;
                }
            }
        }
    }


    return true;
}

static bool snap_click_get_info(uint8_t *data) {
    data[1] = SNAP_CLICK_COUNT;

    return true;
}

static bool snap_click_get(uint8_t *data) {
    uint8_t start = data[0];
    uint8_t count  = data[1];

    if (count > 9 || start + count > SNAP_CLICK_COUNT) return false;
    memcpy(&data[1], &snap_click_pair[start], count * sizeof(snap_click_config_t));

    return true;
}

static bool snap_click_set(uint8_t *data) {
    uint8_t start = data[0];
    uint8_t count  = data[1];

    if (count > 9 || start + count > SNAP_CLICK_COUNT) return false;
    for (uint8_t i=0; i<count; i++) {
        uint8_t offset = 2+sizeof(snap_click_config_t)*i;
        uint8_t type = data[offset];
        uint8_t keycode1 = data[offset+1];
        uint8_t keycode2 = data[offset+2];
        LOG_WRN("type:%d,keycode:%x,%x",type,keycode1,keycode2);
        if (type >= SNAP_CLICK_TYPE_MAX)
            return false;

        if (type != 0 && (keycode1 == 0 || keycode2 == 0))
            return false;
    }
    memcpy(&snap_click_pair[start], &data[2], count * sizeof(snap_click_config_t));

    return true;
}

static bool snap_click_save(uint8_t *data) {
    settings_save_one("keychron/snap_click_pair", (const void*)snap_click_pair,sizeof(snap_click_pair));

    return true;
}
enum {
    SNAP_CLICK_GET_INFO=7,
    SNAP_CLICK_GET,
    SNAP_CLICK_SET,
    SNAP_CLICK_SAVE,

};

void snap_click_rx(uint8_t *data, uint8_t length) {
    uint8_t cmd     = data[0];
    bool    success = true;

    switch (cmd) {
        case SNAP_CLICK_GET_INFO:
            success = snap_click_get_info(&data[1]);
            break;

        case SNAP_CLICK_GET:
            success = snap_click_get(&data[1]);
            break;

        case SNAP_CLICK_SET:
            success = snap_click_set(&data[1]);
            break;

        case SNAP_CLICK_SAVE:
            success = snap_click_save(&data[1]);
            break;

        default:
            data[0] = 0xFF;
            break;
    }

    data[1] = success ? 0 : 1;
}
