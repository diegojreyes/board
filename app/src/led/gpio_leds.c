/*
 * Copyright (c) 2018-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/led.h>
#include <zephyr/pm/device.h>
#include "led_effect.h"
#include <zmk/leds.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/led_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/endpoints.h>
#include <zephyr/logging/log.h>
#if CONFIG_LED_STRIP
#include "../rgb/rgb_matrix.h"
#endif
#include <zmk/battery.h>
#include <zmk/ppt/keyboard_ppt_app.h>
#include "trace.h"
#include "aon_reg.h"
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint8_t keyboard_led_state;

extern bool is_app_enabled_dlps;
static const struct led_effect led_peer_state_effect[] = {
    [LED_PEER_STATE_DISCONNECTED] = LED_EFFECT_LED_OFF(),
    [LED_PEER_STATE_CONNECTED] = LED_EFFECT_LED_ON_GO_OFF(LED_COLOR(255, 255, 0), 3000, 100),
    [LED_PEER_STATE_PAIR] = LED_EFFECT_LED_BLINK(1000, LED_COLOR(255, 255, 0)),
    [LED_PEER_STATE_RECONN] = LED_EFFECT_LED_FLASH(
        3, 200, LED_COLOR(255, 255, 0)), // LED_EFFECT_LED_BLINK(200, LED_COLOR(150, 0, 0)),
    [LED_PEER_STATE_FAILE] = LED_EFFECT_LED_FLASH(3, 300, LED_COLOR(150, 0, 0)),
    [LED_PEER_STATE_POWER_ON] = LED_EFFECT_LED_ON_GO_OFF(LED_COLOR(255, 255, 0), 2500, 100),
    [LED_PEER_STATE_ON_100MS] = LED_EFFECT_LED_ON_GO_OFF(LED_COLOR(100, 100, 100), 200, 100),
    [LED_PEER_STATE_BAT_LOW] = LED_EFFECT_LED_FLASH3(
        5, LED_COLOR(255, 255, 255)), // LED_EFFECT_LED_BLINK(1000, LED_COLOR(255, 0,0)),
    [LED_PEER_STATE_BAT_CHARGING] = LED_EFFECT_LED_ON(LED_COLOR(255, 0, 0)),
    [LED_PEER_STATE_BAT_CHARGEDONE] = LED_EFFECT_LED_ON(LED_COLOR(0, 128, 0)),
    [LED_PEER_STATE_RECOVER] = LED_EFFECT_LED_FLASH2(3, LED_COLOR(255, 255, 0)),
    [LED_PEER_STATE_RECOVER1] = LED_EFFECT_LED_FLASH2(3, LED_COLOR(255, 0, 0)),

};

#define LED_ID(led) ((led) - &leds[0])

struct led {
    const struct device *dev;
    uint8_t color_count;

    struct led_color color;
    const struct led_effect *effect;
    uint16_t effect_step;
    uint16_t effect_substep;

    struct k_work_delayable work;
};

#define _LED_COLOR_ID(_unused) 0,

#define _LED_COLOR_COUNT(id)                                                                       \
    static const uint8_t led_colors_##id[] = {DT_INST_FOREACH_CHILD(id, _LED_COLOR_ID)};

DT_INST_FOREACH_STATUS_OKAY(_LED_COLOR_COUNT)

#define _LED_INSTANCE_DEF(id)                                                                      \
    {                                                                                              \
        .dev = DEVICE_DT_GET(DT_DRV_INST(id)),                                                     \
        .color_count = ARRAY_SIZE(led_colors_##id),                                                \
    },
/**/

static uint8_t led_ble_index = 0;
static uint8_t led_num_index = 0;
static uint8_t led_caps_index = 0;
static uint8_t led_ppt_index = 0;
static uint8_t led_ppt0_index = 0;
static uint8_t led_ble0_index = 0;
static uint8_t led_bat_index = 0;

static uint8_t leds_num;

