#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/led_strip.h>
#include "rgb_matrix_conf.h"
#include "rgb_matrix.h"

#include <zmk/activity.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/workqueue.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <stdlib.h>
#include <zmk/rgb_underglow.h>
#include <zmk/endpoints.h>
#include <zmk/leds.h>
#include <zmk/battery.h>
#include "aon_reg.h"
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
int load_immediate_value(const char *name, void *dest, size_t len);

bool bat_is_shutdown(void);

#if !DT_HAS_CHOSEN(zmk_underglow)

#error "A zmk,underglow chosen node must be declared"

#endif

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)
#define TIME_INTERVAL ((RGB_MATRIX_LED_FLUSH_LIMIT + 2) / 5)

// uint32_t aon_read_reg(void)
// {
//     return AON_REG_READ(AON_NS_REG15X_APP);
// }
// void aon_write_reg(uint8_t value)
// {
//     AON_REG_WRITE(AON_NS_REG15X_APP,value);
// }
void zmk_rgb_test_start(void);

static const struct device *rgb_matrix_device;
// static struct k_work_delayable save_work;
static void save_state_worker(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(save_work, save_state_worker);
static K_SEM_DEFINE(thread_wait_sem, 0, 1);

extern uint8_t rgb_onoff_status;
static uint8_t rgb_test_start;
static uint8_t rgb_test_index;
static uint8_t key3_press = 0;
static bool rgb_enable_dlps;
bool rgb_control_enable = true;

void print_rgb_matrix_config(void) {
    LOG_DBG("rgb_matrix_config");
    LOG_DBG("rgb_matrix_config.enable = %d", rgb_matrix_config.enable);
    LOG_DBG("rgb_matrix_config.mode = %d", rgb_matrix_config.mode);
    LOG_DBG("rgb_matrix_config.back_mode = %d", rgb_matrix_config.back_mode);
    LOG_DBG("rgb_matrix_config.hsv.h = %d", rgb_matrix_config.hsv.h);
    LOG_DBG("rgb_matrix_config.hsv.s = %d", rgb_matrix_config.hsv.s);
    LOG_DBG("rgb_matrix_config.hsv.v = %d", rgb_matrix_config.hsv.v);
    LOG_DBG("rgb_matrix_config.speed = %d", rgb_matrix_config.speed);
    LOG_DBG("rgb_matrix_config.flags = %d", rgb_matrix_config.flags);
}

void save_rgb_matrix_config_rightnow(void) {
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    k_work_reschedule(&save_work, K_MSEC(30));
#else
    k_work_reschedule(&save_work, K_NO_WAIT);
#endif
}
void save_rgb_matrix_config(void) { k_work_reschedule(&save_work, K_MSEC(1000)); }
void cancel_save_rgb_work(void) { k_work_cancel_delayable(&save_work); }
void update_rgb_matrix(void) { save_rgb_matrix_config(); }

void update_rgb_matrix_default(void) {
    LOG_DBG("update_rgb_matrix_default\n");
    rgb_matrix_config.enable = RGB_MATRIX_DEFAULT_ON;
    rgb_matrix_config.mode = RGB_MATRIX_DEFAULT_MODE;
    rgb_matrix_config.hsv =
        (HSV){RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, RGB_MATRIX_DEFAULT_VAL};
    rgb_matrix_config.speed = RGB_MATRIX_DEFAULT_SPD;
    rgb_matrix_config.flags = LED_FLAG_ALL;
    rgb_matrix_config.back_mode = RGB_MATRIX_DEFAULT_MODE;
    save_rgb_matrix_config();
}
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
extern rgb_config_t back_rgb_config;
#endif
static void save_state_worker(struct k_work *work) {
    if (rgb_matrix_config.mode == 0x3f) {
        LOG_ERR("mode not ok,use default");
        k_work_reschedule(&save_work, K_MSEC(1000));
        // rgb_matrix_config.mode =rgb_matrix_config.back_mode;//RGB_MATRIX_DEFAULT_MODE;
        return;
    }
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    if (rgb_matrix_config.mode == RGB_EFFECT_MIXED_RGB) {
        back_rgb_config.mode = RGB_EFFECT_MIXED_RGB;
        back_rgb_config.enable = rgb_matrix_config.enable;
        if (rgb_matrix_config.hsv.v != back_rgb_config.hsv.v) {
            back_rgb_config.hsv.v = rgb_matrix_config.hsv.v;
        }
        LOG_ERR("cur hsv:%x,%x,%x,spd:%d", back_rgb_config.hsv.h, back_rgb_config.hsv.s,
                back_rgb_config.hsv.v, back_rgb_config.speed);
        settings_save_one("rgb/underglow/state", &back_rgb_config, sizeof(back_rgb_config));
        return;
    }
#endif
    LOG_ERR("save rgb:%x,%x,%x,mode:%d", rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s,
            rgb_matrix_config.hsv.v, rgb_matrix_config.mode);

    settings_save_one("rgb/underglow/state", &rgb_matrix_config, sizeof(rgb_matrix_config));
}

void zmk_rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (rgb_matrix_device == NULL)
        return;
    // LOG_DBG("set color index:%d,rgb:%02x,%02x,%02x",index,red,green,blue);
    struct led_rgb pixels = {red, green, blue};

    int err = led_strip_update_rgb(rgb_matrix_device, &pixels, index + 1);
    if (err < 0) {
        LOG_ERR("Failed to update the RGB strip (%d)", err);
    }
}
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
extern uint8_t rgb_regions[];

