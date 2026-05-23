/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/settings/settings.h>
#include <zmk/app_wdt.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include "trace.h"

#include <pm.h>
#include "power_manager_unit_platform.h"
#include <rtl_aon_wdt.h>
#include <rtl_wdt.h>

static void app_wdt_timeout_cb(struct k_timer *timer);
static K_TIMER_DEFINE(app_wdt_timer, app_wdt_timeout_cb, NULL);
const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog));


void app_system_reset(uint8_t flag)
{
    struct wdt_timeout_cfg wdt_config = {
        .flags = flag,
        .window.max = 0, //set window.max to 0: reboot immediately
    };
    wdt_install_timeout(wdt,&wdt_config);
}

int app_wdt_init(void)
{
    if(!device_is_ready(wdt))
    {
        LOG_DBG("%s:device not ready",wdt->name);
        return 1;
    }
    k_timer_start(&app_wdt_timer, K_MSEC(4000),K_MSEC(4000));
    return 0;
}

static void app_wdt_timeout_cb(struct k_timer *timer)
{
    // DBG_DIRECT("app_wdt_timeout_cb");
#if 0//CONFIG_PM    
    LOG_ERR("pm mode:%d,err:%d",platform_pm_get_power_mode(),platform_pm_get_error_code());
#endif     
    wdt_feed(wdt,0);
    return;
}
void app_wdt_stop(void)
{
    k_timer_stop(&app_wdt_timer);
    wdt_disable(wdt);
}
void app_wdt_start(void)
{
    // AON_WDT_Start(AON_WDT, 5000, RESET_ALL);
    WDT_Start(5000, RESET_ALL);
    k_timer_start(&app_wdt_timer, K_MSEC(4000),K_MSEC(4000));
}
void app_wdt_feed(void)
{
     wdt_feed(wdt,0);
}
SYS_INIT(app_wdt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);