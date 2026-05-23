/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
#include "trace.h"
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/activity.h>
#include <zmk/workqueue.h>

#define BAT_VOLTAGE_LOW (3470)
#define BAT_VOLTAGE_SHUTDOWN (3290)
static uint8_t last_state_of_charge = 100;
static uint8_t bat_level = 100;
static uint16_t voltage;
static bool bat_shutdown;
static uint16_t last_voltage;
uint8_t zmk_rgb_matrix_is_enabled(void);
uint8_t lithium_ion_mv_to_pct_rtk(int16_t bat_mv);
void zmk_battery_check(void);
void check_voltage_when_boot(void);
uint8_t zmk_usb_power_on(void);
void set_force_sleep(bool enable);
void bat_low_check(uint8_t keypress);
void led_bat_low(void);
uint8_t get_charge_state(void);

bool bat_enable_dlps = true;

uint8_t zmk_battery_state_of_charge(void) { return last_state_of_charge; }

#if DT_HAS_CHOSEN(zmk_battery)
static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
#else
#warning                                                                                           \
    "Using a node labeled BATTERY for the battery sensor is deprecated. Set a zmk,battery chosen node instead. (Ignore this if you don't have a battery sensor.)"
static const struct device *battery;
#endif
extern uint8_t macro_running;
static int zmk_battery_update(const struct device *battery) {
    struct sensor_value state_of_charge;
    struct sensor_value sensor_voltage;
    bat_enable_dlps = false;

    k_msleep(100);
    if (macro_running)
        return 0;
    int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);

    if (rc != 0) {
        LOG_DBG("Failed to fetch battery values: %d", rc);
        bat_enable_dlps = true;
        return rc;
    }

    rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &state_of_charge);

    if (rc != 0) {
        LOG_DBG("Failed to get battery state of charge: %d", rc);
        bat_enable_dlps = true;
        return rc;
    }

    rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &sensor_voltage);
    if (rc != 0) {
        LOG_DBG("Failed to fetch battery values: %d", rc);
        bat_enable_dlps = true;
        return rc;
    }
    voltage = sensor_voltage.val1 * 1000 + sensor_voltage.val2 / 1000;
    // DBG_DIRECT("cur v:%d,last:%d,per:%d",voltage, last_voltage, state_of_charge.val1);
    voltage = (last_voltage + voltage) / 2;
    last_voltage = voltage;
    if (zmk_usb_power_on()) {
        LOG_ERR("cur level:%d", state_of_charge.val1);
        if (state_of_charge.val1 < 95 && state_of_charge.val1 > 5) {
            state_of_charge.val1 -= 5;
            LOG_ERR("dec level:%d", state_of_charge.val1);
        }
        if (state_of_charge.val1 == 100) {
            if (get_charge_state())
                state_of_charge.val1 = 99;
        }
    } else {
        if ((state_of_charge.val1 < 90) && (voltage > BAT_VOLTAGE_LOW) &&
            zmk_rgb_matrix_is_enabled()) {
            if (state_of_charge.val1 > 80)
                state_of_charge.val1 = lithium_ion_mv_to_pct_rtk(voltage * 18 / 100 + 10);
            else
                state_of_charge.val1 = lithium_ion_mv_to_pct_rtk(voltage * 18 / 100 + 20);
            // DBG_DIRECT("===cur v:%d,per:%d",voltage, state_of_charge.val1);
            if (state_of_charge.val1 > last_state_of_charge)
                state_of_charge.val1 = last_state_of_charge;
            // DBG_DIRECT("===>cur v:%d,per:%d",voltage, state_of_charge.val1);
        }
    }

    zmk_battery_check();
    if (last_state_of_charge != state_of_charge.val1) {
        last_state_of_charge = state_of_charge.val1;
#if IS_ENABLED(CONFIG_BT_BAS)
        if ((state_of_charge.val1 < bat_level) || zmk_usb_power_on()) {
            bat_level = state_of_charge.val1;
            LOG_DBG("Setting BAS GATT battery level to %d.", bat_level);
            // DBG_DIRECT("bt  %d.", bat_level);
            rc = bt_bas_set_battery_level(bat_level);

            if (rc != 0) {
                LOG_WRN("Failed to set BAS GATT battery level (err %d)", rc);
                bat_enable_dlps = true;
                return rc;
            }
        }

#endif

        rc = raise_zmk_battery_state_changed(
            (struct zmk_battery_state_changed){.state_of_charge = last_state_of_charge});
    }

    bat_enable_dlps = true;
    return rc;
}

