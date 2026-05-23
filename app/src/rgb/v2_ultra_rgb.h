#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {// Key Matrix to LED Index
     {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, __, 14},
     {15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, __, 29},
     {30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, __, __, 43},
     {44, __, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, __, 56, __},
     {57, 58, 59, __, __, __, 60, __, __, __, 61, 62, 63, 64, 65, 66}},
    {// LED Index to Physical Position
     {0, 0},    {15, 0},   {29, 0},   {44, 0},   {59, 0},   {73, 0},   {88, 0},   {103, 0},
     {118, 0},  {132, 0},  {147, 0},  {162, 0},  {176, 0},  {202, 0},  {224, 0},  {4, 15},
     {22, 15},  {37, 15},  {51, 15},  {66, 15},  {81, 15},  {95, 15},  {110, 15}, {125, 15},
     {140, 15}, {154, 15}, {169, 15}, {184, 15}, {202, 15}, {224, 15}, {6, 26},   {23, 26},
     {38, 26},  {55, 26},  {70, 26},  {84, 26},  {99, 26},  {114, 26}, {129, 26}, {143, 26},
     {158, 26}, {173, 26}, {202, 26}, {224, 26}, {7, 38},   {33, 38},  {48, 38},  {62, 38},
     {77, 38},  {92, 38},  {106, 38}, {121, 38}, {136, 38}, {151, 38}, {165, 38}, {184, 38},
     {209, 41}, {2, 49},   {20, 49},  {39, 49},  {94, 49},  {147, 49}, {162, 49}, {176, 49},
     {195, 52}, {209, 52}, {224, 52}},
    {// RGB LED Index to Flag
     1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4,
     4, 4, 4, 4, 4, 4, 1, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 4,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 1}};

#ifdef CONFIG_KEYCHRON_RGB_ENABLE
// Default Color of Per Key RGB
#define DC_RED {0, 255, 255}
#define DC_BLU {170, 255, 255}
#define DC_YLW {43, 255, 255}

HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    // 0     //1      //2    //3     //4     //5     //6     //7     //8     //9     //10    //11
    // //12    //13      //14      //15
    DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,
    DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW, DC_YLW, DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_RED, DC_YLW, DC_YLW, DC_BLU, DC_BLU, DC_BLU,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,
    DC_BLU, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
#endif