void zmk_rgb_matrix_region_set_color(uint8_t region, int index, uint8_t red, uint8_t green,
                                     uint8_t blue) {
    if (rgb_matrix_device == NULL)
        return;

    if (rgb_regions[index] == region) {

        struct led_rgb pixels = {red, green, blue};
        int err = led_strip_update_rgb(rgb_matrix_device, &pixels, index + 1);
        if (err < 0) {
            LOG_ERR("Failed to update the RGB strip (%d)", err);
        }
    }
}

void zmk_rgb_matrix_region_set_color_all(uint8_t region, uint8_t red, uint8_t green, uint8_t blue) {
    if (rgb_matrix_device == NULL)
        return;
    for (int i = 0; i < STRIP_NUM_PIXELS; i++)
        if (((g_led_config.flags[i] & 0xF0) >> 4) == region)
            zmk_rgb_matrix_set_color(i, red, green, blue);
}
#endif

void zmk_rgb_matrix_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    if (rgb_matrix_device == NULL)
        return;
    LOG_DBG("set color all");
    for (int i = 0; i < STRIP_NUM_PIXELS; i++)
        zmk_rgb_matrix_set_color(i, red, green, blue);
}

void zmk_rgb_matrix_update_pwm_buffers(void) {
    if (rgb_matrix_device == NULL)
        return;
    rgb_enable_dlps = false;
    led_strip_update_channels(rgb_matrix_device, NULL, 1);
}
static int rgb_matrix_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg) {
    LOG_DBG("Setting launcher value %s", name);

    if (settings_name_steq(name, "underglow/state", NULL)) {
        if (len != sizeof(rgb_matrix_config)) {
            LOG_ERR("Invalid rgb size (got %d expected %d)", len, sizeof(rgb_matrix_config));
            update_rgb_matrix_default();
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &rgb_matrix_config, sizeof(rgb_matrix_config));
        if (err <= 0) {
            LOG_ERR("Failed to read rgb  from settings (err %d)", err);
            update_rgb_matrix_default();
            return err;
        }
        rgb_matrix_config.back_mode = rgb_matrix_config.mode;
        if (rgb_matrix_config.flags == 0) {
            rgb_matrix_config.flags = LED_FLAG_ALL;
            LOG_ERR("reset rgb flags!!!!");
        }
        print_rgb_matrix_config(); // display current eeprom values
    }
    return 0;
}
struct settings_handler rgb_matrix_handler = {.name = "rgb", .h_set = rgb_matrix_handle_set};
void zmk_rgb_settings_init(void) {
    int rc = settings_subsys_init();
    if (rc) {
        LOG_ERR("settings subsys initialization: fail (err %d)\n", rc);
        return;
    }
    rc = settings_register(&rgb_matrix_handler);
    if (rc) {
        LOG_ERR("Failed to setup the profile settings handler (err %d)", rc);
        return;
    }
    settings_load_subtree("rgb");
}
void zmk_rgb_matrix_driver_init(void) {
    rgb_matrix_device = DEVICE_DT_GET(DT_CHOSEN(zmk_underglow));
    // k_work_init_delayable(&save_work, save_state_worker);
    // int
    // rc=load_immediate_value("rgb/underglow/state",&rgb_matrix_config,sizeof(rgb_matrix_config));
    // // print_rgb_matrix_config();
    // if(rc == -ENOENT)
    // {
    //     LOG_ERR("update_rgb_matrix_default");
    //     update_rgb_matrix_default();
    // }
    // print_rgb_matrix_config(); // display current eeprom values
    rgb_matrix_config.enable = RGB_MATRIX_DEFAULT_ON;
    rgb_matrix_config.mode = RGB_MATRIX_DEFAULT_MODE;
    rgb_matrix_config.back_mode = RGB_MATRIX_DEFAULT_MODE;
    rgb_matrix_config.hsv =
        (HSV){RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, RGB_MATRIX_DEFAULT_VAL};
    rgb_matrix_config.speed = RGB_MATRIX_DEFAULT_SPD;
    rgb_matrix_config.flags = LED_FLAG_ALL;

    zmk_rgb_settings_init();

    srand(k_cycle_get_32());
    k_sem_reset(&thread_wait_sem);

    rgb_onoff_status = rgb_matrix_config.enable;
}