static void zmk_battery_work(struct k_work *work) {
    int rc = zmk_battery_update(battery);
    if (rc != 0) {
        LOG_DBG("Failed to update battery value: %d.", rc);
    }
}

K_WORK_DEFINE(battery_work, zmk_battery_work);

static void zmk_battery_timer(struct k_timer *timer) {
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_work);
}

K_TIMER_DEFINE(battery_timer, zmk_battery_timer, NULL);

static void zmk_battery_start_reporting() {
    if (device_is_ready(battery)) {
        if (!bat_shutdown)
            k_timer_start(&battery_timer, K_SECONDS(2),
                          K_SECONDS(CONFIG_ZMK_BATTERY_REPORT_INTERVAL));
        else
            k_timer_stop(&battery_timer);
    }
}

static int zmk_battery_init(void) {
#if !DT_HAS_CHOSEN(zmk_battery)
    battery = device_get_binding("BATTERY");

    if (battery == NULL) {
        return -ENODEV;
    }

    LOG_WRN("Finding battery device labeled BATTERY is deprecated. Use zmk,battery chosen node.");
#endif

    if (!device_is_ready(battery)) {
        LOG_ERR("Battery device \"%s\" is not ready", battery->name);
        return -ENODEV;
    }

    zmk_battery_start_reporting();
#ifndef CONFIG_SHIELD_KEYCHRON_RS45_ANSI
    check_voltage_when_boot();
#endif
    return 0;
}

static int battery_event_listener(const zmk_event_t *eh) {

    if (as_zmk_activity_state_changed(eh)) {
        switch (zmk_activity_get_state()) {
        case ZMK_ACTIVITY_ACTIVE:
            zmk_battery_start_reporting();
            return 0;
        case ZMK_ACTIVITY_IDLE:
        case ZMK_ACTIVITY_SLEEP:
            k_timer_stop(&battery_timer);
            return 0;
        default:
            break;
        }
    }
    return -ENOTSUP;
}
void zmk_battery_check(void) {
    static uint8_t check_count = 0;
    LOG_DBG("voltage:%d,percent:%d", voltage, last_state_of_charge);
    // DBG_DIRECT("voltage:%d,percent:%d",voltage,last_state_of_charge);
#ifdef CONIG_ZMK_SSD1306
    disp_update_bat(last_state_of_charge);
#endif
    if (zmk_usb_power_on())
        return;
    if (voltage < BAT_VOLTAGE_LOW) {

        // led_charge_set_state(LED_BAT_LOW);
        if (voltage < BAT_VOLTAGE_SHUTDOWN) {
            check_count++;
            if (check_count > 2) {
                bat_shutdown = true;
                set_force_sleep(true);
            }
        } else {
            check_count = 0;
            bat_shutdown = false;
        }
    }
    bat_low_check(0);
}

