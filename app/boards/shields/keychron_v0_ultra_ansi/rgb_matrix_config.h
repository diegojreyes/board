#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {// Key Matrix to LED Index
     {
         __,
         0,
         1,
         2,
         3,
     },
     {
         4,
         5,
         6,
         7,
         8,
     },
     {
         9,
         10,
         11,
         12,
         13,
     },
     {
         14,
         15,
         16,
         17,
         __,
     },
     {
         18,
         19,
         20,
         21,
         22,
     },
     {
         23,
         __,
         24,
         25,
         __,
     }},
    {
        // LED Index to Physical Position
        {20, 0}, {36, 0},  {52, 0},  {68, 0},  {0, 15},  {20, 15}, {36, 15}, {52, 15}, {68, 15},
        {0, 26}, {20, 26}, {36, 26}, {52, 26}, {68, 32}, {0, 38},  {20, 38}, {36, 38}, {52, 38},
        {0, 49}, {20, 49}, {36, 49}, {52, 49}, {68, 55}, {0, 61},  {28, 61}, {52, 61},

    },
    {
        1, 1, 1, 1, 1, 4, 4, 4, 4, 1, 4, 4, 4, 4, 1, 4, 4, 4, 1, 4, 4, 4, 4, 1, 1, 1,
    }};
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
// Default Color of Per Key RGB
#define DC_RED                                                                                     \
    { 0, 255, 255 }
#define DC_BLU                                                                                     \
    { 170, 255, 255 }
#define DC_YLW                                                                                     \
    { 43, 255, 255 }

HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU,
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW, DC_BLU, DC_BLU, DC_BLU,
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_RED, DC_YLW, DC_BLU, DC_BLU,
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0,
};
#endif