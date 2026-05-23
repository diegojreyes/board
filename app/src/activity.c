/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/poweroff.h>

#include <zephyr/logging/log.h>
#include <zmk/endpoints.h>
#include <zmk/ppt/keyboard_ppt_app.h>
#if CONFIG_LED_STRIP
#include "rgb/rgb_matrix.h"
#endif
#include <zmk/app_wdt.h>
#include <zephyr/drivers/watchdog.h>
#include <zmk/leds.h>
#include "pm.h"
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define USE_K_TIMER

#ifndef USE_K_TIMER
#include <zephyr/drivers/counter.h>
#define TIMER DT_NODELABEL(timer2)
#define ALARM_CHANNEL_ID 0
#define DELAY 1000000
struct counter_alarm_cfg alarm_cfg;
struct counter_top_cfg counter_cfg;
#endif

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>
#include "trace.h"
#include <zmk/activity.h>
#include <zephyr/settings/settings.h>

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#endif

extern bool is_app_enabled_dlps;
void bat_low_check(uint8_t keypress);
bool all_key_up(void);
bool bat_is_shutdown(void);
uint8_t zmk_usb_power_on(void);
bool force_to_sleep(void);
void stop_battery_timer(void);
void keyboard_matrix_shutdown(void);
void pm_check_status_before_enter_wfi_or_dlps(void);

static int activity_init(void);
// Reimplement some of the device work from Zephyr PM to work with the new `sys_poweroff` API.
// TODO: Tweak this to smarter runtime PM of subsystems on sleep.

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_REALTEK_USING_SDK_LIB)
TYPE_SECTION_START_EXTERN(const struct device *, zmk_pm_device_slots);

#if !defined(CONFIG_PM_DEVICE_RUNTIME_EXCLUSIVE)
/* Number of devices successfully suspended. */
static size_t zmk_num_susp;

static int zmk_pm_suspend_devices(void) {
    const struct device *devs;
    size_t devc;

    devc = z_device_get_all_static(&devs);

    zmk_num_susp = 0;

    for (const struct device *dev = devs + devc - 1; dev >= devs; dev--) {
        int ret;

        /*
         * Ignore uninitialized devices, busy devices, wake up sources, and
         * devices with runtime PM enabled.
         */
        if (!device_is_ready(dev) || pm_device_is_busy(dev) || pm_device_state_is_locked(dev) ||
            pm_device_wakeup_is_enabled(dev) || pm_device_runtime_is_enabled(dev)) {
            continue;
        }
        LOG_ERR("suspend dev:%s", dev->name);
        ret = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
        /* ignore devices not supporting or already at the given state */
        if ((ret == -ENOSYS) || (ret == -ENOTSUP) || (ret == -EALREADY)) {
            continue;
        } else if (ret < 0) {
            LOG_ERR("Device %s did not enter %s state (%d)", dev->name,
                    pm_device_state_str(PM_DEVICE_STATE_SUSPENDED), ret);
            return ret;
        }

        TYPE_SECTION_START(zmk_pm_device_slots)[zmk_num_susp] = dev;
        zmk_num_susp++;
    }

    return 0;
}

static void zmk_pm_resume_devices(void) {
    for (int i = (zmk_num_susp - 1); i >= 0; i--) {
        pm_device_action_run(TYPE_SECTION_START(zmk_pm_device_slots)[i], PM_DEVICE_ACTION_RESUME);
    }

    zmk_num_susp = 0;
}
#endif /* !CONFIG_PM_DEVICE_RUNTIME_EXCLUSIVE */
#endif /* CONFIG_PM_DEVICE */

bool is_usb_power_present(void) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    return zmk_usb_is_powered();
#else
    return false;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
}
void activity_expiry_function(struct k_timer *_timer);
K_TIMER_DEFINE(activity_timer, activity_expiry_function, NULL);
static enum zmk_activity_state activity_state;
static enum zmk_activity_state last_sleep_state;
static uint32_t activity_last_uptime;

#define MAX_IDLE_MS CONFIG_ZMK_IDLE_TIMEOUT

#if IS_ENABLED(CONFIG_ZMK_SLEEP)
#define MAX_SLEEP_MS CONFIG_ZMK_IDLE_SLEEP_TIMEOUT
#endif

uint32_t max_sleep_time = MAX_SLEEP_MS;
uint32_t max_idle_time = MAX_IDLE_MS;

int raise_event(void) {
    return raise_zmk_activity_state_changed(
        (struct zmk_activity_state_changed){.state = activity_state});
}

int set_state(enum zmk_activity_state state) {
    if (activity_state == state)
        return 0;

    activity_state = state;
    return raise_event();
}

