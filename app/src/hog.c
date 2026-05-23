/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/settings/settings.h>
#include <zephyr/init.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

#include <zmk/ble.h>
#include <zmk/endpoints_types.h>
#include <zmk/hog.h>
#include <zmk/hid.h>
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)

enum zmk_transport get_hardware_select_transport(void);
int zmk_ppt_send_keyboard_report(uint8_t *report, uint8_t len);
int zmk_ppt_send_consumer_report(uint8_t *report, uint8_t len);
enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info {
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code;     /* country HID Device hardware is localized for. */
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id;   /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE | HIDS_REMOTE_WAKE,
};

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_FEATURE = 0x03,
};

static struct hids_report input = {
    .id = ZMK_HID_REPORT_ID_KEYBOARD,
    .type = HIDS_INPUT,
};

static struct hids_report input_nkro = {
    .id = ZMK_HID_REPORT_ID_KEYBOARD_NKRO,
    .type = HIDS_INPUT,
};

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)

static struct hids_report led_indicators = {
    .id = ZMK_HID_REPORT_ID_LEDS,
    .type = HIDS_OUTPUT,
};

#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)

static struct hids_report consumer_input = {
    .id = ZMK_HID_REPORT_ID_CONSUMER,
    .type = HIDS_INPUT,
};

#if IS_ENABLED(CONFIG_ZMK_MOUSE)

static struct hids_report mouse_input = {
    .id = ZMK_HID_REPORT_ID_MOUSE,
    .type = HIDS_INPUT,
};

#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

static bool host_requests_notification = false;
static uint8_t ctrl_point;
// static uint8_t proto_mode;

static ssize_t read_hids_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_info));
}

static ssize_t read_hids_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static ssize_t read_hids_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, zmk_hid_report_desc,
                             sizeof(zmk_hid_report_desc));
}

static ssize_t read_hids_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      void *buf, uint16_t len, uint16_t offset) {
    struct zmk_hid_keyboard_report_body *report_body = &zmk_hid_get_keyboard_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_body,
                             sizeof(struct zmk_hid_keyboard_report_body));
}

static ssize_t read_hids_input_nkro_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                           void *buf, uint16_t len, uint16_t offset) {
    if (zmk_get_nkro_status()) {
        struct zmk_hid_keyboard_report_body *report_body = &zmk_hid_get_keyboard_report()->body;
        uint8_t buffer[(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8 + 1];
        buffer[0] = report_body->modifiers;
        memcpy(buffer + 1, report_body->keys, (ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8);
        return bt_gatt_attr_read(conn, attr, buf, len, offset, buffer,
                                 sizeof(struct zmk_hid_keyboard_report_body) - 1);
    }
    return 0;
}

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
static ssize_t write_hids_leds_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      const void *buf, uint16_t len, uint16_t offset,
                                      uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len != sizeof(struct zmk_hid_led_report_body)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    struct zmk_hid_led_report_body *report = (struct zmk_hid_led_report_body *)buf;
    int profile = zmk_ble_active_profile_index(); // zmk_ble_profile_index(bt_conn_get_dst(conn));
    if (profile < 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    struct zmk_endpoint_instance endpoint = {.transport = ZMK_TRANSPORT_BLE,
                                             .ble = {
                                                 .profile_index = profile,
                                             }};
    zmk_hid_indicators_process_report(report, endpoint);

    return len;
}

#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)

static ssize_t read_hids_consumer_input_report(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr, void *buf,
                                               uint16_t len, uint16_t offset) {
    struct zmk_hid_consumer_report_body *report_body = &zmk_hid_get_consumer_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_body,
                             sizeof(struct zmk_hid_consumer_report_body));
}

#if IS_ENABLED(CONFIG_ZMK_MOUSE)
static ssize_t read_hids_mouse_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len, uint16_t offset) {
    struct zmk_hid_mouse_report_body *report_body = &zmk_hid_get_mouse_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_body,
                             sizeof(struct zmk_hid_mouse_report_body));
}
#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

// static ssize_t write_proto_mode(struct bt_conn *conn,
//                                 const struct bt_gatt_attr *attr,
//                                 const void *buf, uint16_t len, uint16_t offset,
//                                 uint8_t flags)
// {
//     printk("PROTO CHANGED\n");
//     return 0;
// }

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    host_requests_notification = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    return len;
}