typedef union {
    struct {
        uint8_t num_on : 1;
        uint8_t caps_on : 1;
        uint8_t ble_on : 1;
        uint8_t ppt_on : 1;
        uint8_t bat_on : 1;
        uint8_t running : 1;
        uint8_t exclude : 1;
        uint8_t power_on : 1;

        uint8_t led_state_pending : 4;
        uint8_t led_index : 4;
    };
    struct {
        uint8_t on_status : 5;
    };

} _led_indicator_state;
static _led_indicator_state led_indicator_state;
#if DT_NODE_HAS_STATUS(DT_NODELABEL(pwr_ctrl), okay)
static struct gpio_dt_spec led_pwr_ctrl = GPIO_DT_SPEC_GET(DT_NODELABEL(pwr_ctrl), gpios);
#endif
static struct led leds[10] = {
    // DT_INST_FOREACH_STATUS_OKAY(_LED_INSTANCE_DEF)
};
static void led_update(struct led *led);
void leds_pwroff(void);
void gpio_led_bat_display_off(void);

static int set_color_one_channel(struct led *led, struct led_color *color) {
    /* For a single color LED convert color to brightness. */
    unsigned int brightness = 0;

    for (size_t i = 0; i < ARRAY_SIZE(color->c); i++) {
        brightness += color->c[i];
    }
    brightness /= ARRAY_SIZE(color->c);

    return led_set_brightness(led->dev, 0, brightness);
}

static int set_color_all_channels(struct led *led, struct led_color *color) {
    int err = 0;

    // for (size_t i = 0; (i < ARRAY_SIZE(color->c)) && !err; i++) {
    for (size_t i = 0; (i < led->color_count) && !err; i++) {
        err = led_set_brightness(led->dev, i, color->c[i]);
    }

    return err;
}

static void set_color(struct led *led, struct led_color *color) {
    int err;
    // LOG_WRN("count:%d,color:%d,%d,%d",led->color_count,color->c[0],color->c[1],color->c[2]);
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(pwr_ctrl), okay))
    gpio_pin_set_dt(&led_pwr_ctrl, 1);
#endif
    // if (led->color_count == ARRAY_SIZE(color->c)) {
    if (led->color_count > 1) {
        err = set_color_all_channels(led, color);
    } else {
        err = set_color_one_channel(led, color);
    }

    if (err) {
        LOG_ERR("Cannot set LED brightness (err: %d)", err);
    }
}

static void set_off(struct led *led) {
    struct led_color nocolor = {0};

    set_color(led, &nocolor);
}