void check_voltage_when_boot(void) {
    struct sensor_value sensor_voltage;

    uint16_t max = 0;
    uint16_t min = 0xffff;
    uint32_t sum = 0;

    bat_enable_dlps = false;
    bat_shutdown = false;
    for (int i = 0; i < 8; i++) {
        int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
        if (rc != 0) {
            LOG_DBG("Failed to fetch battery values: %d", rc);
            bat_enable_dlps = true;
            return;
        }
        rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &sensor_voltage);
        if (rc != 0) {
            LOG_DBG("Failed to fetch battery values: %d", rc);
            bat_enable_dlps = true;
            return;
        }
        k_msleep(1);
    }
    for (int i = 0; i < 10; i++) {
        int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
        if (rc != 0) {
            LOG_DBG("Failed to fetch battery values: %d", rc);
            bat_enable_dlps = true;
            return;
        }
        rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &sensor_voltage);
        if (rc != 0) {
            LOG_DBG("Failed to fetch battery values: %d", rc);
            bat_enable_dlps = true;
            return;
        }
        voltage = sensor_voltage.val1 * 1000 + sensor_voltage.val2 / 1000;
        sum += voltage;
        if (voltage >= max) {
            max = voltage;
        }
        if (voltage < min) {
            min = voltage;
        }
        k_msleep(2);
        LOG_DBG("voltage:%d,max%d,min:%d", voltage, max, min);
        // DBG_DIRECT("voltage:%d,max%d,min:%d",voltage,max,min);
    }

    sum -= max + min;
    voltage = sum / 8;
    LOG_ERR("ave voltage:%d", voltage);
    // DBG_DIRECT("ave voltage:%d",voltage);
    if (voltage <= BAT_VOLTAGE_SHUTDOWN && voltage > 1750) {

        LOG_ERR("bat low to shutdown");
        bat_shutdown = true;
        k_timer_stop(&battery_timer);
    }
    // if(!bat_shutdown)
    // // zmk_battery_update(battery);
    // {
    //     k_work_submit(&battery_work);
    // }
    last_voltage = voltage;
    last_state_of_charge = lithium_ion_mv_to_pct_rtk(voltage * 18 / 100);
    bat_level = last_state_of_charge;
    LOG_ERR("last_state_of_charge:%d", last_state_of_charge);
    bat_enable_dlps = true;
}
bool bat_is_shutdown(void) { return bat_shutdown; }

void clear_bat_shutdown(void) {
    if (bat_shutdown) {
        bat_shutdown = false;
        zmk_battery_start_reporting();
    }
}
bool rgb_is_allow_sleep(void);
bool bat_is_low(void) {
    static bool islow = false;
    uint32_t bat_low_voltage = BAT_VOLTAGE_LOW;
#ifdef CONFIG_LED_STRIP
    if (!rgb_is_allow_sleep()) {
        bat_low_voltage -= 50;
        // LOG_WRN("bat low voltage:%d",bat_low_voltage);
    }
#endif
    if (voltage < bat_low_voltage && voltage > 0) {
        islow = true;
    } else if (voltage > (BAT_VOLTAGE_LOW + 100)) {
        islow = false;
    }
    return islow;
}
void bat_low_check(uint8_t keypress) {
#ifndef CONFIG_SHIELD_KEYCHRON_RS45_ANSI
    static bool first = true;
    static int64_t last_time = 0;
    int64_t uptime = k_uptime_get();
    int64_t delta_time = uptime - last_time;

    bool low = bat_is_low();
    // LOG_ERR("bat vol:%d, low:%d,delta:%d",voltage,low,(uint32_t)delta_time);
    // DBG_DIRECT("bat vol:%d, low:%d,delta:%d",voltage,low,(uint32_t)delta_time);
    if (!low) {
        first = true;
        // LOG_ERR("bat not low,set first");
    } else if (!zmk_usb_power_on()) {
        if (!keypress) {
            if (first) {
                first = false;
                led_bat_low();
                last_time = uptime;
                // LOG_ERR("first,led on");
            }
        } else if (delta_time > 30000) {
            led_bat_low();
            last_time = uptime;
            LOG_ERR("led_bat_low");
        }
    }
#else
    bool low = bat_is_low();
    disp_low_bat(low && !zmk_usb_power_on());
#endif
}
uint8_t get_battery_level(void) { return last_state_of_charge; }
void stop_battery_timer(void) { k_timer_stop(&battery_timer); }
ZMK_LISTENER(battery, battery_event_listener);

ZMK_SUBSCRIPTION(battery, zmk_activity_state_changed);

SYS_INIT(zmk_battery_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
