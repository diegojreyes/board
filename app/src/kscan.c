/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/matrix_transform.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/mode_monitor.h>
#include <zmk/matrix.h>
#include <zmk/endpoints.h>
#include <zephyr/drivers/gpio.h>

#define ZMK_KSCAN_EVENT_STATE_PRESSED 0
#define ZMK_KSCAN_EVENT_STATE_RELEASED 1

struct zmk_kscan_event {
    uint32_t row;
    uint32_t column;
    uint32_t state;
};

struct zmk_kscan_msg_processor {
    struct k_work work;
} msg_processor;

K_MSGQ_DEFINE(zmk_kscan_msgq, sizeof(struct zmk_kscan_event), CONFIG_ZMK_KSCAN_EVENT_QUEUE_SIZE, 4);

#if CONFIG_ZMK_LAUNCHER
static uint32_t matrix_rows[ZMK_MATRIX_ROWS ]; 
#endif 
#if CONFIG_PM
static int key_press_num = 0;
#endif

#if CONFIG_DEBUG_IO
// P2_7 gpioa-28
const struct gpio_dt_spec debug_io = GPIO_DT_SPEC_GET(DT_NODELABEL(debug_io),gpios);
// {
//     .port = DEVICE_DT_GET(DT_NODELABEL(gpioa)),
//     .pin = 28,
//     .dt_flags = GPIO_ACTIVE_LOW | GPIO_PULL_UP,
// };
#endif 
#include "rtl_pinmux.h"
#include "trace.h"
#include <drivers/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

bool dip_switch_update_user(uint8_t index, bool active);
void process_rgb_matrix(uint8_t row, uint8_t col, bool pressed); 
uint8_t zmk_keymap_highest_layer_active(void);
struct zmk_behavior_binding *get_zmk_keymap(uint8_t layer, uint8_t pos);
int hid_listener_keycode_pressed(const struct zmk_keycode_state_changed *ev);
int hid_listener_keycode_released(const struct zmk_keycode_state_changed *ev);

static void zmk_kscan_callback(const struct device *dev, uint32_t row, uint32_t column,
                               bool pressed) {                              
    // gpio_pin_toggle_dt(&debug_io);
    // LOG_DBG("keyscan callback: row,col is (%d %d),press:%d", row, column,pressed);
#if CONFIG_PM
    /* cpu will check dlps status modules by modules in idle task, in this case interrupt is
       disabled when the scan interval of keyscan is set <100us, it may result in unstable interrupt
       intervals in this case we can skip check before wfi/dlps
     */
    if (row < ((ZMK_MATRIX_ROWS-1))) {
        if (pressed) {
            key_press_num++;
            if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB)
            {
                pm_no_check_status_before_enter_wfi();
            }
        } else {
            key_press_num--;
            if (key_press_num <= 0) {
                pm_check_status_before_enter_wfi_or_dlps();
                key_press_num=0;
            }
        }
    }
    else
    {
        if(row==(ZMK_MATRIX_ROWS-1) &&column==0)
        {
            dip_switch_update_user(0,pressed?1:0);
        }
    }
    // LOG_DBG("row,col is (%d %d),num:%d,press:%d", row, column,key_press_num,pressed);
#endif
    

    {
        struct zmk_kscan_event ev = {
            .row = row,
            .column = column,
            .state = (pressed ? ZMK_KSCAN_EVENT_STATE_PRESSED : ZMK_KSCAN_EVENT_STATE_RELEASED)};

        int err=k_msgq_put(&zmk_kscan_msgq, &ev, K_NO_WAIT);
        if(err<0)
        {
            LOG_ERR("kscan msg full,err:%d",err);
        }
        k_work_submit(&msg_processor.work);
    }
}

void zmk_kscan_process_msgq(struct k_work *item) {
    struct zmk_kscan_event ev;

    while (k_msgq_get(&zmk_kscan_msgq, &ev, K_NO_WAIT) == 0) {
        bool pressed = (ev.state == ZMK_KSCAN_EVENT_STATE_PRESSED);
        int32_t position = zmk_matrix_transform_row_column_to_position(ev.row, ev.column);
        LOG_ERR("r:%d,c:%d,pos:%d",ev.row,ev.column,position);
        if (position < 0) {
            LOG_WRN("Not found in transform: row: %d, col: %d, pressed: %s", ev.row, ev.column,
                    (pressed ? "true" : "false"));
            continue;
        }
#if CONFIG_ZMK_LAUNCHER
        if (pressed)
            matrix_rows[ev.row] |= 1 << ev.column;
        else
            matrix_rows[ev.row] &= ~(1 << ev.column);
#endif

        raise_zmk_position_state_changed((struct zmk_position_state_changed){
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                .state = pressed,
                .position = position,
                .timestamp = k_uptime_get()});
#if CONFIG_LED_STRIP
        process_rgb_matrix(ev.row,ev.column,pressed);
#endif         
    }
}

int zmk_kscan_init(const struct device *dev) {
    if (dev == NULL) {
        LOG_ERR("Failed to get the KSCAN device");
        return -EINVAL;
    }

    k_work_init(&msg_processor.work, zmk_kscan_process_msgq);

    kscan_config(dev, zmk_kscan_callback);
    kscan_enable_callback(dev);
#if CONFIG_DEBUG_IO
    gpio_pin_configure_dt(&debug_io, GPIO_OUTPUT);
#endif 
    return 0;
}
#if CONFIG_ZMK_LAUNCHER
uint32_t matrix_get_row(uint8_t row) { return matrix_rows[row]; }
#endif

void toggle_debug_pin(void) { 
#if CONFIG_DEBUG_IO
    gpio_pin_toggle_dt(&debug_io); 
#endif     
}


bool all_key_up(void)
{
    return key_press_num ==0;
}
uint8_t get_key_press_num(void)
{
    return key_press_num;
}