static void work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct led *led = CONTAINER_OF(dwork, struct led, work);
    //  LOG_INF("led
    //  %d,time:%d,%d",LED_ID(led),led->effect->steps[0].substep_time,led->effect->steps[1].substep_time);
    const struct led_effect_step *effect_step = &led->effect->steps[led->effect_step];

    __ASSERT_NO_MSG(effect_step->substep_count > 0);
    int substeps_left = effect_step->substep_count - led->effect_substep;

    for (size_t i = 0; i < ARRAY_SIZE(led->color.c); i++) {
        int diff = (effect_step->color.c[i] - led->color.c[i]) / substeps_left;
        led->color.c[i] += diff;
    }
    set_color(led, &led->color);

    led->effect_substep++;
    if (led->effect_substep == effect_step->substep_count) {
        led->effect_substep = 0;
        led->effect_step++;

        if (led->effect_step == led->effect->step_count) {
            if (led->effect->loop_forever) {
                led->effect_step = 0;
            } else {
                LOG_WRN("led effect finish,led:%d,nums:%d", LED_ID(led), leds_num);
                if (LED_ID(led) == led_ble_index ||
                    (LED_ID(led) >= led_ble0_index && LED_ID(led) < (led_ble0_index + 3))) {
                    led_indicator_state.ble_on = 0;
                } else if (LED_ID(led) == led_ppt_index || LED_ID(led) == led_ppt0_index) {
                    led_indicator_state.ppt_on = 0;
                } else if (LED_ID(led) == led_bat_index) {
                    led_indicator_state.bat_on = 0;
                }
                if (led_indicator_state.on_status == 0) {
                    leds_pwroff();
                }
#if CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI
                if (led->effect == &led_peer_state_effect[LED_PEER_STATE_POWER_ON] &&
                    (LED_ID(led) >= (led_ble0_index + 2)))
#else
                if (led->effect == &led_peer_state_effect[LED_PEER_STATE_POWER_ON] &&
                    (LED_ID(led) >= (led_ble_index)))
#endif
                {
                    led_indicator_state.power_on = 0;

                    LOG_ERR("poweron stop");
                    keyboad_led_set_onoff(keyboard_get_led_state().raw);
#if (!defined(CONFIG_SHIELD_KEYCHRON_RS87_ANSI) &&                                                 \
     !defined(CONFIG_SHIELD_KEYCHRON_Q1_ULTRA_ANSI) &&                                             \
     !defined(CONFIG_SHIELD_KEYCHRON_Q3_ULTRA_ANSI) &&                                             \
     !defined(CONFIG_SHIELD_KEYCHRON_Q6_ULTRA_ANSI) &&                                             \
     !defined(CONFIG_SHIELD_KEYCHRON_V6_ULTRA_ANSI) &&                                             \
     !defined(CONFIG_SHIELD_KEYCHRON_V3_ULTRA_ANSI) &&                                             \
     !defined(CONFIG_SHIELD_KEYCHRON_V5_ULTRA_ANSI) &&                                             \
     !defined(CONFIG_SHIELD_KEYCHRON_V1_ULTRA_ANSI) &&                                             \
     !defined(CONFIG_SHIELD_KEYCHRON_V10_ULTRA_ANSI))
                    if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE) {
                        if (zmk_ble_active_profile_is_connected()) {
                            LOG_ERR("restore led connected");
                            gpio_led_blue_set_state(LED_PEER_STATE_CONNECTED,
                                                    zmk_ble_active_profile_index());
                        } else {
                            LOG_ERR("restore led re connected");
                            zmk_ble_reconn();
                        }

                    } else if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_PPT) {
                        if (zmk_ppt_is_ready()) {
                            LOG_ERR("restore 24g led connected");
                            gpio_led_24G_set_state(LED_PEER_STATE_CONNECTED);
                        } else {
                            LOG_ERR("restore 24g led re connected");
                            zmk_ppt_reconn();
                        }
                    }
#endif
                }

                led_indicator_state.running = 0;
                // is_app_enabled_dlps =true;
                //  DBG_DIRECT("led:%d,onstatus:%d",LED_ID(led),led_indicator_state.on_status);
            }
        }
    }

    if (led->effect_step < led->effect->step_count) {
        int32_t next_delay = led->effect->steps[led->effect_step].substep_time;
        //  LOG_INF("led %d,led->effect_step:%d,next
        //  delay:%d",LED_ID(led),led->effect_step,next_delay);
        k_work_reschedule(&led->work, K_MSEC(next_delay));
    }
}

static void led_update(struct led *led) {
    k_work_cancel_delayable(&led->work);

    led->effect_step = 0;
    led->effect_substep = 0;

    if (!led->effect) {
        LOG_DBG("No effect set");
        return;
    }

    __ASSERT_NO_MSG(led->effect->steps);

    if (led->effect->step_count > 0) {
        int32_t next_delay = led->effect->steps[led->effect_step].substep_time;
        LOG_INF("next delay:%d", next_delay);
        if (led->effect == &led_peer_state_effect[LED_PEER_STATE_PAIR])
            k_work_reschedule(&led->work, K_MSEC(10)); // sync with rgb
        else
            k_work_reschedule(&led->work, K_MSEC(next_delay));
    } else {
        LOG_WRN("LED effect with no effect");
    }
}

int leds_init(void) {
    int err = 0;
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(pwr_ctrl), okay))
    {
        gpio_pin_configure_dt(&led_pwr_ctrl, GPIO_OUTPUT);
        gpio_pin_set_dt(&led_pwr_ctrl, 1);
    }
#endif
    uint8_t index = 1;

#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_numlock), okay))
    {
        led_num_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_numlock));
        leds[index].color_count = 1;
        LOG_WRN("num led:%d", index);
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_caps), okay))
    {
        led_caps_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_caps));
        leds[index].color_count = 1;
        LOG_WRN("cap led:%d", index);
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_ppt), okay))
    {
        led_ppt_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_ppt));
        leds[index].color_count = 1;
        LOG_WRN("ppt led:%d", index);
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_ble), okay))
    {
        led_ble_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_ble));
        leds[index].color_count = 1;
        LOG_WRN("ble led:%d", index);
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_ble0), okay))
    {
        led_ble0_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_ble0));
        leds[index].color_count = 1;
        LOG_WRN("ble0 led:%d", index);
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_ble1), okay))
    {
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_ble1));
        leds[index].color_count = 1;
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_ble2), okay))
    {
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_ble2));
        leds[index].color_count = 1;
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_ppt0), okay))
    {
        led_ppt0_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_ppt0));
        leds[index].color_count = 1;
        LOG_WRN("ppt0 led:%d", index);
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_rgb), okay))
    {
        led_bat_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_rgb));
        leds[index].color_count = 3;
        index++;
    }