/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(
    hog_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    //    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
    //                           BT_GATT_PERM_WRITE, NULL, write_proto_mode, &proto_mode),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_hids_info,
                           NULL, &info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
                           read_hids_report_map, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &input),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_consumer_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &consumer_input),
    // add nkro report
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_input_nkro_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &input_nkro),

#if IS_ENABLED(CONFIG_ZMK_MOUSE)
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_mouse_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &mouse_input),
#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, NULL,
                           write_hids_leds_report, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &led_indicators),
#endif // IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point));

struct bt_conn *destination_connection(void) {
    struct bt_conn *conn;
    bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    LOG_DBG("Address pointer %p", addr);
    if (!bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
        LOG_WRN("Not sending, no active address for current profile");
        return NULL;
    } else if ((conn = bt_conn_lookup_addr_le(zmk_ble_active_profile_btid(), addr)) == NULL) {
        LOG_WRN("Not sending, not connected to active profile:%d", zmk_ble_active_profile_index());
        char addr_str[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
        LOG_DBG("addr:%s", addr_str);
        return NULL;
    }

    return conn;
}

K_THREAD_STACK_DEFINE(hog_q_stack, CONFIG_ZMK_BLE_THREAD_STACK_SIZE);

struct k_work_q hog_work_q;
#if 1
K_MSGQ_DEFINE(zmk_hog_keyboard_msgq, sizeof(struct zmk_hid_keyboard_report_body),
              CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE, 4);

void send_keyboard_report_callback(struct k_work *work) {
    struct zmk_hid_keyboard_report_body report;

    while (k_msgq_get(&zmk_hog_keyboard_msgq, &report, K_NO_WAIT) == 0) {
        // if(get_hardware_select_transport()==ZMK_TRANSPORT_BLE)
        {
            struct bt_conn *conn = destination_connection();
            if (conn == NULL) {
                return;
            }

            struct bt_gatt_notify_params notify_params = {
                .attr = &hog_svc.attrs[5],
                .data = &report,
                .len = CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE + 2, // sizeof(report),
            };
            if (report._reserved) {

                uint8_t buffer[(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8 + 1];
                buffer[0] = report.modifiers;
                memcpy(buffer + 1, report.keys, (ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8);

                notify_params.attr = &hog_svc.attrs[13];
                notify_params.data = buffer;
                notify_params.len = sizeof(report) - 1;
            }
            // toggle_debug_pin();
            int err = bt_gatt_notify_cb(conn, &notify_params);
            // toggle_debug_pin();
            if (err == -EPERM) {
                bt_conn_set_security(conn, BT_SECURITY_L2);
            } else if (err) {
                LOG_DBG("Error notifying %d", err);
            }

            bt_conn_unref(conn);
        }
        // else
        // {
        //     LOG_ERR("msg:%d",k_msgq_num_used_get(&zmk_hog_keyboard_msgq));
        //     int ret=zmk_ppt_send_keyboard_report((uint8_t
        //     *)&report,zmk_get_nkro_status()?sizeof(report):CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE+2);
        //     if(ret !=0)
        //     {
        //         LOG_ERR("ppt send err:%d",ret);
        //     }
        // }
    }
}

K_WORK_DEFINE(hog_keyboard_work, send_keyboard_report_callback);

int zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report_body *report) {
    int err = k_msgq_put(&zmk_hog_keyboard_msgq, report, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("Keyboard message queue full, popping first message and queueing again");
            struct zmk_hid_keyboard_report_body discarded_report;
            k_msgq_get(&zmk_hog_keyboard_msgq, &discarded_report, K_NO_WAIT);
            return zmk_hog_send_keyboard_report(report);
        }
        default:
            LOG_WRN("Failed to queue keyboard report to send (%d)", err);
            return err;
        }
    }

    k_work_submit_to_queue(&hog_work_q, &hog_keyboard_work);

    return 0;
};
#endif
K_MSGQ_DEFINE(zmk_hog_consumer_msgq, sizeof(struct zmk_hid_consumer_report_body),
              CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE, 4);

