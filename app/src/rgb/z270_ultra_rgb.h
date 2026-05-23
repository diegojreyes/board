#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {
        // Key Matrix to LED Index
        { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16 },
        { 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33 },
        { 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, __, 49, 50, 51, 34 },
        { 52, __, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, __, 63, 64, 65, 35 },
        { 66, 67, 68, __, __, __, 69, __, __, __, __, 70, 71, 72, 73, 74, 36 },

    },
    {
        // LED Index to Physical Position
        {0,0},  {14,0},  {28,0},  {32,0},  {48,0},  {62,0},  {74,0},  {88,0},   {102,0},  {116,0},  {130,0},  {144,0},  {160,0},    {174,0},            {188,0}, {202,0},{216,0},
        {0,15}, {15,15}, {29,15}, {32,15}, {48,15}, {62,15}, {74,15}, {88,15}, {102,15}, {116,15}, {130,15}, {144,15}, {160,15},   {174,15},           {188,15}, {202,15},{216,15},
        {0,26}, {14,26}, {28,26}, {32,26}, {48,26}, {62,26}, {74,26}, {88,26}, {102,26}, {116,26}, {130,26}, {144,26},             {174,26},           {188,26}, {202,26},{216,26},
        {0,38},          {28,38}, {32,38}, {48,38}, {62,38}, {74,38}, {88,38}, {102,38}, {116,38}, {130,38}, {144,38},             {174,38},           {188,38}, {202,38},{216,38},
        {0,49}, {14,49}, {28,49},                            {74,49},                                        {144,49}, {160,49},   {174,49},           {188,49}, {202,49},{216,49},
       
    },
    {
        // RGB LED Index to Flag
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    1,1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,    1,1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    4,    1,1, 1,
        8,    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    1,    1,1, 1,
        1, 1, 4,          4,             4, 1, 1,    1,1, 1,

    }
};
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
// Default Color of Per Key RGB
#define DC_RED {  0, 255, 255}
#define DC_BLU {170, 255, 255}
#define DC_YLW {43, 255, 255}


HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,             DC_YLW, DC_YLW, DC_YLW,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW,             DC_YLW, DC_YLW, DC_YLW,
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,         DC_YLW,             DC_YLW, DC_YLW, DC_YLW,
    DC_YLW,         DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,         DC_RED,             DC_YLW, DC_YLW, DC_YLW,
    DC_YLW, DC_BLU, DC_BLU,                         DC_BLU,                                 DC_YLW, DC_YLW, DC_YLW,             DC_YLW, DC_YLW, DC_YLW,
      
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       0,0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       0,0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0,       0,0, 0,
    0,    1, 1, 0, 0, 0, 0, 0, 0, 0, 0,    0,       0,0, 0,
    0, 0, 0,          0,             0, 0, 0,       0,0, 0,
};
#endif 