#endif
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(led_red), okay))
    {
        led_bat_index = index;
        leds[index].dev = DEVICE_DT_GET(DT_NODELABEL(led_red));
        leds[index].color_count = 1;
        LOG_WRN("bat led:%d", index);
        index++;
    }
#endif
    leds_num = index;
    memset(&led_indicator_state, sizeof(led_indicator_state), 0);
    // BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) > 0, "No LEDs defined");

    for (size_t i = 1; (i < leds_num) && !err; i++) {
        struct led *led = &leds[i];
        LOG_INF("led :%d,color count:%d", i, led->color_count);

        __ASSERT_NO_MSG((led->color_count == 1) || (led->color_count == 3));

        if (!device_is_ready(led->dev)) {
            LOG_ERR("Device %s is not ready", led->dev->name);
            err = -ENODEV;
        } else {
            k_work_init_delayable(&led->work, work_handler);
            // led_update(led);
            set_off(led);
        }
    }
    gpio_led_power_on();
    return err;
}

void leds_start(void) {
    for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_ZMK_LEDS_GPIO)
        /* Zephyr power management API is not implemented for GPIO LEDs.
         * LEDs are turned off by CAF LEDs module.
         */
        int err = pm_device_action_run(leds[i].dev, PM_DEVICE_ACTION_RESUME);

        if (err) {
            LOG_ERR("Failed to set LED driver into active state (err: %d)", err);
        }
#endif
        led_update(&leds[i]);
    }
}

void leds_stop(void) {
    for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
        k_work_cancel_delayable(&leds[i].work);

        set_off(&leds[i]);

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_ZMK_LEDS_GPIO)
        /* Zephyr power management API is not implemented for GPIO LEDs.
         * LEDs are turned off by CAF LEDs module.
         */
        int err = pm_device_action_run(leds[i].dev, PM_DEVICE_ACTION_SUSPEND);

        if (err) {
            LOG_ERR("Failed to set LED driver into suspend state (err: %d)", err);
        }
#endif
    }
}

void led_set_state(uint8_t index, uint8_t led_state) {
    LOG_INF("led_set_state index:%d,state:%d", index, led_state);
    if (index < leds_num) {
        // why leds in dts is enable?
        for (int i = 0; i < leds_num; i++) {
            leds[i].effect = &led_peer_state_effect[LED_PEER_STATE_DISCONNECTED];
            led_update(&leds[i]);
        }
        leds[index].effect = &led_peer_state_effect[led_state];
        led_update(&leds[index]);
    }
}
#if CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI
#define LED_BLUE led_ble0_index // 0
#else
#define LED_BLUE led_ble_index // 0
#endif
#define LED_NUMLOCK led_num_index   // 1
#define LED_CAPSLOCK led_caps_index // 2
#define LED_24G led_ppt_index       // 3
#define LED_BAT led_bat_index       // 4

// static  struct gpio_dt_spec led_num=GPIO_DT_SPEC_GET(DT_NODELABEL(numlock_led),gpios);
// static  struct gpio_dt_spec led_caps=GPIO_DT_SPEC_GET(DT_NODELABEL(led_capslock),caps_led);
// static  struct gpio_dt_spec led_blue=GPIO_DT_SPEC_GET(DT_NODELABEL(led_blue),blue_led);
// static  struct gpio_dt_spec led_24g=GPIO_DT_SPEC_GET(DT_NODELABEL(led_24g),led_24g);

// void keyboard_led_init(void)
// {
//     if (gpio_is_ready_dt(&led_num)) {
//             LOG_DBG("led num pin:%d",led_num.spec.pin);
//     }
//     if (gpio_is_ready_dt(&led_caps)) {
//             LOG_DBG("led caps pin:%d",led_caps.spec.pin);
//     }
//     if (gpio_is_ready_dt(&led_blue)) {
//             LOG_DBG("led blue pin:%d",led_blue.spec.pin);
//     }
//     if (gpio_is_ready_dt(&led_24g)) {
//             LOG_DBG("led 24g pin:%d",led_24g.spec.pin);
//     }

