/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include "trace.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zephyr/drivers/gpio.h>

#include <zmk/usb_hid.h>
#include <zmk/mode_monitor.h>
#include <zmk/endpoints.h>
#include <app_version.h>
#include <zephyr/sys/byteorder.h>
#include <zmk/leds.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void winlock_led_onoff(uint8_t onoff);
void winlock_led_set(void);

// #define USB_DEC_TO_BCD(dec)  ((((dec) / 10) << 4) | ((dec) % 10))

/** USB Device release number (bcdDevice Descriptor field) */
#define USB_BCD_VER                                                                                \
    (USB_DEC_TO_BCD(APP_VERSION_MAJOR) << 8 |                                                      \
     USB_DEC_TO_BCD((APP_VERSION_MINOR * 10) + APP_PATCHLEVEL))

static struct gpio_dt_spec detect_usb = GPIO_DT_SPEC_GET(DT_NODELABEL(usb_det), gpios);
// {
//     .port = DEVICE_DT_GET(DT_NODELABEL(gpioa)),
//     .pin = 24,
//     .dt_flags = GPIO_ACTIVE_LOW ,
// } ;//GPIO_DT_SPEC_GET(DT_NODELABEL(mode_monitor), detect_usb_gpios);
static enum usb_dc_status_code usb_status = USB_DC_UNKNOWN;

static void raise_usb_status_changed_event(struct k_work *_work) {
    raise_zmk_usb_conn_state_changed(
        (struct zmk_usb_conn_state_changed){.conn_state = zmk_usb_get_conn_state()});
}
// static void usb_disable_worker(struct k_work * work)
// {
//     LOG_ERR("usb disabled!!!");
//     int rc = usb_disable();
//     if(rc != 0)
//     {
//         LOG_ERR("Unable to disable usb ,err = %d",rc);
//     }
// }
K_WORK_DELAYABLE_DEFINE(usb_status_check_work, raise_usb_status_changed_event);
K_WORK_DEFINE(usb_status_notifier_work, raise_usb_status_changed_event);
// K_WORK_DELAYABLE_DEFINE(usb_disable_check,usb_disable_worker);

// static uint8_t usb_check;
uint8_t usb_configured = 0;

enum usb_dc_status_code zmk_usb_get_status(void) { return usb_status; }

enum zmk_usb_conn_state zmk_usb_get_conn_state(void) {
    LOG_DBG("state: %d", usb_status);
    switch (usb_status) {
    case USB_DC_SUSPEND:
    case USB_DC_CONFIGURED:
    case USB_DC_RESUME:
    case USB_DC_CLEAR_HALT:
    case USB_DC_SOF:
        return ZMK_USB_CONN_HID;

    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return ZMK_USB_CONN_NONE;

    default:
        return ZMK_USB_CONN_POWERED;
    }
}

void usb_status_cb(enum usb_dc_status_code status, const uint8_t *params) {
    // Start-of-frame events are too frequent and noisy to notify, and they're
    // not used within ZMK
    // LOG_DBG("usb status cb: usb status is %d",status);
    // DBG_DIRECT("usb status cb: usb status is %d",status);
    static uint8_t led_backup_state = 0;

    // if(usb_check)
    // {
    //     if(status != USB_DC_SUSPEND)
    //     {
    //         usb_check =0;
    //         LOG_ERR("stop usb disable check,status:%d",status);
    //         k_work_cancel_delayable(&usb_disable_check);
    //     }
    // }
    if (status == USB_DC_SOF) {
        return;
    }
    LOG_ERR("usb status:%d", status);
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (status == USB_DC_RESET) {
        zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
    }
#endif
    usb_status = status;

    switch (status) {
    case USB_DC_SUSPEND: {
        if (usb_configured && zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB) {
            led_backup_state = keyboard_get_led_state().raw;
            keyboad_led_set_onoff(0);
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
            winlock_led_onoff(0);
#endif
        }
    } break;

    case USB_DC_RESUME: {
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR
        winlock_led_set();
#endif
    } break;

    case USB_DC_CONFIGURED: {
        usb_configured = 1;
    } break;
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
        usb_configured = 0;
    default:
        break;
    }
    // if(status == USB_DC_CONFIGURED)
    // {
    //     app_global_data.is_usb_enumeration_success = true;
    // }
    k_work_submit(&usb_status_notifier_work);
};
extern struct usb_desc_header __usb_descriptor_start[];

int zmk_usb_init(void) {
    int usb_enable_ret;
    int usb_disable_ret;
    struct usb_device_descriptor *device_descriptor =
        (struct usb_device_descriptor *)__usb_descriptor_start;
    if (device_descriptor) {
        device_descriptor->bcdDevice = sys_cpu_to_le16(USB_BCD_VER);
        LOG_ERR("new usb bcd ver:%x,ver:%s", device_descriptor->bcdDevice, APP_VERSION_STRING);
    }
    usb_enable_ret = usb_enable(usb_status_cb);
    gpio_pin_configure_dt(&detect_usb, GPIO_INPUT);
    if (!gpio_pin_get(detect_usb.port, detect_usb.pin)) {
        LOG_ERR("usb is not insert");
        usb_disable_ret = usb_disable();
        if (usb_disable_ret != 0) {
            LOG_ERR("Unable to disable usb ,err = %d", usb_disable_ret);
            return -EINVAL;
        }
    } else {
        // usb_check =1;
        // k_work_reschedule(&usb_disable_check,K_MSEC(1000));
        k_work_reschedule(&usb_status_check_work, K_MSEC(3000));
    }

    if (usb_enable_ret != 0) {
        LOG_ERR("Unable to enable USB ,err = %d", usb_enable_ret);
        // app_mode.is_in_usb_mode = false;
        return -EINVAL;
    }

    return 0;
}
int zmk_usb_deinit(void) {
    int usb_enable_ret;

    usb_enable_ret = usb_disable();

    if (usb_enable_ret != 0) {
        LOG_ERR("Unable to disable USB");
        return -EINVAL;
    }

    return 0;
}

SYS_INIT(zmk_usb_init, APPLICATION, CONFIG_ZMK_USB_INIT_PRIORITY);
