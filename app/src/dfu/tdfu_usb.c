/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/usb.h>

#include "tdfu.h"


LOG_MODULE_DECLARE(zmk,CONFIG_ZMK_LOG_LEVEL);

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_GET_REPORT_ID_MASK 0x00ff

static const struct device *dfu_hid_dev;

static K_SEM_DEFINE(hid_sem, 1, 1);

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_sem); }

#define HID_REPORT_TYPE_INPUT 0x100
#define HID_REPORT_TYPE_OUTPUT 0x200
#define HID_REPORT_TYPE_FEATURE 0x300

static const uint8_t report_descs[] =
{				
 	0x05,0x8c,			//USAGE_PAGE 
 	0x09,0x01,			//usage(RESERVED)
	0xA1,0x01,			//Collection(Application)
 	0x85,MY_DFU_RPT_INPUT_ID,			//REPORT_ID (0xb1)
 	0x09,0x02,
 	0x15,0x00,			//Logical Minimum (0)
 	0x26,0xFF, 0x00,	//Logical Maximum (255)0
	0x75,0x08,			//REPORT SIZE (8)
 	0x95,32,			//REPORT COUNT(32)
 	0x81,0x02,			//INPUT (Data,Ary,Abs)
 	0x85,MY_DFU_RPT_OUTPUT_ID,			//REPORT_ID (0x08)
 	0x09,0x03,
 	0x95,32,			//REPORT_COUNT (32)	 //--output 1byte
 	0x75,0x08,			//REPORT_SIZE (8)
 	0x15,0x00,			//Logical Minimum (0)
 	0x26,0xFF, 0x00,	//Logical Maximum (255)
 	0x91,0x02,			//OUTPUT (Data,Var,Abs)
	0xC0, 
};

K_MSGQ_DEFINE(dfu_usb_msgq, 64,8, 4);
void dfu_usb_worker(struct k_work *work);
K_WORK_DEFINE(dfu_usb_work, dfu_usb_worker);

void dfu_usb_worker(struct k_work *work)
{
    uint8_t usb_data[64];
    while (k_msgq_get(&dfu_usb_msgq, usb_data, K_NO_WAIT) == 0) {
        // LOG_HEXDUMP_DBG(usb_data,sizeof(usb_data),"rx");
        my_scdfu_data_handle(usb_data+1,sizeof(usb_data)-1);
    }
}
static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {

    /*
     * 7.2.1 of the HID v1.11 spec is unclear about handling requests for reports that do not exist
     * For requested reports that aren't input reports, return -ENOTSUP like the Zephyr subsys does
     */
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_INPUT) {
        LOG_ERR("Unsupported report type %d requested", (setup->wValue & HID_GET_REPORT_TYPE_MASK)
                                                            << 8);
        return -ENOTSUP;
    }
    // uint8_t report[32]={MY_DFU_RPT_INPUT_ID};
    switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
    case MY_DFU_RPT_INPUT_ID:
    //     *data = (uint8_t *)report;
    //     *len = sizeof(report);
    // break;
    default:
        LOG_ERR("Invalid report ID %d requested", setup->wValue & HID_GET_REPORT_ID_MASK);
        return -EINVAL;
    }

    return 0;
}
static void out_ready_cb(const struct device *dev)
{
    uint8_t rev_buf[64]={0};
	uint32_t rev_bytes =0;
	hid_int_ep_read(dev,rev_buf,sizeof(rev_buf),&rev_bytes);
    int err = k_msgq_put(&dfu_usb_msgq, rev_buf, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("queue full");

        }
        default:
            LOG_WRN("Failed to queue launcher data (%d)", err);
        }
    }
    err = k_work_submit(&dfu_usb_work);
}
#if 0
static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_OUTPUT) {
        LOG_ERR("Unsupported report type %d requested",
                (setup->wValue & HID_GET_REPORT_TYPE_MASK) >> 8);
        return -ENOTSUP;
    }

    switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
        case MY_DFU_RPT_OUTPUT_ID:
            uint8_t rev_buf[64]={0};
            memcpy(rev_buf,*data,sizeof(rev_buf));
            int err = k_msgq_put(&dfu_usb_msgq, rev_buf, K_MSEC(100));
            if (err) {
                switch (err) {
                case -EAGAIN: {
                    LOG_WRN("queue full");

                }
                default:
                    LOG_WRN("Failed to queue launcher data (%d)", err);
                }
            }
            err = k_work_submit(&dfu_usb_work);
        break;


    default:
        LOG_ERR("Invalid report ID %d requested", setup->wValue & HID_GET_REPORT_ID_MASK);
        return -EINVAL;
    }

    return 0;
}
#endif

static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb,
    .int_out_ready = out_ready_cb,
    .get_report = get_report_cb,
    // .set_report = set_report_cb,
};

static int zmk_usb_dfu_send_report(const uint8_t *report, size_t len) {
    switch (zmk_usb_get_status()) {
    case USB_DC_SUSPEND:
        return usb_wakeup_request();
    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return -ENODEV;
    default:
        
        k_sem_take(&hid_sem, K_MSEC(30));
        int err =0;
        // LOG_HEXDUMP_DBG(report,len,"tx");
        err = hid_int_ep_write(dfu_hid_dev, report, len, NULL);

        if (err) {
            k_sem_give(&hid_sem);
            LOG_ERR("hid ep write err:%d",err);
        }
        // k_busy_wait(125);
        return err;
    }
}
uint8_t usb_send_user_if_data(uint8_t report_id, uint8_t *data, uint16_t len)
{
    uint8_t buf[64]={report_id};
    uint8_t length = len>63?63:len;
    memcpy(&buf[1],data,length);
    zmk_usb_dfu_send_report(buf,length+1);
    return 1;
}

static int zmk_usb_dfu_init(void) {
    dfu_hid_dev = device_get_binding("HID_2");
    if (dfu_hid_dev == NULL) {
        LOG_ERR("Unable to locate HID device");
        return -EINVAL;
    }

    usb_hid_register_device(dfu_hid_dev, report_descs, sizeof(report_descs), &ops);
    usb_hid_init(dfu_hid_dev);

    return 0;
}

SYS_INIT(zmk_usb_dfu_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
