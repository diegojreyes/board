/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk, 4); // CONFIG_ZMK_LOG_LEVEL);

#include <zmk/matrix.h>
#include <zmk/kscan.h>
#include <zmk/display.h>
#include <zmk/mode_monitor.h>
#include <zmk/ble.h>
#include <drivers/ext_power.h>
#include "trace.h"
#include <app_version.h>
#include <zmk/endpoints.h>
#include <zmk/activity.h>
#include <zmk/app_wdt.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log_ctrl.h>
#include <zmk/board.h>
#include "reset_reason.h"
#include "aon_reg.h"
#include <zmk/mp_test/mp_test.h>
#include <zmk/mp_test/single_tone.h>
#include <zephyr/sys/reboot.h>
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif
#include <zmk/keymap.h>

extern void WDG_SystemReset(int wdt_mode, int reset_reason);

void enable_rgb_thread(void);
static uint32_t actual_mhz = 40;

#if CONFIG_PM

#include <pm.h>
#include "power_manager_unit_platform.h"
extern bool bat_enable_dlps;
extern bool settings_enabled_dlps;
extern bool macro_enabled_dlps;
extern bool kscan_enabled_dlps;
bool is_app_enabled_dlps = true;
uint8_t my_scdfu_is_active(void);
#ifdef CONFIG_LED_STRIP
bool rgb_is_allow_sleep(void);
#else
#define rgb_is_allow_sleep() true
#endif
#ifdef CONFIG_ZMK_SSD1306
bool i2c_is_allow_dlps(void);
#endif

enum PMCheckResult app_enter_dlps_check(void) {
    static bool last_enable = false;
    static bool last_rgb = false;

    if (last_enable != is_app_enabled_dlps || last_rgb != rgb_is_allow_sleep()) {
        LOG_ERR("app check dlps flag %d,rgb:%d", is_app_enabled_dlps, rgb_is_allow_sleep());
        last_enable = is_app_enabled_dlps;
        last_rgb = rgb_is_allow_sleep();
    }

    return (is_app_enabled_dlps && rgb_is_allow_sleep() && !my_scdfu_is_active() &&
            bat_enable_dlps && settings_enabled_dlps && macro_enabled_dlps && kscan_enabled_dlps
#ifdef CONFIG_ZMK_SSD1306
            && i2c_is_allow_dlps()
#endif
                )
               ? PM_CHECK_PASS
               : PM_CHECK_FAIL;
}

static int app_dlps_check_cb_register(void) {
    LOG_DBG("app_dlps_check_cb_register");
    platform_pm_register_callback_func((void *)app_enter_dlps_check, PLATFORM_PM_CHECK);
    // platform_pm_register_callback_func_with_priority((void *)app_enter_dlps_check,
    // PLATFORM_PM_CHECK, 1);
    return 0;
}

static bool is_check_status_before_enter_wfi = true;
/**
 * @brief  CPU can enter wfi or dlps with checking all module status pass
 * @param  None
 * @return None
 */
void pm_check_status_before_enter_wfi_or_dlps(void) {
    if (!is_check_status_before_enter_wfi) {
        power_mode_resume();
        LOG_DBG("power_mode_resume");
        is_check_status_before_enter_wfi = true;
    }
}

/**
 * @brief  CPU enter wfi without checking all module status
 * @param  None
 * @return None
 */
void pm_no_check_status_before_enter_wfi(void) {
    if (is_check_status_before_enter_wfi) {
        power_mode_pause();
        LOG_DBG("power_mode_pause!");
        is_check_status_before_enter_wfi = false;
    }
}
#endif

bool bat_is_shutdown(void);
void zmk_rgb_sleep(void);
#if CONFIG_SHIELD_KEYCHRON_RS45_ANSI
void check_voltage_when_boot(void);
void read_mode(void);
void mode_init(void);
#endif