// }

extern bool is_app_enabled_dlps;

static uint8_t charge_led_state;

void gpio_led_charge_set_state(uint8_t led_state) {
    if (LED_BAT == 0)
        return;
    LOG_DBG("led:%d charge state:%d", LED_BAT, led_state);
    // if(led_state == LED_BAT_CHARGING)
    // {
    //         leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_BAT_CHARGING];
    //         led_update(&leds[LED_BAT]);
    // }
    // else if(led_state == LED_BAT_CHARGE_DONE)
    // {
    //         leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_BAT_CHARGEDONE];
    //         led_update(&leds[LED_BAT]);
    // }
    // else if(led_state == LED_BAT_LOW)
    // {
    //         leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_BAT_LOW];
    //         led_update(&leds[LED_BAT]);
    // }
    // else if(led_state == LED_BAT_NONE)
    // {
    //         leds[LED_BAT].effect = &led_peer_state_effect[LED_PEER_STATE_DISCONNECTED];
    //         led_update(&leds[LED_BAT]);
    // }
    charge_led_state = led_state;
}

extern uint8_t get_battery_level(void);

void leds_pwroff(void) {
#if (DT_NODE_HAS_STATUS(DT_NODELABEL(pwr_ctrl), okay))
    gpio_pin_set_dt(&led_pwr_ctrl, 0);
    LOG_ERR("power off");
#endif
}