int zmk_rgb_underglow_on() {
    k_sem_give(&thread_wait_sem);
    zmk_rgb_matrix_enable();
    return 0;
}
int zmk_rgb_underglow_off() {

    k_sem_reset(&thread_wait_sem);
    zmk_rgb_matrix_disable();
    return 0;
}
int zmk_rgb_underglow_toggle() {
    if (!rgb_control_enable)
        return -1;
    zmk_rgb_matrix_toggle();
    if (rgb_matrix_config.enable) {
        k_sem_give(&thread_wait_sem);
    } else if (!rgb_led_indicators.rgb_enable) {
        LOG_WRN("reset rgb thread");
        k_sem_reset(&thread_wait_sem);
    }
    // rgb_matrix_config.enable?k_sem_give(&thread_wait_sem): k_sem_reset(&thread_wait_sem);
    return 0;
}
int zmk_rgb_underglow_change_hue(int direction) {
    if (!rgb_control_enable)
        return -1;
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    if (rgb_matrix_config.mode == RGB_EFFECT_PER_KEY_RGB) {
        LOG_ERR("skip hue perkey");
        return -1;
    }
#endif
    if (direction > 0)
        zmk_rgb_matrix_increase_hue();
    else
        zmk_rgb_matrix_decrease_hue();
    return 0;
}

int zmk_rgb_underglow_change_sat(int direction) {
    if (!rgb_control_enable)
        return -1;
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    if (rgb_matrix_config.mode == RGB_EFFECT_PER_KEY_RGB) {
        LOG_ERR("skip sat perkey");
        return -1;
    }
#endif
    if (direction > 0)
        zmk_rgb_matrix_increase_sat();
    else
        zmk_rgb_matrix_decrease_sat();
    return 0;
}
int zmk_rgb_underglow_change_brt(int direction) {
    if (!rgb_control_enable)
        return -1;
    if (direction > 0) {
        if (!rgb_matrix_config.enable) {
            zmk_rgb_underglow_toggle();
#ifdef CONFIG_ZMK_SSD1306
            uint8_t index =
                (rgb_matrix_config.hsv.v + RGB_MATRIX_VAL_STEP / 2) / RGB_MATRIX_VAL_STEP;
            disp_change_setting(TYPE_LIGHTNESS, index);
#endif
            return 0;
        }
        zmk_rgb_matrix_increase_val();
    } else {
        zmk_rgb_matrix_decrease_val();
        if (rgb_matrix_config.enable && rgb_matrix_config.hsv.v == 0) {
            zmk_rgb_underglow_toggle();
        }
    }
    return 0;
}
int zmk_rgb_underglow_change_spd(int direction) {
    if (!rgb_control_enable)
        return -1;
    if (direction > 0)
        zmk_rgb_matrix_increase_speed();
    else
        zmk_rgb_matrix_decrease_speed();
    return 0;
}
int zmk_rgb_underglow_cycle_effect(int direction) {
    if (!rgb_control_enable)
        return -1;
    if (direction > 0) {
        zmk_rgb_matrix_step();
    } else {
        zmk_rgb_matrix_step_reverse();
    }
    return 0;
}
int zmk_rgb_underglow_select_effect(int effect) {
    if (!rgb_control_enable)
        return -1;
    zmk_rgb_matrix_mode(effect);
    return 0;
}
int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    zmk_rgb_matrix_sethsv(color.h, color.s, color.b);
    return 0;
}
int zmk_rgb_underglow_get_state(bool *state) {
    *state = rgb_matrix_config.enable;
    return 0;
}

