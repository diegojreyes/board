/* Copyright 2017 Jason Williams (Wilba)
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

#include "dynamic_keymap.h"
#include "keycodes.h"
#include "send_string.h"
#include "launcher.h"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zmk/endpoints.h>
#include <drivers/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
uint8_t behavior_queue_is_full(void);
void send_string_end(void);
void macro_timer_expiry(struct k_timer *timer);
void macro_exec(void);

K_TIMER_DEFINE(macro_timer, macro_timer_expiry, NULL);
static uint16_t p_macro;
enum {
    SAVE_LAUNCHER_LAYOUT = 0x01,
    SAVE_LAUNCHER_ENCODER = 0x02,
    SAVE_LAUNCHER_CUSTOM = 0x04,
    SAVE_LAUNCHER_MACROS = 0x08,
    SAVE_LAUNCHER_KEYMAPS = 0x10,
    SAVE_LAUNCHER_CHANGED = 0x20,
    SAVE_LAUNCHER_KEYMAPS_RESET = 0x40,
    SAVE_LAUNCHER_MACROS_RESET = 0x80,
    SAVE_LAUNCHER_DEBOUNCE = 0x20,
    SAVE_LAUNCHER_MAX = 8
};

struct _save_info {
    uint16_t save_type;
    uint16_t save_index;
};

static struct _save_info save_info;
extern bool settings_enabled_dlps;
void storage_launcher_worker(struct k_work *work) {
    LOG_DBG("save,type:%x,index:%d", save_info.save_type, save_info.save_index);
    uint8_t bits = 0x80;
    settings_enabled_dlps = false;
    for (int i = 0; i < SAVE_LAUNCHER_MAX; i++) {
        switch (save_info.save_type & bits) {
        case SAVE_LAUNCHER_LAYOUT:
            launcher_update_layout();
            save_info.save_type &= ~SAVE_LAUNCHER_LAYOUT;
            break;

        case SAVE_LAUNCHER_ENCODER:
#ifdef ENCODER_MAP_ENABLE
            launcher_update_encoder();
            save_info.save_type &= ~SAVE_LAUNCHER_ENCODER;
#endif
            break;
        case SAVE_LAUNCHER_CUSTOM:
#if LAUNCHER_EEPROM_CUSTOM_CONFIG_SIZE
            launcher_update_custom();
            save_info.save_type &= ~SAVE_LAUNCHER_CUSTOM;
#endif
            break;
        case SAVE_LAUNCHER_MACROS:
            launcher_update_macros();
            save_info.save_type &= ~SAVE_LAUNCHER_MACROS;
            break;
        case SAVE_LAUNCHER_KEYMAPS: {

            uint16_t bits = 0x01;
            for (int i = 0; i < DYNAMIC_KEYMAP_LAYER_COUNT; i++) {
                if (bits & save_info.save_index) {
                    LOG_DBG("SAVE_LAUNCHER_KEYMAPS:%d", i);
                    launcher_update_keymap(i);
                    save_info.save_index &= ~bits;
                }
                bits <<= 1;
            }

            save_info.save_type &= ~SAVE_LAUNCHER_KEYMAPS;

        } break;
        // case SAVE_LAUNCHER_CHANGED:
        //     {

        //         uint16_t bits=0x01;
        //         for(int i=0;i<16;i++)
        //         {
        //             if(bits & save_info.save_index)
        //             {
        //                 LOG_DBG("SAVE_LAUNCHER_CHANGED:%d",i);
        //                 launcher_update_changed_keys(i);
        //             }
        //             bits <<=1;
        //         }
        //         LOG_DBG("save,type:%x,index:%d",save_info.save_type,save_info.save_index);
        //         save_info.save_type &= ~SAVE_LAUNCHER_CHANGED;
        //         save_info.save_index =0;

        //     }

        //     break;
        case SAVE_LAUNCHER_KEYMAPS_RESET:
            save_info.save_type &= ~SAVE_LAUNCHER_KEYMAPS_RESET;
            launcher_reset_keymaps();
            break;
        case SAVE_LAUNCHER_MACROS_RESET:
            save_info.save_type &= ~SAVE_LAUNCHER_MACROS_RESET;
            launcher_delete_macros();
            break;
#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
        case SAVE_LAUNCHER_DEBOUNCE:
            launcher_update_debounce();
            save_info.save_type &= ~SAVE_LAUNCHER_DEBOUNCE;
            break;
#endif
        }
        bits >>= 1;
    }
    settings_enabled_dlps = true;
}
K_WORK_DELAYABLE_DEFINE(storage_launcher_work, storage_launcher_worker);

extern uint16_t gen_launcher_keymaps[DYNAMIC_KEYMAP_LAYER_COUNT][MATRIX_ROWS][MATRIX_COLS];
uint16_t keycode_at_keymap_location_raw(uint8_t layer_num, uint8_t row, uint8_t column) {
    if (layer_num < NUM_KEYMAP_LAYERS_RAW && row < MATRIX_ROWS && column < MATRIX_COLS) {
        // return (layer_num <<8) |
        // (row*MATRIX_COLS+column);//launcher_device.keymaps[layer_num][row][column];
        return gen_launcher_keymaps[layer_num][row][column];
    }
    return KC_TRNS;
}
uint8_t dynamic_keymap_get_layer_count(void) { return DYNAMIC_KEYMAP_LAYER_COUNT; }

inline uint16_t dynamic_keymap_key_to_eeprom_address(uint8_t layer, uint8_t row, uint8_t column) {
    // TODO: optimize this with some left shifts
    return (DYNAMIC_KEYMAP_EEPROM_ADDR) + (layer * MATRIX_ROWS * MATRIX_COLS * 2) +
           (row * MATRIX_COLS * 2) + (column * 2);
}

uint16_t dynamic_keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t column) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || row >= MATRIX_ROWS || column >= MATRIX_COLS)
        return KC_NO;
    uint16_t address = dynamic_keymap_key_to_eeprom_address(layer, row, column);
    // Big endian, so we can read/write EEPROM directly from host if we want
    uint16_t keycode = eeprom_read_byte(address) << 8;
    keycode |= eeprom_read_byte(address + 1);
    return keycode;
}
void dynamic_keymap_set_keycode_no_update(uint8_t layer, uint8_t row, uint8_t column,
                                          uint16_t keycode) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || row >= MATRIX_ROWS || column >= MATRIX_COLS)
        return;
    uint16_t address = dynamic_keymap_key_to_eeprom_address(layer, row, column);
    // Big endian, so we can read/write EEPROM directly from host if we want
    eeprom_update_byte(address, (uint8_t)(keycode >> 8));
    eeprom_update_byte(address + 1, (uint8_t)(keycode & 0xFF));
}
void dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || row >= MATRIX_ROWS || column >= MATRIX_COLS)
        return;
    uint16_t address = dynamic_keymap_key_to_eeprom_address(layer, row, column);
    // Big endian, so we can read/write EEPROM directly from host if we want
    eeprom_update_byte(address, (uint8_t)(keycode >> 8));
    eeprom_update_byte(address + 1, (uint8_t)(keycode & 0xFF));

    // launcher_set_changed_key(layer,row,column);
    set_zmk_keymap(layer, row, column, keycode);

    save_info.save_type |= SAVE_LAUNCHER_KEYMAPS;
    save_info.save_index |= 1 << layer;

    k_work_reschedule(&storage_launcher_work, K_MSEC(1500));
}

#ifdef ENCODER_MAP_ENABLE
extern uint16_t gen_launcher_encoders[DYNAMIC_KEYMAP_LAYER_COUNT][NUM_ENCODERS][2];
uint16_t dynamic_keymap_encoder_to_eeprom_address(uint8_t layer, uint8_t encoder_id) {
    return (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR) + (layer * NUM_ENCODERS * 2 * 2) +
           (encoder_id * 2 * 2);
}

uint16_t dynamic_keymap_get_encoder(uint8_t layer, uint8_t encoder_id, bool clockwise) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || encoder_id >= NUM_ENCODERS)
        return KC_NO;
    uint16_t address = dynamic_keymap_encoder_to_eeprom_address(layer, encoder_id);
    // Big endian, so we can read/write EEPROM directly from host if we want
    uint16_t keycode = ((uint16_t)eeprom_read_byte(address + (clockwise ? 0 : 2))) << 8;
    keycode |= eeprom_read_byte(address + (clockwise ? 0 : 2) + 1);
    return keycode;
}

void dynamic_keymap_set_encoder(uint8_t layer, uint8_t encoder_id, bool clockwise,
                                uint16_t keycode) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || encoder_id >= NUM_ENCODERS)
        return;
    uint16_t address = dynamic_keymap_encoder_to_eeprom_address(layer, encoder_id);
    // Big endian, so we can read/write EEPROM directly from host if we want
    eeprom_update_byte(address + (clockwise ? 0 : 2), (uint8_t)(keycode >> 8));
    eeprom_update_byte(address + (clockwise ? 0 : 2) + 1, (uint8_t)(keycode & 0xFF));

    set_zmk_encoders(layer, clockwise, keycode);

    save_info.save_type |= SAVE_LAUNCHER_ENCODER;

    k_work_reschedule(&storage_launcher_work, K_MSEC(1500));
}
void dynamic_keymap_set_encoder_no_update(uint8_t layer, uint8_t encoder_id, bool clockwise,
                                          uint16_t keycode) {
    if (layer >= DYNAMIC_KEYMAP_LAYER_COUNT || encoder_id >= NUM_ENCODERS)
        return;
    uint16_t address = dynamic_keymap_encoder_to_eeprom_address(layer, encoder_id);
    // Big endian, so we can read/write EEPROM directly from host if we want
    eeprom_update_byte(address + (clockwise ? 0 : 2), (uint8_t)(keycode >> 8));
    eeprom_update_byte(address + (clockwise ? 0 : 2) + 1, (uint8_t)(keycode & 0xFF));
}
uint16_t keycode_at_encodermap_location_raw(uint8_t layer_num, uint8_t encoder_idx,
                                            bool clockwise) {
    if (layer_num < NUM_KEYMAP_LAYERS_RAW && encoder_idx < NUM_ENCODERS) {
        return gen_launcher_encoders[layer_num][encoder_idx][clockwise ? 0 : 1];
    } else
        return 0;
}
#endif // ENCODER_MAP_ENABLE

void dynamic_keymap_reset(bool save) {
    // Reset the keymaps in EEPROM to what is in flash.
    for (int layer = 0; layer < DYNAMIC_KEYMAP_LAYER_COUNT; layer++) {
#if 1
        for (int row = 0; row < MATRIX_ROWS; row++) {
            for (int column = 0; column < MATRIX_COLS; column++) {
                dynamic_keymap_set_keycode_no_update(
                    layer, row, column, keycode_at_keymap_location_raw(layer, row, column));
                set_zmk_keymap(layer, row, column,
                               keycode_at_keymap_location_raw(layer, row, column));
            }
        }
#endif
        // memcpy(&launcher_device.keymaps[layer*KEYMAP_LEN],&gen_launcher_keymaps[layer],KEYMAP_LEN);
#ifdef ENCODER_MAP_ENABLE
        for (int encoder = 0; encoder < NUM_ENCODERS; encoder++) {
            dynamic_keymap_set_encoder(layer, encoder, true,
                                       keycode_at_encodermap_location_raw(layer, encoder, true));
            dynamic_keymap_set_encoder(layer, encoder, false,
                                       keycode_at_encodermap_location_raw(layer, encoder, false));
        }
#endif // ENCODER_MAP_ENABLE
    }
    LOG_DBG("MATRIX_ROWS:%d,MATRIX_COLS:%d", MATRIX_ROWS, MATRIX_COLS);
    LOG_HEXDUMP_DBG(&launcher_device.keymaps[0], 32, "keymap");

    if (save) {
        save_info.save_type = SAVE_LAUNCHER_KEYMAPS_RESET;
        save_info.save_index = 0xff;
        k_work_reschedule(&storage_launcher_work, K_MSEC(100));
    }
}

void dynamic_keymap_get_buffer(uint16_t offset, uint16_t size, uint8_t *data) {
    uint16_t dynamic_keymap_eeprom_size =
        DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
    uint16_t source = (DYNAMIC_KEYMAP_EEPROM_ADDR + offset);
    uint8_t *target = data;
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i < dynamic_keymap_eeprom_size) {
            *target = eeprom_read_byte(source);
        } else {
            *target = 0x00;
        }
        source++;
        target++;
    }
}

void dynamic_keymap_set_buffer(uint16_t offset, uint16_t size, uint8_t *data) {
    uint16_t dynamic_keymap_eeprom_size =
        DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
    uint16_t target = (DYNAMIC_KEYMAP_EEPROM_ADDR + offset);
    uint8_t *source = data;
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i < dynamic_keymap_eeprom_size) {
            eeprom_update_byte(target, *source);
        }
        source++;
        target++;
    }
    // TODO:
    LOG_DBG(".");
}

uint16_t keycode_at_keymap_location(uint8_t layer_num, uint8_t row, uint8_t column) {
    if (layer_num < DYNAMIC_KEYMAP_LAYER_COUNT && row < MATRIX_ROWS && column < MATRIX_COLS) {
        return dynamic_keymap_get_keycode(layer_num, row, column);
    }
    return KC_NO;
}

#ifdef ENCODER_MAP_ENABLE
uint16_t keycode_at_encodermap_location(uint8_t layer_num, uint8_t encoder_idx, bool clockwise) {
    if (layer_num < DYNAMIC_KEYMAP_LAYER_COUNT && encoder_idx < NUM_ENCODERS) {
        return dynamic_keymap_get_encoder(layer_num, encoder_idx, clockwise);
    }
    return KC_NO;
}
#endif // ENCODER_MAP_ENABLE

uint8_t dynamic_keymap_macro_get_count(void) { return DYNAMIC_KEYMAP_MACRO_COUNT; }

uint16_t dynamic_keymap_macro_get_buffer_size(void) { return DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; }

void dynamic_keymap_macro_get_buffer(uint16_t offset, uint16_t size, uint8_t *data) {
    uint16_t source = (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset);
    uint8_t *target = data;
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE) {
            *target = eeprom_read_byte(source);
        } else {
            *target = 0x00;
        }
        source++;
        target++;
    }
}

void dynamic_keymap_macro_set_buffer(uint16_t offset, uint16_t size, uint8_t *data) {
    uint16_t target = (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + offset);
    uint8_t *source = data;
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i < DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE) {
            eeprom_update_byte(target, *source);
        }
        source++;
        target++;
    }

    save_info.save_type |= SAVE_LAUNCHER_MACROS;

    k_work_reschedule(&storage_launcher_work, K_MSEC(1500));
}

void dynamic_keymap_macro_reset(bool save) {
    uint16_t p = (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR);
    uint16_t end = (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE);
    // int i=0;
    while (p != end) {
        eeprom_update_byte(p, 0);
        ++p;
    }
    if (save) {
        save_info.save_type = SAVE_LAUNCHER_MACROS_RESET;
        k_work_reschedule(&storage_launcher_work, K_MSEC(100));
    }
}
void launcher_save_debounce(void) {
    save_info.save_type |= SAVE_LAUNCHER_DEBOUNCE;
    k_work_reschedule(&storage_launcher_work, K_MSEC(1000));
}
uint32_t convert_time(uint8_t *ch);
void send_string_delay(uint32_t delay_time);
extern bool macro_enabled_dlps;
uint8_t macro_exec_start = 0;
uint8_t macro_exec_end = 0;
void dynamic_keymap_macro_send(uint8_t id) {
    if (id >= DYNAMIC_KEYMAP_MACRO_COUNT || (p_macro != 0)) {
        return;
    }

    // Check the last byte of the buffer.
    // If it's not zero, then we are in the middle
    // of buffer writing, possibly an aborted buffer
    // write. So do nothing.
    uint16_t p = (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1);
    if (eeprom_read_byte(p) != 0) {
        return;
    }

    // Skip N null characters
    // p will then point to the Nth macro
    p = (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR);
    uint16_t end = (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE);
    while (id > 0) {
        // If we are past the end of the buffer, then there is
        // no Nth macro in the buffer.
        if (p == end) {
            return;
        }
        if (eeprom_read_byte(p) == 0) {
            --id;
        }
        ++p;
    }
    p_macro = p;
    // skip empty macro!
    if (eeprom_read_byte(p_macro) == 0) {
        p_macro = 0;
        return;
    }
    macro_enabled_dlps = false;
    macro_exec_start = 1;
    macro_exec_end = 0;
    void ppt_macro_start(void);
    ppt_macro_start();
    macro_exec();
}
#include <ctype.h>
uint32_t convert_time(uint8_t *ch) {
    uint32_t ms = 0;
    while (isdigit(*ch)) {
        ms *= 10;
        ms += *ch - '0';
        ch++;
    }
    return ms;
}
void send_string_delay(uint32_t delay_time) {
    uint8_t data[8] = {SS_QMK_PREFIX, SS_DELAY_CODE};
    if (delay_time > 9999)
        delay_time = 9999;
    sprintf(&data[2], "%d", delay_time);
    send_string_with_delay(data, DYNAMIC_KEYMAP_MACRO_DELAY);
}
uint16_t get_behavior_queue_num(void);
int ringbuf_used_get(void);
bool ringbuf_almost_full(void);
bool hog_almost_full(void);
uint8_t get_report_rate(void);

void macro_exec(void) {
    static uint16_t total = 0;
    uint8_t err = 0;
    static uint32_t delay_time = 0;

    uint8_t poll = 1 * get_report_rate();
    uint8_t max_num = 18;
    if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
        poll = 4 * get_report_rate() / 2;
    } else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE) {
        poll = 20;
        max_num = 10;
    }
    // Send the macro string by making a temporary string.
    char data[8] = {0};
    // We already checked there was a null at the end of
    // the buffer, so this cannot go past the end
    uint8_t count = 0;
    if (get_behavior_queue_num() > max_num || ringbuf_almost_full() || hog_almost_full()) {
        LOG_ERR("behavior_queue_is_full!,queue:%d,ringbuf:%d", get_behavior_queue_num(),
                ringbuf_used_get());
        k_timer_start(&macro_timer, K_MSEC(poll), K_FOREVER);
        return;
    }
    while (1) {
        data[0] = eeprom_read_byte(p_macro++);
        data[1] = 0;
        // Stop at the null terminator of this macro string
        if (data[0] == 0) {
            err = 2;
            break;
        }
        if (data[0] == SS_QMK_PREFIX) {
            // Get the code
            data[1] = eeprom_read_byte(p_macro++);
            // Unexpected null, abort.
            if (data[1] == 0) {
                err = 1;
                break;
            }
            if (data[1] == SS_TAP_CODE || data[1] == SS_DOWN_CODE || data[1] == SS_UP_CODE) {
                // Get the keycode
                data[2] = eeprom_read_byte(p_macro++);
                // Unexpected null, abort.
                if (data[2] == 0) {
                    err = 1;
                    break;
                }
                // Null terminate
                data[3] = 0;
            } else if (data[1] == SS_DELAY_CODE) {
                // Get the number and '|'
                // At most this is 4 digits plus '|'
                uint8_t i = 2;
                while (1) {
                    data[i] = eeprom_read_byte(p_macro++);
                    // Unexpected null, abort
                    if (data[i] == 0) {
                        err = 1;
                        break;
                    }
                    // Found '|', send it
                    if (data[i] == '|') {
                        data[i + 1] = 0;
                        break;
                    }
                    // If haven't found '|' by i==6 then
                    // number too big, abort
                    if (i == 6) {
                        err = 1;
                        break;
                    }
                    ++i;
                }
                delay_time += convert_time(&data[2]);
            }
        }
        // LOG_HEXDUMP_DBG(data,sizeof(data),"macro");
        LOG_DBG("macro:%d,data:%02x", total, data[2]);
        if (data[1] != SS_DELAY_CODE) {
            send_string_with_delay(data, DYNAMIC_KEYMAP_MACRO_DELAY);
        } else if (delay_time) {
            send_string_delay(delay_time);
            LOG_DBG("send_string_delay:%d", delay_time);
            delay_time = 0;
        }

        total++;
        if (++count >= max_num) {
            break;
        }
    }
    if (err == 0) {
        LOG_DBG("macro continue");
        k_timer_start(&macro_timer, K_MSEC(poll), K_FOREVER);
    } else if (err == 2) {
        LOG_DBG("macro done,t:%d", total);
        send_string_end();
        macro_exec_start = 0;
        macro_exec_end = 1;
        if (total == 0) {
            macro_enabled_dlps = true;
        }
        total = 0;
        p_macro = 0;
    } else if (err == 1) {
        LOG_ERR("macro ERR");
        macro_enabled_dlps = true;
        p_macro = 0;
    }
}
void macro_timer_expiry(struct k_timer *timer) { macro_exec(); }