void send_consumer_report_callback(struct k_work *work) {
    struct zmk_hid_consumer_report_body report;

    while (k_msgq_get(&zmk_hog_consumer_msgq, &report, K_NO_WAIT) == 0) {
        // if(get_hardware_select_transport()==ZMK_TRANSPORT_BLE)
        {
            struct bt_conn *conn = destination_connection();
            if (conn == NULL) {
                return;
            }

            struct bt_gatt_notify_params notify_params = {
                .attr = &hog_svc.attrs[9],
                .data = &report,
                .len = sizeof(report),
            };

            int err = bt_gatt_notify_cb(conn, &notify_params);
            if (err == -EPERM) {
                bt_conn_set_security(conn, BT_SECURITY_L2);
            } else if (err) {
                LOG_DBG("Error notifying %d", err);
            }

            bt_conn_unref(conn);
        }
        // else
        // {
        //     int ret=zmk_ppt_send_consumer_report((uint8_t*)&report,2);
        //     if(ret !=0)
        //     {
        //         LOG_ERR("ppt send err:%d",ret);
        //     }
        // }
    }
};

K_WORK_DEFINE(hog_consumer_work, send_consumer_report_callback);

int zmk_hog_send_consumer_report(struct zmk_hid_consumer_report_body *report) {
    int err = k_msgq_put(&zmk_hog_consumer_msgq, report, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("Consumer message queue full, popping first message and queueing again");
            struct zmk_hid_consumer_report_body discarded_report;
            k_msgq_get(&zmk_hog_consumer_msgq, &discarded_report, K_NO_WAIT);
            return zmk_hog_send_consumer_report(report);
        }
        default:
            LOG_WRN("Failed to queue consumer report to send (%d)", err);
            return err;
        }
    }

    k_work_submit_to_queue(&hog_work_q, &hog_consumer_work);

    return 0;
};

#if IS_ENABLED(CONFIG_ZMK_MOUSE)

K_MSGQ_DEFINE(zmk_hog_mouse_msgq, sizeof(struct zmk_hid_mouse_report_body),
              CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE, 4);

void send_mouse_report_callback(struct k_work *work) {
    struct zmk_hid_mouse_report_body report;
    while (k_msgq_get(&zmk_hog_mouse_msgq, &report, K_NO_WAIT) == 0) {
        struct bt_conn *conn = destination_connection();
        if (conn == NULL) {
            return;
        }

        struct bt_gatt_notify_params notify_params = {
            .attr = &hog_svc.attrs[13 + 4], // ccc
            .data = &report,
            .len = sizeof(report),
        };

        int err = bt_gatt_notify_cb(conn, &notify_params);
        if (err == -EPERM) {
            bt_conn_set_security(conn, BT_SECURITY_L2);
        } else if (err) {
            LOG_DBG("Error notifying %d", err);
        }

        bt_conn_unref(conn);
    }
};

K_WORK_DEFINE(hog_mouse_work, send_mouse_report_callback);

int zmk_hog_send_mouse_report(struct zmk_hid_mouse_report_body *report) {
    int err = k_msgq_put(&zmk_hog_mouse_msgq, report, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("Consumer message queue full, popping first message and queueing again");
            struct zmk_hid_mouse_report_body discarded_report;
            k_msgq_get(&zmk_hog_mouse_msgq, &discarded_report, K_NO_WAIT);
            return zmk_hog_send_mouse_report(report);
        }
        default:
            LOG_WRN("Failed to queue mouse report to send (%d)", err);
            return err;
        }
    }

    k_work_submit_to_queue(&hog_work_q, &hog_mouse_work);

    return 0;
};

#endif // IS_ENABLED(CONFIG_ZMK_MOUSE)

static int zmk_hog_init(void) {
    static const struct k_work_queue_config queue_config = {.name = "HID Over GATT Send Work"};
    k_work_queue_start(&hog_work_q, hog_q_stack, K_THREAD_STACK_SIZEOF(hog_q_stack),
                       CONFIG_ZMK_BLE_THREAD_PRIORITY, &queue_config);

    return 0;
}

SYS_INIT(zmk_hog_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);
#if 1
bool hog_almost_full(void) { return k_msgq_num_used_get(&zmk_hog_keyboard_msgq) > 20; }
#else