enum zmk_activity_state zmk_activity_get_state(void) { return activity_state; }
bool zmk_rgb_test(struct zmk_position_state_changed *pos_state);
uint8_t max_keyboard_pos(void);
int activity_event_listener(const zmk_event_t *eh) {
    activity_last_uptime = k_uptime_get();
    struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    struct zmk_sensor_event *sensor = as_zmk_sensor_event(eh);
    // LOG_ERR("pos:%d,max_pos:%d",pos->position,max_keyboard_pos());
    // set_force_sleep(false);
    if (pos != NULL) {
        if (last_sleep_state) {
            LOG_ERR("reset");
            last_sleep_state = 0;
            // activity_init();
            app_wdt_start();
            app_system_reset(WDT_FLAG_RESET_SOC);
        }
        if (
#ifdef CONFIG_LED_STRIP
            !zmk_rgb_test(pos) &&
#endif
            (pos->state) &&
            (pos->position < max_keyboard_pos())) // kc control not keys,skip charge!
        {
            // is_app_enabled_dlps =false;
            if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE) {
                zmk_ble_reconn();
            } else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
                zmk_ppt_reconn();
                if (zmk_ppt_is_ready()) {
                    if (bt_power_mode_get() == BTPOWER_DEEP_SLEEP) {
                        uint8_t get_report_rate(void);
                        if (get_report_rate() <= 8) {
                            LOG_ERR("ppt active!!!!");
                            bt_power_mode_set(BTPOWER_ACTIVE);
                            is_app_enabled_dlps = false;
                        }
                    }
                }
            }
            // is_app_enabled_dlps =false;
            // LOG_ERR("last sleep state:%d",last_sleep_state);

            set_force_sleep(false);

            bat_low_check(1);
        }
    }
    if (sensor) {
        // is_app_enabled_dlps =false;
        if (last_sleep_state) {
            last_sleep_state = 0;
            LOG_ERR("reset");
            app_wdt_start();
            app_system_reset(WDT_FLAG_RESET_SOC);
        }
    }
    return set_state(ZMK_ACTIVITY_ACTIVE);
}
extern uint8_t usb_configured;
void activity_work_handler(struct k_work *work) {
    int32_t current = k_uptime_get();
    int32_t inactive_time = current - activity_last_uptime;
    // DBG_DIRECT("cur:%d",current);
    //  if(bat_is_shutdown()&& !zmk_usb_power_on())
    //      set_force_sleep(true);
    if ((inactive_time > max_sleep_time || force_to_sleep() || bat_is_shutdown()))
        LOG_ERR("force to sleep:%d,bat:%d,inactive:%d", force_to_sleep(), bat_is_shutdown(),
                inactive_time > MAX_SLEEP_MS);

#if IS_ENABLED(CONFIG_ZMK_SLEEP)
    if ((inactive_time > max_sleep_time || force_to_sleep() || bat_is_shutdown()) &&
        (zmk_endpoints_selected().transport != ZMK_TRANSPORT_USB) && all_key_up())
    // && !get_rgb_test_start())
    {
        keyboad_led_set_onoff(0);
#if CONFIG_LED_STRIP
        rgb_matrix_config.enable = 0;
        rgb_led_indicators.rgb_enable = 0;
#endif
        LOG_ERR("go to sleep");
        // DBG_DIRECT("go to sleep");
        if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE) {
            void zmk_ble_sleep(void);
            zmk_ble_sleep();
        } else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
            void zmk_ppt_disconnect(void);
            zmk_ppt_disconnect();
        }
#ifdef CONFIG_ZMK_SSD1306
        disp_sleep();
#endif
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
        void winlock_led_onoff(uint8_t onff);
        winlock_led_onoff(0);
#endif
        pm_check_status_before_enter_wfi_or_dlps();
        k_timer_stop(&activity_timer);
        stop_battery_timer();
        app_wdt_stop();
        k_msleep(100);

        // Put devices in suspend power mode before sleeping
        set_state(ZMK_ACTIVITY_SLEEP);
        is_app_enabled_dlps = true;
#ifndef CONFIG_LED_STRIP
        void leds_pwroff(void);
        leds_pwroff();
