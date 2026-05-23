
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rgb_matrix_conf.h"

typedef enum rgb_task_states {
    RGB_STATE_START,
    RGB_STATE_RENDER,
    RGB_STATE_FLUSH,
    RGB_STATE_SYNC
} rgb_task_states;

typedef uint8_t led_flags_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_led_t;

typedef rgb_led_t RGB;

typedef struct HSV {
    uint8_t h;
    uint8_t s;
    uint8_t v;
} HSV;

typedef struct {
    uint8_t count;
    uint8_t x[LED_HITS_TO_REMEMBER];
    uint8_t y[LED_HITS_TO_REMEMBER];
    uint8_t index[LED_HITS_TO_REMEMBER];
    uint16_t tick[LED_HITS_TO_REMEMBER];
} last_hit_t;

typedef struct {
    uint8_t iter;
    led_flags_t flags;
    bool init;
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    uint8_t region;
#endif
} effect_params_t;

typedef struct {
    uint8_t x;
    uint8_t y;
} led_point_t;

typedef struct {
    uint8_t matrix_co[MATRIX_ROWS][MATRIX_COLS];
    led_point_t point[RGB_MATRIX_LED_COUNT];
    uint8_t flags[RGB_MATRIX_LED_COUNT];
} led_config_t;

typedef union {
    uint64_t raw;
    struct {
        uint8_t enable : 2;
        uint8_t mode : 6;
        HSV hsv;
        uint8_t speed;
        led_flags_t flags;
        uint8_t back_mode;
    };
} rgb_config_t;

#define HAS_FLAGS(bits, flags) ((bits & flags) == flags)
#define HAS_ANY_FLAGS(bits, flags) ((bits & flags) != 0x00)

#define LED_FLAG_ALL 0xFF
#define LED_FLAG_NONE 0x00
#define LED_FLAG_MODIFIER 0x01
#define LED_FLAG_UNDERGLOW 0x02
#define LED_FLAG_KEYLIGHT 0x04
#define LED_FLAG_INDICATOR 0x08

#define NO_LED 255
