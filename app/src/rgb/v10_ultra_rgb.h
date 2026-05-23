#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {// Key Matrix to LED Index
     {__, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, __, __, 14},
     {15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, __, __, 30},
     {31, 32, 33, 34, 35, 36, 37, __, 38, 39, 40, 41, 42, 43, 44, 45, __, 46},
     {47, 48, 49, 50, 51, 52, 53, __, 54, 55, 56, 57, 58, 59, 60, __, __, 61},
     {62, 63, __, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, __, 76, __},
     {77, 78, 79, __, 80, __, 81, 82, __, 83, __, 84, __, __, __, 85, 86, 87}},
    {// LED Index to Physical Position
     {15, 0},   {30, 0},   {42, 0},   {57, 0},   {69, 0},   {84, 0},   {96, 0},   {120, 0},
     {132, 0},  {147, 0},  {159, 0},  {174, 0},  {186, 0},  {201, 0},  {218, 0},  {4, 15},
     {18, 15},  {30, 15},  {42, 15},  {57, 15},  {69, 15},  {81, 15},  {93, 15},  {112, 15},
     {124, 15}, {136, 15}, {148, 15}, {163, 15}, {175, 15}, {201, 15}, {220, 15}, {3, 26},
     {18, 26},  {33, 26},  {48, 26},  {60, 26},  {72, 26},  {84, 26},  {110, 26}, {122, 26},
     {134, 26}, {146, 26}, {160, 26}, {173, 26}, {185, 26}, {201, 26}, {222, 26}, {2, 38},
     {18, 38},  {33, 38},  {50, 38},  {62, 38},  {74, 38},  {85, 38},  {114, 38}, {125, 38},
     {137, 38}, {149, 38}, {164, 38}, {176, 38}, {201, 38}, {224, 38}, {0, 49},   {18, 49},
     {37, 49},  {52, 49},  {64, 49},  {76, 49},  {86, 49},  {110, 49}, {121, 49}, {133, 49},
     {145, 49}, {157, 49}, {172, 49}, {193, 49}, {209, 49}, {0, 61},   {15, 61},  {30, 61},
     {52, 61},  {75, 61},  {94, 61},  {121, 61}, {143, 61}, {200, 61}, {209, 61}, {224, 61}},
    {// RGB LED Index to Flag
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,
     1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     1, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 4, 1, 4, 1, 1, 1, 1, 1}};

#ifdef CONFIG_KEYCHRON_RGB_ENABLE
// Default Color of Per Key RGB
#define DC_RED {0, 255, 255}
#define DC_BLU {170, 255, 255}
#define DC_YLW {43, 255, 255}
#define DC_NUL {0, 0, 0}

HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,
    DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW, DC_YLW, DC_YLW, DC_YLW,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,
    DC_BLU, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_BLU, DC_BLU,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW, DC_YLW,
    DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_BLU, DC_YLW, DC_BLU, DC_YLW, DC_YLW, DC_YLW, DC_YLW,
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
#endif