#include <zephyr/sys/ring_buffer.h>
uint8_t zmk_ble_is_ready(void);
RING_BUF_DECLARE(zmk_ble_msgs, sizeof(struct zmk_hid_keyboard_report) * 64);
static void poll_timer_expiry_function(struct k_timer *timer);
static K_TIMER_DEFINE(poll_timer, poll_timer_expiry_function, NULL);
#define POLL_PERIOD K_MSEC(8)
static struct k_spinlock lock;
void ringbuf_ble_reset(void) { ring_buf_reset(&zmk_ble_msgs); }
int ringbuf_ble_used_get(void) {
    return ring_buf_size_get(&zmk_ble_msgs) / (sizeof(struct zmk_hid_keyboard_report));
}
bool hog_almost_full(void) { return ringbuf_ble_used_get() > 20; }
int ringbuf_ble_msg_peek(struct zmk_hid_keyboard_report *rep) {
    return ring_buf_peek(&zmk_ble_msgs, (uint8_t *)rep, sizeof(struct zmk_hid_keyboard_report));
}
int ringbuf_ble_msg_get(struct zmk_hid_keyboard_report *rep) {
    return ring_buf_get(&zmk_ble_msgs, (uint8_t *)rep, sizeof(struct zmk_hid_keyboard_report));
}
int ringbuf_ble_msg_put(struct zmk_hid_keyboard_report *rep) {
    int err = 0;

    if (ring_buf_space_get(&zmk_ble_msgs) >= sizeof(struct zmk_hid_keyboard_report)) {
        k_spinlock_key_t key = k_spin_lock(&lock);
        int wr =
            ring_buf_put(&zmk_ble_msgs, (uint8_t *)rep, sizeof(struct zmk_hid_keyboard_report));
        if (wr < sizeof(struct zmk_hid_keyboard_report)) {
            LOG_ERR("write err!");
        }
        k_spin_unlock(&lock, key);
    } else {
        LOG_ERR("no buf,discard old one!");
        struct zmk_hid_keyboard_report rep;
        ringbuf_ble_msg_get(&rep);
        err = 1;
    }
    return err;
}

int zmk_hog_send_keyboard_report(struct zmk_hid_keyboard_report *report) {

    struct zmk_hid_keyboard_report rep;

    memcpy(&rep, report, sizeof(struct zmk_hid_keyboard_report));

    if (!zmk_ble_is_ready())
        return -1;

    ringbuf_ble_msg_put(&rep);
    LOG_DBG("msg:%d", ringbuf_ble_used_get());
    if (k_timer_remaining_get(&poll_timer) == 0)
        poll_timer_expiry_function(NULL);

    return 0;
}
void poll_timer_expiry_function(struct k_timer *timer) {
    uint8_t count = 0;
    while (ringbuf_ble_used_get()) {
        struct zmk_hid_keyboard_report rep;
        ringbuf_ble_msg_get(&rep);
        struct bt_conn *conn = destination_connection();
        if (conn == NULL) {
            break;
        }
        uint8_t attr_index = 5;
        uint8_t tx_len = 8;
        uint8_t buffer[(ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8 + 1] = {0};
        if (rep.report_id == ZMK_HID_REPORT_ID_KEYBOARD_NKRO) {
            tx_len = sizeof(struct zmk_hid_keyboard_report) - 2;
            buffer[0] = rep.body.modifiers;
            memcpy(buffer + 1, rep.body.keys, (ZMK_HID_KEYBOARD_NKRO_MAX_USAGE + 1) / 8);
            attr_index = 13;
        } else {
            tx_len = 8;
            attr_index = 5;
            memcpy(buffer, &rep.body, tx_len);
        }

        struct bt_gatt_notify_params notify_params = {
            .attr = &hog_svc.attrs[attr_index],
            .data = buffer,
            .len = tx_len,
        };
        int err = bt_gatt_notify_cb(conn, &notify_params);
        if (err == -EPERM) {
            bt_conn_set_security(conn, BT_SECURITY_L2);
        } else if (err) {
            LOG_DBG("Error notifying %d", err);
        }

        bt_conn_unref(conn);
        if (++count >= 3)
            break;
    }

    if (ringbuf_ble_used_get())
        k_timer_start(&poll_timer, POLL_PERIOD, K_FOREVER);
}
#endif