void gpio_led_blue_set_state(uint8_t led_state, uint8_t index) {
    if (LED_BLUE == 0)
        return;
    if (bat_is_shutdown())
        return;
    // is_app_enabled_dlps =false;
    LOG_WRN("gpio_led_blue_set_state:%d,index:%d", led_state, index);
    if (led_indicator_state.running && led_indicator_state.exclude) {
        led_indicator_state.led_state_pending = led_state;
        led_indicator_state.led_index = index;
        return;
    }
#if CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI
    for (uint8_t i = 0; i < 3; i++) {
        // if(i==index) continue;
        // leds[LED_BLUE+i].effect = &led_peer_state_effect[LED_PEER_STATE_DISCONNECTED];
        // led_update(&leds[LED_BLUE+i]);
        k_work_cancel_delayable(&leds[LED_BLUE + i].work);
        set_off(&leds[LED_BLUE + i]);
    }

    // if((leds[LED_BLUE+index].effect !=&led_peer_state_effect[led_state]) ||
    // k_work_delayable_remaining_get(&leds[LED_BLUE+index].work)==0)
    {
        leds[LED_BLUE + index].effect = &led_peer_state_effect[led_state];
        if (led_indicator_state.running) {
            leds[LED_BLUE + index].effect_step = 0;
            leds[LED_BLUE + index].effect_substep = 0;
            k_work_reschedule(&leds[LED_BLUE + index].work, K_MSEC(20));
            LOG_ERR("restart blue ind");
        } else
            led_update(&leds[LED_BLUE + index]);

        // if(led_ble_index){
        //     leds[led_ble_index].effect =&led_peer_state_effect[led_state];
        //     led_update(&leds[led_ble_index]);
        // }
        // leds_on_status |=4;
    }
#else
    // k_work_cancel_delayable(&leds[LED_BLUE].work);
    // set_off(&leds[LED_BLUE]);
    leds[LED_BLUE].effect = &led_peer_state_effect[led_state];
    if (led_indicator_state.running) {
        leds[LED_BLUE].effect_step = 0;
        leds[LED_BLUE].effect_substep = 0;
        k_work_reschedule(&leds[LED_BLUE].work, K_MSEC(20));
        LOG_ERR("restart blue ind");
    } else
        led_update(&leds[LED_BLUE]);
#endif
    led_indicator_state.running = 1;
    led_indicator_state.exclude = 0;
    led_indicator_state.ble_on = 1;
}
void gpio_led_24G_set_state(uint8_t led_state) {
    if (LED_24G == 0)
        return;
    if (bat_is_shutdown())
        return;
    LOG_DBG("24g set:%d", led_state);
    if (led_indicator_state.running && led_indicator_state.exclude) {
        led_indicator_state.led_state_pending = led_state;
        led_indicator_state.led_index = 3;
        return;
    }
    // if((leds[LED_24G].effect !=&led_peer_state_effect[led_state]) ||
    // k_work_delayable_remaining_get(&leds[LED_24G].work)==0)
    {
        leds[LED_24G].effect = &led_peer_state_effect[led_state];
        if (led_indicator_state.running) {
            leds[LED_24G].effect_step = 0;
            leds[LED_24G].effect_substep = 0;
            k_work_reschedule(&leds[LED_24G].work, K_NO_WAIT);
        } else
            led_update(&leds[LED_24G]);
        // if(led_ppt_index)
        // {
        //     leds[led_ppt_index].effect = &led_peer_state_effect[led_state];
        //     led_update(&leds[led_ppt_index]);

        // }
    }
    led_indicator_state.ppt_on = 1;
    led_indicator_state.running = 1;
    led_indicator_state.exclude = 0;
}
void gpio_led_bat_low(void) {
    if (bat_is_shutdown())
        return;
    leds[led_bat_index].effect = &led_peer_state_effect[LED_PEER_STATE_BAT_LOW];
    led_update(&leds[led_bat_index]);
    led_indicator_state.bat_on = 1;
}
void gpio_led_recover(void) {
    //  DBG_DIRECT("led recover");
    // is_app_enabled_dlps =false;
    led_indicator_state.running = 1;
    led_indicator_state.exclude = 1;
    for (size_t i = 1; i < leds_num; i++) {
        k_work_cancel_delayable(&leds[i].work);
        set_off(&leds[i]);
    }
    for (size_t i = 1; i < leds_num; i++) {
        if (i == led_ppt0_index) {
            leds[i].effect = &led_peer_state_effect[LED_PEER_STATE_RECOVER1];
            led_update(&leds[i]);
        } else {
            leds[i].effect = &led_peer_state_effect[LED_PEER_STATE_RECOVER];
            led_update(&leds[i]);
        }
    }
}
void gpio_led_power_on(void) {
    LOG_DBG("led power on");
    // uint32_t sleep =aon_read_reg() &0xff;
    // LOG_WRN("led ,sleep:%x",sleep);
    // if(sleep&0x02)
    if (aon_get_state(AON_STATE_SLEEP)) {
        LOG_WRN("led ,skip");
        return;
    }
    if (bat_is_shutdown())
        return;
    if (led_indicator_state.power_on)
        return;
    // is_app_enabled_dlps =false;
    led_indicator_state.running = 1;
    led_indicator_state.exclude = 1;
    led_indicator_state.on_status = 0x1c;
    led_indicator_state.power_on = 1;
    //  for (size_t i = 1; i < leds_num;i++)
    //  {
    //      k_work_cancel_delayable(&leds[i].work);
    //      set_off(&leds[i]);
    //  }
    //  k_msleep(100);
    for (size_t i = 1; i < leds_num; i++) {
#if (defined(CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI))
        if (i == led_caps_index || i == led_bat_index) {
            led_indicator_state.caps_on = 0;
            led_indicator_state.bat_on = 0;
            continue;
        }
#endif
        leds[i].effect = &led_peer_state_effect[LED_PEER_STATE_POWER_ON];
        led_update(&leds[i]);
    }
}
void restore_led_pair(void);
void bat_show_work_cb(struct k_work *work) {
    gpio_led_bat_display_off();
    restore_led_pair();
}
K_WORK_DELAYABLE_DEFINE(bat_show_work, bat_show_work_cb);

