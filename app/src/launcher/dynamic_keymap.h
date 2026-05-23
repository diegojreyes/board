
#pragma once

#include <stdint.h>
#include <stdbool.h>

uint8_t dynamic_keymap_get_layer_count(void);
uint16_t dynamic_keymap_key_to_eeprom_address(uint8_t layer, uint8_t row, uint8_t column);
uint16_t dynamic_keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t column);
void dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);
#ifdef ENCODER_MAP_ENABLE
uint16_t dynamic_keymap_get_encoder(uint8_t layer, uint8_t encoder_id, bool clockwise);
void dynamic_keymap_set_encoder(uint8_t layer, uint8_t encoder_id, bool clockwise,
                                uint16_t keycode);
#endif // ENCODER_MAP_ENABLE
void dynamic_keymap_reset(bool save);
void dynamic_keymap_get_buffer(uint16_t offset, uint16_t size, uint8_t *data);
void dynamic_keymap_set_buffer(uint16_t offset, uint16_t size, uint8_t *data);

uint8_t dynamic_keymap_macro_get_count(void);
uint16_t dynamic_keymap_macro_get_buffer_size(void);
void dynamic_keymap_macro_get_buffer(uint16_t offset, uint16_t size, uint8_t *data);
void dynamic_keymap_macro_set_buffer(uint16_t offset, uint16_t size, uint8_t *data);
void dynamic_keymap_macro_reset(bool save);

void dynamic_keymap_macro_send(uint8_t id);
void dynamic_keymap_set_keycode_no_update(uint8_t layer, uint8_t row, uint8_t column,
                                          uint16_t keycode);
void dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);
uint16_t keycode_at_keymap_location_raw(uint8_t layer_num, uint8_t row, uint8_t column);
uint16_t keycode_at_encodermap_location_raw(uint8_t layer_num, uint8_t encoder_idx, bool clockwise);
void dynamic_keymap_set_encoder_no_update(uint8_t layer, uint8_t encoder_id, bool clockwise,
                                          uint16_t keycode);