int main(void) {
    LOG_INF("Welcome to ZMK!,Ver:%s,freq:%d Mhz\n", APP_VERSION_STRING, actual_mhz);
#ifdef CONFIG_SHIELD_KEYCHRON_RS45_ANSI
    read_mode();
    check_voltage_when_boot();
#endif
#if CONFIG_PM
    app_dlps_check_cb_register();
#endif

    AON_NS_REG0X_APP_TYPE aon_0x1ae0 = {.d32 = AON_REG_READ(AON_NS_REG0X_APP)};
    uint32_t sw_reset_type = aon_0x1ae0.reset_reason;
    LOG_WRN("reset reson:%x", sw_reset_type);
    if (sw_reset_type == SWITCH_TO_TEST_MODE) {
        // app_mode.is_in_single_test_mode = true;
#if (MP_TEST_SINGLE_TONE_MODE == GAP_LAYER_SINGLE_TONE_INTERFACE)
        LOG_INF("GAP_SINGLE_TONE_MODE");
        // not currently supported
        // if (app_global_data.is_watchdog_enable)
        { app_wdt_stop(); /* Avoid unexpected reboot */ }
#elif MP_TEST_SINGLE_TONE_MODE == HCI_LAYER_SINGLE_TONE_INTERFACE
        LOG_INF("HCI_SINGLE_TONE_MODE");
        // if (app_global_data.is_watchdog_enable)
        { app_wdt_stop(); /* Avoid unexpected reboot */ }
        single_tone_init();
#endif // MP_TEST_SINGLE_TONE_MODE
    } else {
        // #if CONFIG_SHIELD_KEYCHRON_RS45_ANSI
        //         mode_init();
        // #endif
        if (zmk_kscan_init(DEVICE_DT_GET(ZMK_MATRIX_NODE_ID)) != 0) {
            return -ENOTSUP;
        }
#ifdef CONFIG_ZMK_SSD1306
        disp_init();
#endif
        if (bat_is_shutdown()) {
            LOG_ERR("shutdown prepare");

            pm_check_status_before_enter_wfi_or_dlps();

            set_force_sleep(true);
#if CONFIG_LED_STRIP
            zmk_rgb_sleep();
#endif
            // set_state(ZMK_ACTIVITY_SLEEP);
            // #ifdef CONFIG_ZMK_SSD1306
            //         disp_sleep();
            // #endif
        }
        // else{

        // }
#ifdef CONFIG_SHIELD_KEYCHRON_RS87_ANSI
        uint8_t get_mac_win_layer(void);
#define MAC_LAYER 0
#define WIN_LAYER 2
        zmk_keymap_layer_activate(get_mac_win_layer() ? WIN_LAYER : MAC_LAYER);
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
        void set_backup_winlock(uint8_t mac);
        set_backup_winlock(get_mac_win_layer() == 0);
#endif
#endif
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
        void winlock_led_set(void);
        if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB)
            winlock_led_set();
#endif
#if (THE_WAY_TO_ENTER_MP_TEST_MODE == ENTER_MP_TEST_MODE_BY_GPIO_TRIGGER)
        bool mode_type = mp_test_mode_check_and_enter();
        if (mode_type) {
            LOG_DBG("reboot to switch to test mode");
            // sys_reboot(SWITCH_TO_TEST_MODE);
            WDG_SystemReset(0, SWITCH_TO_TEST_MODE);
        }
#endif

#ifdef CONFIG_ZMK_DISPLAY
        zmk_display_init();
#endif /* CONFIG_ZMK_DISPLAY */
    }
    return 0;
}

// SYS_INIT(app_dlps_check_cb_register, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
int32_t pm_cpu_freq_set(uint32_t required_mhz, uint32_t *actual_mhz);
int set_cpu_high_freq(void) {
    LOG_WRN("set freq 125Mhz");
    int ret = pm_cpu_freq_set(125, &actual_mhz);
    // k_busy_wait(10000);
    return ret;
}
int set_cpu_noraml_freq(void) { return pm_cpu_freq_set(40, &actual_mhz); }

SYS_INIT(set_cpu_high_freq, POST_KERNEL, 1);

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf) {
    ARG_UNUSED(esf);

    LOG_PANIC();
    LOG_ERR("Reboot system");

    app_system_reset(WDT_FLAG_RESET_SOC);
}

uint8_t get_report_rate(void);
void user_set_scan_period(uint32_t period);
uint32_t get_default_scan_period(void);
void lowpower_settings(void) {
    uint32_t target_cpu_freq = 125;

    if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
        if (get_report_rate() > 8) {
            target_cpu_freq = 40;
            is_app_enabled_dlps = true;
            bt_power_mode_set(BTPOWER_DEEP_SLEEP);
            user_set_scan_period(10);
        } else {
            bt_power_mode_set(BTPOWER_ACTIVE);
            is_app_enabled_dlps = false;
            user_set_scan_period(get_default_scan_period());
        }
    } else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE) {
        target_cpu_freq = 40;
        is_app_enabled_dlps = true;
        user_set_scan_period(10);
    } else {
        target_cpu_freq = 125;
        is_app_enabled_dlps = false;
        user_set_scan_period(get_default_scan_period());
    }
    LOG_ERR("cur freq:%d", actual_mhz);
    if (actual_mhz != target_cpu_freq) {
        pm_cpu_freq_set(target_cpu_freq, &actual_mhz);
        k_busy_wait(10000);
    }

    LOG_ERR("trans:%d,target cpu freq:%d,now:%d,dlps:%d,report rate:%d",
            zmk_endpoints_selected().transport, target_cpu_freq, actual_mhz, is_app_enabled_dlps,
            get_report_rate());
}
void enter_lowpower(void) {
    pm_cpu_freq_set(40, &actual_mhz);
    is_app_enabled_dlps = true;
}
void exit_lowpower(void) {
    pm_cpu_freq_set(125, &actual_mhz);
    is_app_enabled_dlps = false;
}