void gpio_led_bat_display(void) {
    led_indicator_state.running = 1;
    led_indicator_state.exclude = 0;
    uint8_t level = get_battery_level();
    struct led_color color = LED_COLOR(0xff, 0, 0);
    struct led_color nocolor = LED_COLOR(0, 0, 0);
#ifdef CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI
    if (level > 80) {
        set_color(&leds[led_ble0_index], &color);
        set_color(&leds[led_ble0_index + 1], &color);
        set_color(&leds[led_ble0_index + 2], &color);
        set_color(&leds[led_ppt_index], &color);
    } else if (level > 60) {
        set_color(&leds[led_ble0_index], &color);
        set_color(&leds[led_ble0_index + 1], &color);
        set_color(&leds[led_ble0_index + 2], &color);
        set_color(&leds[led_ppt_index], &nocolor);
    } else if (level > 30) {
        set_color(&leds[led_ble0_index], &color);
        set_color(&leds[led_ble0_index + 1], &color);
        set_color(&leds[led_ble0_index + 2], &nocolor);
        set_color(&leds[led_ppt_index], &nocolor);
    } else {
        set_color(&leds[led_ble0_index], &color);
        set_color(&leds[led_ble0_index + 1], &nocolor);
        set_color(&leds[led_ble0_index + 2], &nocolor);
        set_color(&leds[led_ppt_index], &nocolor);
    }
    k_work_reschedule(&bat_show_work, K_MSEC(3000));
#elif CONFIG_SHIELD_KEYCHRON_K5SE2_ANSI
    if (level > 50) {
        set_color(&leds[led_ppt_index], &color);
        set_color(&leds[led_ble_index], &nocolor);
    } else {
        set_color(&leds[led_ppt_index], &nocolor);
        set_color(&leds[led_ble_index], &color);
    }
#else
    if (level < 30) {
        set_color(&leds[led_bat_index], &color);
    } else {
        set_color(&leds[led_bat_index], &nocolor);
    }
    if (led_ble0_index) {
        uint8_t count = level / 33;
        if (level > 0 && count == 0)
            count = 1;
        for (uint8_t i = 0; i < count; i++) {
            set_color(&leds[led_ble0_index + i], &color);
        }
    }

#endif
}
void gpio_led_bat_display_off(void) {
    struct led_color nocolor = LED_COLOR(0, 0, 0);
#ifdef CONFIG_SHIELD_KEYCHRON_K3SE2_ANSI
    set_color(&leds[led_ppt_index], &nocolor);
#elif CONFIG_SHIELD_KEYCHRON_K5SE2_ANSI
    set_color(&leds[led_ppt_index], &nocolor);
    set_color(&leds[led_ble_index], &nocolor);
#else
    set_color(&leds[led_bat_index], &nocolor);
#endif
    if (led_ble0_index) {
        for (uint8_t i = 0; i < 3; i++) {
            set_color(&leds[led_ble0_index + i], &nocolor);
        }
    }
    led_indicator_state.running = 0;
    if (led_indicator_state.on_status == 0)
        leds_pwroff();
}
uint8_t keyboad_gpio_led_set_onoff(uint8_t led_state) {
    uint8_t handled = 0;
    keyboard_led_state = led_state;
    uint8_t num = led_state & 0x01;
    uint8_t caps = (led_state & 0x02);
    if (led_indicator_state.power_on)
        return 1;
    struct led_color color = LED_COLOR(0xff, 0xff, 0xff);
    LOG_DBG("led state:%d,num:%d,caps:%d", led_state, num, caps);
    uint8_t mask = 0;
    if (LED_NUMLOCK != 0) {
        num ? set_color(&leds[LED_NUMLOCK], &color) : set_off(&leds[LED_NUMLOCK]);
        handled = 1;
        mask |= 1 << 0;
    }

    if (LED_CAPSLOCK != 0) {
        caps ? set_color(&leds[LED_CAPSLOCK], &color) : set_off(&leds[LED_CAPSLOCK]);
        handled = 1;
        mask |= 1 << 1;
    }
    led_indicator_state.on_status &= 0x1c;
    led_indicator_state.on_status |= led_state & mask;
    LOG_ERR("led onstatus:%x", led_indicator_state.on_status);
    if (led_indicator_state.on_status == 0)
        leds_pwroff();
    return handled;
}

uint8_t gpio_led_is_power_on(void) { return led_indicator_state.power_on; }

SYS_INIT(leds_init, APPLICATION, CONFIG_ZMK_LED_INIT_PRIORITY);

#ifndef CONFIG_LED_STRIP
void led_bat_low(void) { gpio_led_bat_low(); }
void led_24G_set_state(uint8_t led_state) { gpio_led_24G_set_state(led_state); }
void blue_led_set_state(uint8_t led_state, uint8_t index) {
    gpio_led_blue_set_state(led_state, index);
}
void led_recover(uint8_t stop_rgb) { gpio_led_recover(); }
void led_charge_set_state(uint8_t state) {}
void led_bat_display_off(void) { gpio_led_bat_display_off(); }
void led_bat_display(void) { gpio_led_bat_display(); }
#endif