#endif
        bt_power_mode_set(BTPOWER_DEEP_SLEEP);
        // if (zmk_pm_suspend_devices() < 0) {
        //     LOG_ERR("Failed to suspend all the devices");
        //     zmk_pm_resume_devices();
        //     return;
        // }

        // app_wdt_stop();
        // sys_poweroff();
        // power_mode_set(1);
        last_sleep_state = 1;

        // aon_write_reg(0xf2);
        aon_write_state(AON_STATE_SLEEP);
    } else {
#endif /* IS_ENABLED(CONFIG_ZMK_SLEEP) */
        if (inactive_time > 300000) {
            if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
                if (bt_power_mode_get() == BTPOWER_ACTIVE) {
                    bt_power_mode_set(BTPOWER_DEEP_SLEEP);
                    is_app_enabled_dlps = true;
                    LOG_ERR("ppt low power");
                }
            }
        }
        if (inactive_time > max_idle_time) {

            set_state(ZMK_ACTIVITY_IDLE);
        } else if (inactive_time > 10000) {
            // if(!keyboard_get_led_state())
            // is_app_enabled_dlps =true;;
            if (bat_is_shutdown()) {
                pm_check_status_before_enter_wfi_or_dlps();
#ifdef CONFIG_ZMK_SSD1306
                disp_sleep();
#endif
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
                void winlock_led_onoff(uint8_t onoff);
                winlock_led_onoff(0);
#endif
                // keyboard_matrix_shutdown();
                last_sleep_state = 1;
                bt_power_mode_set(BTPOWER_DEEP_SLEEP);
                k_timer_stop(&activity_timer);
                stop_battery_timer();
                app_wdt_stop();
            }
            // note: enable ppt to sleep ! ppt sleep take about 600us to wakeup
            //  if(zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT)
            //  {

            //     pm_check_status_before_enter_wfi_or_dlps();
            // }
            // DBG_DIRECT("inactive:%d ms",inactive_time);
        }
    }
}

K_WORK_DEFINE(activity_work, activity_work_handler);

void activity_expiry_function(struct k_timer *_timer) { k_work_submit(&activity_work); }

#ifndef USE_K_TIMER
static void counter_interrupt_fn(const struct device *dev, void *user_data) {

    LOG_ERR("!!! Alarm !!!");
    DBG_DIRECT("!!! Alarm !!!");
    k_work_submit(&activity_work);
}
#endif

static int activity_init(void) {
    activity_last_uptime = k_uptime_get();
#ifdef USE_K_TIMER
    k_timer_start(&activity_timer, K_SECONDS(1), K_SECONDS(1));
#else
    const struct device *const counter_dev = DEVICE_DT_GET(TIMER);
    if (!device_is_ready(counter_dev)) {
        LOG_ERR("device not ready.\n");
        return 0;
    }

    // alarm_cfg.flags = 0;
    // alarm_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY);
    // alarm_cfg.callback = counter_interrupt_fn;
    // alarm_cfg.user_data = &alarm_cfg;
    // int err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID,
    //              &alarm_cfg);

    counter_cfg.callback = counter_interrupt_fn;
    counter_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY);
    counter_cfg.flags = 0;
    counter_cfg.user_data = &counter_cfg;

    int err = counter_set_top_value(counter_dev, &counter_cfg);

    if (err) {
        LOG_ERR("counter set err:%d", err);
    }
    counter_start(counter_dev);
#endif
    return 0;
}

ZMK_LISTENER(activity, activity_event_listener);
ZMK_SUBSCRIPTION(activity, zmk_position_state_changed);
ZMK_SUBSCRIPTION(activity, zmk_sensor_event);

SYS_INIT(activity_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

lpm_settings_t lpm_set = {.max_idle_time = CONFIG_ZMK_IDLE_TIMEOUT / 1000,
                          .max_sleep_time = CONFIG_ZMK_IDLE_SLEEP_TIMEOUT / 1000};
void update_lpm_set(uint16_t idle_time, uint16_t sleep_time) {
    max_idle_time = idle_time * 1000;
    max_sleep_time = sleep_time * 1000;
}

void get_lpm_set(uint16_t *idle_time, uint16_t *sleep_time) {
    *idle_time = lpm_set.max_idle_time;
    *sleep_time = lpm_set.max_sleep_time;
}
void set_lpm_set(uint16_t idle_time, uint16_t sleep_time) {
    lpm_set.max_idle_time = idle_time;
    lpm_set.max_sleep_time = sleep_time;
    update_lpm_set(lpm_set.max_idle_time, lpm_set.max_sleep_time);
    settings_save_one("launcher/lpm_set", &lpm_set, sizeof(lpm_set));
}

void raise_active_event(void) {
    // struct zmk_position_state_changed pos_state={
    //     .position =ZMK_KEYMAP_LEN+0x01,
    //     .state = 1,
    //     .timestamp = k_uptime_get_32()
    // };
    // raise_zmk_position_state_changed(pos_state);
    // pos_state.state =0;
    // raise_zmk_position_state_changed(pos_state);
    activity_last_uptime = k_uptime_get();
    set_state(ZMK_ACTIVITY_ACTIVE);

    LOG_ERR("raise active event");
}