int rgb_underglow_auto_state(bool new_state) {
    if (rgb_matrix_config.enable == new_state) {
        return 0;
    }
    if (new_state) {

        return zmk_rgb_matrix_on();
    } else {

        return zmk_rgb_matrix_off();
    }
}

static int rgb_underglow_event_listener(const zmk_event_t *eh) {

    if (zmk_endpoints_selected().transport != ZMK_TRANSPORT_USB) {
        if (as_zmk_activity_state_changed(eh)) {
            if (rgb_onoff_status)
                return rgb_underglow_auto_state(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
            else
                return 0;
        }
    } else {
        if (as_zmk_usb_conn_state_changed(eh)) {
            extern uint8_t usb_configured;
            if (!rgb_control_enable && !rgb_test_start)
                rgb_control_enable = true;
            if (rgb_onoff_status && usb_configured &&
                zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB) {
                return rgb_underglow_auto_state(zmk_usb_get_status() != USB_DC_SUSPEND);
            } else {
                if ((zmk_usb_get_status() == USB_DC_UNKNOWN) ||
                    (zmk_usb_get_status() == USB_DC_SUSPEND)) {
                    zmk_rgb_matrix_off();
                    // rgb_onoff_status=0;
                    // rgb_control_enable =false;
                    LOG_ERR("set rgb onff=0,status:%x", zmk_usb_get_status());
                }
                return 0;
            }
        }
    }

    return 0;
}

ZMK_LISTENER(rgb_matrix, rgb_underglow_event_listener);
ZMK_SUBSCRIPTION(rgb_matrix, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(rgb_matrix, zmk_usb_conn_state_changed);
// ZMK_SUBSCRIPTION(rgb_matrix, zmk_position_state_changed);

extern bool is_app_enabled_dlps;

#define RGB_THREAD_STACK_SIZE 1024
#define RGB_THREAD_PRIORITY 8
void zmk_rgb_thread(void);
K_THREAD_DEFINE(zmk_rgb_thread_tid, RGB_THREAD_STACK_SIZE, zmk_rgb_thread, NULL, NULL, NULL,
                RGB_THREAD_PRIORITY, 0, 0);
uint8_t zmk_usb_power_on(void);
void zmk_rgb_thread(void) {
    k_sem_take(&thread_wait_sem, K_FOREVER);
    LOG_ERR("zmk_rgb_thread start!");
    // if(aon_read_reg()==0xf1)
    if (aon_get_state(AON_STAET_RGB_TEST)) {
        zmk_rgb_test_start();
    }
    while (1) {
        if (rgb_matrix_config.enable == 0 && rgb_led_indicators.rgb_enable == 0)
            LOG_ERR("rgb not enable");
        if (bat_is_shutdown() && !zmk_usb_power_on())
            LOG_ERR("bat shutdown");
        if ((!zmk_usb_power_on() &&
             (bat_is_low() && !rgb_led_indicators.rgb_enable && k_uptime_get_32() > 8000))) {
            LOG_ERR("bat low,no rgb inicator");
        }
        if ((!rgb_matrix_config.enable && !rgb_led_indicators.rgb_enable) ||
            (bat_is_shutdown() && !zmk_usb_power_on()) ||
            (!zmk_usb_power_on() &&
             (bat_is_low() && !rgb_led_indicators.rgb_enable && k_uptime_get_32() > 3000))) {
            zmk_rgb_matrix_set_color_all(0, 0, 0);
            // if(CAPS_LOCK_INDEX!=0xff)
            // {
            //     if(keyboard_get_led_state()&0x02)
            //     {
            //         zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX,0xff,0xff,0xff);
            //     }
            // }
            // if(NUM_LOCK_INDEX!=0xff)
            // {
            //     if(keyboard_get_led_state()&0x01)
            //     {
            //         zmk_rgb_matrix_set_color(NUM_LOCK_INDEX,0xff,0xff,0xff);
            //     }
            // }
            void os_state_indicate(void);
            os_state_indicate();
            zmk_rgb_matrix_update_pwm_buffers();
            zmk_rgb_sleep();
            LOG_ERR("rgb clear,wait to start,dlps:%d,%d", is_app_enabled_dlps, rgb_enable_dlps);
            // rgb_enable_dlps=true;
            k_sem_take(&thread_wait_sem, K_FOREVER);
            LOG_ERR("rgb thread wakeup");
        }
        k_msleep(2);
        zmk_rgb_matrix_task();
        rgb_enable_dlps = false;
    }
}
extern rgb_task_states rgb_task_state;
void zmk_rgb_task_sync(void) {
    uint32_t deltaTime = k_uptime_delta_32(g_rgb_timer);
    if (deltaTime >= RGB_MATRIX_LED_FLUSH_LIMIT)
        rgb_task_state = RGB_STATE_START;
    else {
        uint32_t interval = RGB_MATRIX_LED_FLUSH_LIMIT - deltaTime;
        k_msleep(interval);
    }
}
void enable_rgb_thread(void) { k_sem_give(&thread_wait_sem); }
int zmk_rgb_matrix_on() {
    k_sem_give(&thread_wait_sem);
    rgb_task_state = RGB_STATE_START;
    rgb_matrix_config.enable = 1;
    LOG_ERR("rgb on");
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    if (back_rgb_config.raw && (rgb_matrix_config.mode != RGB_EFFECT_MIXED_RGB)) {
        rgb_matrix_config.hsv.h = back_rgb_config.hsv.h;
        rgb_matrix_config.hsv.s = back_rgb_config.hsv.s;
        rgb_matrix_config.speed = back_rgb_config.speed;
        back_rgb_config.raw = 0;
        LOG_ERR("reset rgb config");
    }
#endif
    return 0;
}
int zmk_rgb_matrix_off() {
    k_sem_reset(&thread_wait_sem);
    rgb_task_state = RGB_STATE_START;
    rgb_matrix_config.enable = 0;
    LOG_ERR("rgb off");
    return 0;
}
int zmk_rgb_led_indicatots_on() {
    k_sem_give(&thread_wait_sem);
    rgb_task_state = RGB_STATE_START;
    rgb_led_indicators.rgb_enable = 1;
    if (rgb_onoff_status)
        rgb_matrix_config.enable = 1;
    return 0;
}
int zmk_rgb_led_indicatots_off() {
    k_sem_reset(&thread_wait_sem);
    rgb_task_state = RGB_STATE_START;
    rgb_led_indicators.rgb_enable = 0;
    return 0;
}
void zmk_rgb_test_start(void) {
    rgb_test_start = 1;
    rgb_control_enable = false;
    rgb_test_index = 0;
    led_rgb_set_color(0xff, 0xff, 0xff);

    if (zmk_endpoints_selected().transport != ZMK_TRANSPORT_USB) {
        // aon_write_reg(0xf1);
        aon_write_state(AON_STAET_RGB_TEST);
    }
}
void rgb_test_worker(struct k_work *work) {
    if (key3_press) {
        key3_press = 0;
        LOG_DBG("rgb_test_start");
        zmk_rgb_test_start();
    }
}
K_WORK_DELAYABLE_DEFINE(rgb_test_work, rgb_test_worker);

bool zmk_rgb_test(struct zmk_position_state_changed *pos_state) {
#if 0
        if(pos_state !=NULL)
        {
            if(pos_state->state)
            {
                if(!rgb_matrix_config.enable && !rgb_led_indicators.rgb_enable)
                {
                    rgb_matrix_config.mode =0x31;
                }
                if(rgb_matrix_config.mode ==0x31)
                {
                    LOG_ERR("pos :%d,red",pos_state->position);
                    zmk_rgb_matrix_set_color(pos_state->position,0xff,0,0);
                    zmk_rgb_matrix_update_pwm_buffers();
                }
            }
            else
            {
                if(rgb_matrix_config.mode ==0x31)
                {
                    LOG_ERR("pos :%d,clear",pos_state->position);
                    zmk_rgb_matrix_set_color(pos_state->position,0,0,0);
                    zmk_rgb_matrix_update_pwm_buffers();
                    rgb_enable_dlps =true;
                }
            }
        }
#endif
    if (rgb_test_start) {

        if (pos_state != NULL && pos_state->state) {
            if (pos_state->position == RGB_TEST_RIGHT_KEY) // right key
            {
                rgb_test_index++;
                rgb_test_index = rgb_test_index % 4;
                switch (rgb_test_index) {
                case 0:
                    led_rgb_set_color(0xff, 0xff, 0xff);
                    break;
                case 1:
                    led_rgb_set_color(0xff, 0, 0);
                    break;
                case 2:
                    led_rgb_set_color(0, 0xff, 0);
                    break;
                case 3:
                    led_rgb_set_color(0, 0, 0xff);
                    break;
                }
            } else if (pos_state->position == RGB_TEST_HOME_KEY) // home key
            {
                led_bat_display_off();
                // aon_write_reg(0xf0);
                aon_clear_state(AON_STAET_RGB_TEST);
                rgb_test_start = 0;
                rgb_control_enable = true;
            }
        }

        return 1;
    } else {
        static uint8_t key_state = 0;

        if (pos_state != NULL) {
            if (pos_state->state) {
                if (pos_state->position == RGB_TEST_HOME_KEY)
                    key_state |= 1;
                else if (pos_state->position == RGB_TEST_RIGHT_KEY)
                    key_state |= 2;
                else if (pos_state->position == RGB_TEST_FN_KEY)
                    key_state |= 4;
                LOG_DBG("rgb test state:%x,key:%d", key_state, pos_state->position);
                if (!key3_press && key_state == 7) {
                    LOG_DBG("rgb test 3key press");
                    key3_press = 1;
                    k_work_reschedule(&rgb_test_work, K_MSEC(3000));
                }
            } else {
                if (pos_state->position == RGB_TEST_HOME_KEY)
                    key_state &= 0xfe;
                else if (pos_state->position == RGB_TEST_RIGHT_KEY)
                    key_state &= 0xfd;
                else if (pos_state->position == RGB_TEST_FN_KEY)
                    key_state &= 0xfb;
                key3_press = 0;
                k_work_cancel_delayable(&rgb_test_work);
            }
        }
    }
    return 0;
}
enum {
    BACKLIGHT_TEST_OFF = 0,
    BACKLIGHT_TEST_WHITE,
    BACKLIGHT_TEST_RED,
    BACKLIGHT_TEST_GREEN,
    BACKLIGHT_TEST_BLUE,
    BACKLIGHT_TEST_MAX,
};
void zmk_rgb_test_handle_cmd(uint8_t cmd) {
    LOG_ERR("rgb cmd:%x", cmd);
    if (cmd == BACKLIGHT_TEST_OFF) {
        led_bat_display_off();
        rgb_test_start = 0;
        rgb_control_enable = true;
        rgb_test_index = 0;
    } else {
        if (!rgb_test_start) {
            rgb_test_start = 1;
            rgb_control_enable = false;
        }
        switch (cmd) {
        case BACKLIGHT_TEST_WHITE:
            led_rgb_set_color(0xff, 0xff, 0xff);
            break;
        case BACKLIGHT_TEST_RED:
            led_rgb_set_color(0xff, 0, 0);
            break;
        case BACKLIGHT_TEST_GREEN:
            led_rgb_set_color(0, 0xff, 0);
            break;
        case BACKLIGHT_TEST_BLUE:
            led_rgb_set_color(0, 0, 0xff);
            break;
        }
    }
}
void zmk_rgb_sleep(void) {
    if (rgb_matrix_device == NULL)
        return;

    hid_led_t check_bit = {0};
#ifdef NUM_LOCK_INDEX
    check_bit.num_lock = 1;
#endif
#ifdef CAPS_LOCK_INDEX
    check_bit.caps_lock = 1;
#endif
    if ((keyboard_get_led_state().raw & check_bit.raw) == 0) {
        led_strip_update_channels(rgb_matrix_device, NULL, 0xff); // sleep all;
        rgb_enable_dlps = true;
    } else {
        if ((keyboard_get_led_state().num_lock == 0) || (check_bit.num_lock == 0)) {
            led_strip_update_channels(rgb_matrix_device, NULL, 0xf0); // sleep id=0;
        }

        if ((keyboard_get_led_state().caps_lock == 0) || (check_bit.caps_lock == 0)) {
            led_strip_update_channels(rgb_matrix_device, NULL, 0xf1); // sleep id=1;
        }

        rgb_enable_dlps = false;
    }
}

bool rgb_is_allow_sleep(void) { return rgb_enable_dlps; }

uint8_t get_rgb_test_start(void) { return rgb_test_start; }