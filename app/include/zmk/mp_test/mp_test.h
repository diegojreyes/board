/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "zmk/board.h"
#include "rtl_pinmux.h"

#if FEATURE_SUPPORT_MP_TEST_MODE
/*============================================================================*
 *                              Macros
 *============================================================================*/
#if (THE_WAY_TO_ENTER_MP_TEST_MODE == ENTER_MP_TEST_MODE_BY_GPIO_TRIGGER)
#define MP_TEST_PIN_1 P2_3
#define MP_TEST_PIN_2 P2_4
#endif

#endif

// #define SWITCH_TO_TEST_MODE 0xDB

bool mp_test_mode_check_and_enter(void);