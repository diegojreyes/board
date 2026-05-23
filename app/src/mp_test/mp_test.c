/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mp_test, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/mode_monitor.h>
#include <zmk/board.h>
#include <zmk/mp_test/mp_test.h>
#include "rtl_rcc.h"
#include "reset_reason.h"
#include "trace.h"

#if (THE_WAY_TO_ENTER_MP_TEST_MODE == ENTER_MP_TEST_MODE_BY_GPIO_TRIGGER)
static struct gpio_dt_spec
    mp_test_pin_1; //=
                   // GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(mode_monitor), mptest_gpios, 0);
static struct gpio_dt_spec
    mp_test_pin_2; //=
                   // GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(mode_monitor), mptest_gpios, 1);
#endif

#if (THE_WAY_TO_ENTER_MP_TEST_MODE == ENTER_MP_TEST_MODE_BY_GPIO_TRIGGER)
bool mp_test_mode_check_and_enter(void);
#elif (THE_WAY_TO_ENTER_MP_TEST_MODE == ENTER_MP_TEST_MODE_BY_USB_CMD)
void mp_test_set_report_handle(uint8_t *p_packet, uint32_t packet_len);
void mp_test_get_report_handle(uint8_t *p_packet, uint16_t *packet_len);
#endif

#if (THE_WAY_TO_ENTER_MP_TEST_MODE == ENTER_MP_TEST_MODE_BY_GPIO_TRIGGER)
/**
 * @brief  Check if need enter mp test mode
 * @param  None
 * @return Result - true: success, false: fail
 */
bool mp_test_mode_check_and_enter(void) {
    bool ret = false;
    gpio_pin_configure_dt(&mp_test_pin_1, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_pin_configure_dt(&mp_test_pin_2, GPIO_INPUT | GPIO_PULL_UP);

    int mp_test_pin_1_level = gpio_pin_get_raw(mp_test_pin_1.port, mp_test_pin_1.pin); // P2_4
    int mp_test_pin_2_level = gpio_pin_get_raw(mp_test_pin_2.port, mp_test_pin_2.pin); // P2_3

    if ((mp_test_pin_1_level == GPIO_PIN_LEVEL_HIGH) &&
        (mp_test_pin_2_level == GPIO_PIN_LEVEL_LOW)) {
        app_mode.is_in_single_test_mode = true;
        ret = app_mode.is_in_single_test_mode;
    }
    return ret;
}
#endif